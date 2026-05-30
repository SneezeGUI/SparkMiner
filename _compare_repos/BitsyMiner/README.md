# BitsyMiner Open Source

BitsyMiner is a Bitcoin lottery miner application designed to run on ESP32 microcontroller devices.

![Image of BitsyMiner Screen](/assets/bitsy_image.jpg)

<br/><br/>
### Required Hardware

BitsyMiner was first programmed to run specifically on "Cheap Yellow Display" boards with 2.8" ILI9341 displays. I have since gotten it working on ST7789 displays, as well as the 2.4" ILI9341 boards, which have a slightly different pin configuration.

You can also compile and run the code in headless mode (no display), which should work on boards that fall under the ESP32 Dev Module category.

The current code includes inline assembly that is very hardware-dependent and is not compatible with other board types.


<br/><br/>
### Installation

**Option 1:** Compile from Source

Set up your environment by installing all of the required libraries in the Arduino IDE, attach your device, compile, and install.

Changes to display type can be set in defines_n_types.h.

In the tools menu, change the settings as follow:

- Arduino Runs on Core 0
- Events Run on Core 0
- Parition Scheme:  Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)


**Option 2:** Install from Binaries

You can now find the binaries in the individual releases. You will see two types of binaries, one for doing a new install, and one for upgrading a device. The first will overwrite the entire device, and the latter can be used for just overwriting the application area so you don't lose your data.

Use the [ESP Tool](https://espressif.github.io/esptool-js/) to connect to and program the device.

For a new installation, set the flash address to 0x0 and write the "New Install" binary.

For upgrading an existing BitsyMiner installation, set the flash address to 0x10000 and write the "Upgrade" binary.

After installing, you can follow the setup video [here](https://www.youtube.com/watch?v=Ur3amBXdaBI).

<br/><br/>
### Programming Environment

BitsyMiner started as a personal project to learn more about Bitcoin mining. For simplicity's sake, I began working in the [Arduino IDE](https://www.arduino.cc/en/software/), and I never left it.


<br/><br/>
### Required Libraries

ArduinoJson
Copyright © 2014-2024, Benoit BLANCHON
MIT License
https://github.com/bblanchon/ArduinoJson/blob/7.x/LICENSE.txt

NTPClient by Fabrice WeinBerg
MIT License
https://github.com/arduino-libraries/NTPClient

CustomJWT
Antony Jose Kuruvilla
Public Domain
https://github.com/Ant2000/CustomJWT/blob/main/LICENSE

PNGDec
Copyright 2020 BitBank Software, Inc.
Apache License 2.0
https://github.com/bitbank2/PNGdec/blob/master/LICENSE

TFT_eSPI
Bodmer
MIT License
https://github.com/Bodmer/TFT_eSPI/blob/master/license.txt

QRCode
Copyright (c) 2017 Richard Moore
MIT License
https://github.com/ricmoo/QRCode/blob/master/LICENSE.txt

XPT2046_Touchscreen
Paul Stoffregen
No license defined (Public Domain)
https://github.com/PaulStoffregen/XPT2046_Touchscreen    


<br/><br/>
## Support

The binaries and code are offered as-is. No support or guarantee of any kind is available for BitsyMiner Open Source.



<br/><br/>
## License

BitsyMiner Open Source is licensed under the GNU General Public License v3.0 (GPL-3.0).

Commercial licenses for integrating BitsyMiner into proprietary products or for using the
Enhanced / Pro edition are available. For details, contact: bitsyminer@protonmail.com
