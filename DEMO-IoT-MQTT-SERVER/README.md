# ESP32 LED Control with MQTT, WebSockets, and Web Dashboard**

This project implements a complete system for controlling an **ESP32 RGB LED** using a **modern web interface**, an **MQTT broker**, and **WebSockets**.
The system allows any user to select a custom LED color from a dashboard and send it directly to the ESP32 in real time.

---

# **Project Overview**

This setup includes:

* **An EC2 Ubuntu Server (AWS)**
* **Mosquitto MQTT Broker** with authentication
* **WebSockets on port 9001** (enabled in Mosquitto)
* **A modern web dashboard (HTML + JS)** hosted on Apache
* **Real-time color control (RGB sliders, HEX picker, quick buttons)**
* **ESP32 subscribed to the MQTT topic** to update the LED color

---

# 🖥️ **1. Server Setup (Ubuntu 22.04 on AWS EC2)**

Update packages:

```bash
sudo apt update && sudo apt upgrade -y
```

Install Apache Web Server:

```bash
sudo apt install apache2 -y
```

The webpage will be stored in:

```
/var/www/html/
```

---

# **2. Install and Configure Mosquitto Broker**

Install Mosquitto and its tools:

```bash
sudo apt install mosquitto mosquitto-clients -y
```

Enable the service:

```bash
sudo systemctl enable mosquitto
```

---

# **3. Create User Authentication for MQTT**

Create password file:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd username
```

Set permissions:

```bash
sudo chown mosquitto:mosquitto /etc/mosquitto/passwd
sudo chmod 640 /etc/mosquitto/passwd
```

---

# **4. Configure Mosquitto for WebSockets**

Edit configuration file:

```bash
sudo nano /etc/mosquitto/mosquitto.conf
```

Add:

```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd

listener 9001
protocol websockets
```

Restart:

```bash
sudo systemctl restart mosquitto
```

---

# **5. Upload the Web Dashboard (index.html)**

Replace the default Apache page:

```bash
sudo nano /var/www/html/index.html
```

Paste the modern RGB dashboard code (sliders, color picker, quick colors, MQTT WebSockets).

Make sure to update your server public IP:

```js
const client = mqtt.connect("ws://YOUR_PUBLIC_IP:9001", {
    username: "username",
    password: "YOUR_PASSWORD"
});
```

# **6. ESP32 MQTT Code (LED Control)**

The ESP32 must subscribe to the topic:

```
esp32/led
```

On message received:

* Convert HEX → RGB
* Set the LED using `neopixel` / GPIO PWM

Example logic:

```cpp
client.subscribe("esp32/led");

void callback(char* topic, byte* payload, unsigned int length) {
    String hex = "";

    for (int i = 0; i < length; i++) {
        hex += (char)payload[i];
    }

    int r = strtol(hex.substring(1,3).c_str(), NULL, 16);
    int g = strtol(hex.substring(3,5).c_str(), NULL, 16);
    int b = strtol(hex.substring(5,7).c_str(), NULL, 16);

    // Set LED here (WS2812 or analog RGB)
}
```

---

# **7. Testing MQTT**

Send a message:

```bash
mosquitto_pub -h localhost -t esp32/led -m "#ff0000" -u paulina -P "PASSWORD"
```

Subscribe from terminal:

```bash
mosquitto_sub -h localhost -t esp32/led -u paulina -P "PASSWORD"
```

---

# **8. Access the Web App**

Open your server public IP:

```
http://YOUR_PUBLIC_IP/
```

You should see the full dark dashboard with:

* Circle color preview
* Sliders
* HEX selector
* Quick color buttons
* Live MQTT status

---

# **9. Final System Flow**

```
[Web Dashboard] → MQTT via WebSockets → [Mosquitto Broker] → ESP32 → LED
```

