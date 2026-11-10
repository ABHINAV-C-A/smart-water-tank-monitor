# Smart Water Tank Monitor

A water level monitor built with an **ESP8266 NodeMCU** and an **HC-SR04 ultrasonic sensor**. It shows the tank level on an LCD, on a live web page on your home network, and through a **Telegram bot** so you can check it from anywhere.

## Features

- Level bar and percentage on a 16x2 I2C LCD
- Live web page with:
  - animated tank and percentage
  - status badge (OK, LOW, FULL, SENSOR ERROR)
  - water height (cm) and estimated volume (litres)
  - distance from sensor, 1-minute trend (filling / draining / steady)
  - sensor status, time since last good reading, Wi-Fi signal and uptime
- Telegram bot:
  - `/level` returns the current level and details
  - automatic alerts for low water, full tank and sensor errors
- Sensor error detection: if the sensor gets no echo, the LCD, web page and Telegram all warn you instead of showing an old value
- Median filtering of 7 readings to reduce noise

## Hardware

| Part | Quantity |
|---|---|
| ESP8266 NodeMCU | 1 |
| HC-SR04 ultrasonic sensor | 1 |
| 16x2 LCD with I2C backpack | 1 |
| Jumper wires, USB cable and power supply | as needed |

## Wiring

| Part | Pin | NodeMCU |
|---|---|---|
| LCD | GND | G |
| LCD | VCC | VU (5V) |
| LCD | SDA | D2 |
| LCD | SCL | D1 |
| HC-SR04 | VCC | VU (5V) |
| HC-SR04 | GND | G |
| HC-SR04 | Trig | D5 |
| HC-SR04 | Echo | D6 |

Note: the HC-SR04 Echo pin outputs 5V. A 1k/2k resistor divider on Echo is safer for the ESP8266.

## Software setup

1. Install the **Arduino IDE** and add ESP8266 board support.
2. Install these libraries from the Library Manager:
   - LiquidCrystal I2C (Frank de Brabander)
   - UniversalTelegramBot (Brian Lough)
   - ArduinoJson (Benoit Blanchon), version 6.x
3. Select board **NodeMCU 1.0 (ESP-12E Module)** and the correct port.

## Telegram setup

1. In Telegram, open **@BotFather**, send `/newbot` and copy the **bot token**.
2. Get your numeric **chat ID** (for example with **@userinfobot**).
3. Open your new bot and press **Start** once.

## Configuration

Edit these values at the top of the sketch:

```cpp
const char *WIFI_SSID = "YOUR_WIFI_NAME";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";
#define BOT_TOKEN "PASTE_ONLY_THE_TOKEN_HERE"
#define CHAT_ID   "PASTE_YOUR_CHAT_ID_HERE"

const float EMPTY_CM = 120.0;    // sensor-to-bottom distance when empty
const float FULL_CM  = 10.0;     // sensor-to-water distance when full
const float TANK_LITRES = 1000;  // tank capacity in litres
const int LOW_PERCENT  = 20;
const int FULL_PERCENT = 95;
```

Measure `EMPTY_CM` and `FULL_CM` on your own tank after mounting the sensor.

**Never commit your real Wi-Fi password or bot token to GitHub.** Keep only placeholders in the repository.

## Usage

1. Upload the sketch.
2. The LCD shows the local IP address at startup. Open it in a browser on the same Wi-Fi to see the live page.
3. In Telegram, send `/level` to your bot from anywhere.

## Mounting tips

- Mount the sensor at the top, facing straight down at open water.
- Keep it away from side walls, pipes and floats, since the beam spreads and can bounce off them.
- Keep it at least 15 cm above the highest water level, and protect it from condensation and splashes.

## Troubleshooting

| Problem | Fix |
|---|---|
| LCD is blank | Change the I2C address `0x27` to `0x3F`, or adjust the contrast screw |
| Upload says "no upload port" | Select the port under Tools > Port and use a data USB cable |
| Level stays wrong when the tank is empty | Check the distance on the web page; something may be in the sensor beam |
| Telegram does not reply | Check that BOT_TOKEN has only the token (no link) and that you pressed Start on the bot |

## License

MIT
