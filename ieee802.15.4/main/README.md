# **README — IEEE 802.15.4 CLI Example**

This example demonstrates how to build a simple **command-line interface (CLI)** application for interacting with the **IEEE 802.15.4 radio** on ESP32 devices using **ESP-IDF**.
It exposes several radio-control commands through a UART-based console, allowing developers to manually test, configure, and debug IEEE 802.15.4 functionality.

---

## **Overview**

The application initializes the IEEE 802.15.4 subsystem and starts an interactive console (REPL) over UART.
Through this console, users can execute predefined commands to:

* Configure IEEE 802.15.4 radio parameters
* Send and receive frames
* Enable debugging features
* Inspect system information
* Interact with ESP-IDF system utilities

---

## **Main Features**

### **IEEE 802.15.4 Radio Activation**

The radio is enabled through:

```c
esp_ieee802154_enable();
```

This prepares the PHY/MAC layer for commands issued from the console.

### **UART-Based Console (REPL)**

A REPL interface is created using the ESP-IDF console subsystem:

* Customizable prompt (`ieee802154>`)
* 256-character command line length
* Auto-registered help command
* UART configured as the console backend

### **Command Registration**

The following command groups are registered:

* **IEEE 802.15.4 control commands** (`register_ieee802154_cmd()`)
* **System commands** (`register_system_common()`)
* **Debug commands** (optional, only if `CONFIG_IEEE802154_DEBUG` enabled)

These commands allow interacting with PHY parameters, sending frames, enabling tracing, and inspecting device information.

---

## **Runtime Behavior**

Once the device boots:

1. NVS is initialized
2. The IEEE 802.15.4 driver is enabled
3. A REPL console over UART is started
4. The user sees a prompt such as:

   ```
   ieee802154>
   ```
5. Commands can be typed directly to control the radio or query system data.

The example remains running while responding to console input until the device is reset.

---

## **File: `clic.c` Purpose**

The `clic.c` file serves as the **entry point** of the application:

* Sets up storage
* Enables IEEE 802.15.4 radio
* Configures the console
* Registers all related commands
* Starts the REPL loop

This creates a fully interactive environment for testing IEEE 802.15.4 communication features.

---

## **Dependencies**

The example relies on the following ESP-IDF components:

* `esp_ieee802154` — IEEE 802.15.4 radio driver
* `esp_console` — Console/REPL subsystem
* `nvs_flash` — Non-volatile storage
* `esp_phy_init` — PHY initialization
* `driver/uart` (via console backend)

---

## **How to Use**

1. Build and flash the example using ESP-IDF:

   ```bash
   idf.py build flash monitor
   ```
2. Open the serial monitor.
3. At the prompt `ieee802154>`, type:

   ```bash
   help
   ```
4. Explore the available IEEE 802.15.4 commands.

