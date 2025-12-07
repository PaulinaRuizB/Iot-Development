/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <stdlib.h>
#include "esp_netif.h"   



#include "lwip/err.h"
#include "lwip/sys.h"
#include "mqtt_client.h"
#include "led_strip.h"

// Configuración del LED RGB WS2812 en ESP32-C6-DevKitC-1
#define LED_STRIP_GPIO    8
#define LED_NUMBERS       1

static esp_mqtt_client_handle_t mqtt_client = NULL;
static led_strip_handle_t led_strip = NULL;

// ======== SECUENCIA DE 3 COLORES CONFIGURABLES ========
static uint8_t seq[3][3] = { {255,0,0}, {0,255,0}, {0,0,255} }; // default: R,G,B
static SemaphoreHandle_t seq_mutex = NULL;

// Convierte "#RRGGBB" o "RRGGBB" a RGB
static bool parse_hex_rgb(const char* s, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (!s) return false;
    if (s[0] == '#') s++;
    if (strlen(s) != 6) return false;
    char buf[3] = {0};
    buf[0]=s[0]; buf[1]=s[1]; *r = (uint8_t) strtol(buf, NULL, 16);
    buf[0]=s[2]; buf[1]=s[3]; *g = (uint8_t) strtol(buf, NULL, 16);
    buf[0]=s[4]; buf[1]=s[5]; *b = (uint8_t) strtol(buf, NULL, 16);
    return true;
}

static void seq_init(void){
    seq_mutex = xSemaphoreCreateMutex();
}

static void seq_set(int idx, uint8_t R, uint8_t G, uint8_t B){
    if (!seq_mutex) return;
    if (idx<0 || idx>2) return;
    xSemaphoreTake(seq_mutex, portMAX_DELAY);
    seq[idx][0]=R; seq[idx][1]=G; seq[idx][2]=B;
    xSemaphoreGive(seq_mutex);
}

static void seq_get(int idx, uint8_t* R, uint8_t* G, uint8_t* B){
    if (!seq_mutex) { *R=*G=*B=0; return; }
    xSemaphoreTake(seq_mutex, portMAX_DELAY);
    *R=seq[idx][0]; *G=seq[idx][1]; *B=seq[idx][2];
    xSemaphoreGive(seq_mutex);
}


static void led_set_color(const char* color) {
    ESP_LOGI("LED", "Encendiendo LED RGB color: %s", color);
    
    if (strcmp(color, "red") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 0)); // RGB: Rojo puro
    } else if (strcmp(color, "green") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 255, 0)); // RGB: Verde puro
    } else if (strcmp(color, "blue") == 0) {
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 255)); // RGB: Azul puro
    }
    
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

static void led_task(void *pvParameters) {
    int i = 0;
    while (1) {
        uint8_t R,G,B;
        seq_get(i % 3, &R,&G,&B);

        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, R, G, B));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));

        // Publica el color actual en hex para que la web lo muestre
        if (mqtt_client) {
            char msg[16];
            snprintf(msg, sizeof(msg), "#%02X%02X%02X", R,G,B);
            esp_mqtt_client_publish(mqtt_client, "esp32/led", msg, 0, 1, 0);
        }

        ESP_LOGI("LED", "Color (%u,%u,%u) por 3s", R,G,B);
        vTaskDelay(pdMS_TO_TICKS(3000));

        // breve apagado
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 0));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        vTaskDelay(pdMS_TO_TICKS(100));

        i++;
    }
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "Connected to MQTT broker");
            // Suscripciones
            esp_mqtt_client_subscribe(event->client, "esp32/led/set", 1);  // cambio instantáneo
            esp_mqtt_client_subscribe(event->client, "esp32/led/seq", 1);  // define la secuencia
            break;

        case MQTT_EVENT_DATA: {
            const char *topic = event->topic; int tlen = event->topic_len;
            const char *data  = event->data;  int dlen = event->data_len;

            // -------- Control instantáneo (esp32/led/set) --------
            if (tlen == (int)strlen("esp32/led/set") && strncmp(topic,"esp32/led/set",tlen)==0) {
                char color[32]={0}; int n = dlen<31?dlen:31;
                // a minúsculas
                for(int i=0;i<n;i++){
                    char c = data[i];
                    color[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
                }
                ESP_LOGI("MQTT", "SET LED -> %s", color);
                led_set_color(color); // tu función existente

                // confirma estado
                if (mqtt_client) esp_mqtt_client_publish(mqtt_client, "esp32/led", color, 0, 1, 0);
            }

            // -------- Secuencia de 3 colores (esp32/led/seq) --------
            else if (tlen == (int)strlen("esp32/led/seq") && strncmp(topic,"esp32/led/seq",tlen)==0) {
                // copia payload y tokeniza
                char p[96]={0}; int n = dlen<95?dlen:95; memcpy(p, data, n);

                char* saveptr; char* token;
                int idx = 0;
                for (token = strtok_r(p, ",", &saveptr); token && idx < 3; token = strtok_r(NULL, ",", &saveptr), idx++) {
                    // trim espacios
                    while(isspace((unsigned char)*token)) token++;
                    char* end = token + strlen(token) - 1;
                    while(end>token && isspace((unsigned char)*end)) *end-- = '\0';

                    // a minúsculas
                    for(char* q=token; *q; ++q) *q = (char)tolower((unsigned char)*q);

                    uint8_t R=0,G=0,B=0;
                    if (parse_hex_rgb(token, &R,&G,&B)) {
                        // listo
                    } else {
                        // nombres comunes
                        if      (strcmp(token,"red")==0)     { R=255;G=0;  B=0;   }
                        else if (strcmp(token,"green")==0)   { R=0;  G=255;B=0;   }
                        else if (strcmp(token,"blue")==0)    { R=0;  G=0;  B=255; }
                        else if (strcmp(token,"yellow")==0)  { R=255;G=255;B=0;   }
                        else if (strcmp(token,"cyan")==0)    { R=0;  G=255;B=255; }
                        else if (strcmp(token,"magenta")==0 || strcmp(token,"purple")==0) { R=255;G=0;B=255; }
                        else if (strcmp(token,"white")==0)   { R=255;G=255;B=255; }
                        else if (strcmp(token,"orange")==0)  { R=255;G=165;B=0;   }
                        else { R=0; G=0; B=0; } // desconocido -> apaga
                    }
                    seq_set(idx, R,G,B);
                }

                ESP_LOGI("MQTT","Secuencia actualizada");
                if (mqtt_client) esp_mqtt_client_publish(mqtt_client, "esp32/status", "seq_updated", 0, 1, 0);
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT", "Disconnected from MQTT broker");
            break;

        default:
            break;
    }
}



static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://18.116.199.169:1883",
        .credentials.username = "esp32",
        .credentials.authentication.password = "password",
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "WIFI NAME"
#define EXAMPLE_ESP_WIFI_PASS      "PASSWORD"
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // Configuración del LED RGB WS2812
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_NUMBERS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "LED RGB WS2812 inicializado correctamente");

    // Prueba inicial: ciclo de colores reales
    ESP_LOGI(TAG, "Probando LED RGB...");
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 255, 0, 0)); // Rojo
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 255, 0)); // Verde
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 255)); // Azul
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, 0)); // Apagar
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
    ESP_LOGI(TAG, "Prueba de LED RGB completada");

    // Inicializa MQTT
    mqtt_app_start();

    seq_init();  // prepara el mutex de la secuencia


    // Crea la tarea para cambiar el color del LED y publicar en MQTT
    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);
}

