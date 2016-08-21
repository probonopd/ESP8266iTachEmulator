# ESP8266iTachEmulator

## Purpose

Implements sending of infrared remote control commands using (subsets of)
* [iTach API Specification Version 1.5](http://www.globalcache.com/files/docs/API-iTach.pdf)
* LIRC with SEND_CCF_ONCE extensions

## Compatibility

Known to work with
* IrScrutinizer 1.1.2 from https://github.com/bengtmartensson/harctoolboxbundle
* iPhone iRule app with remote created with cloud codes online at http://iruleathome.com

## Building

**ESP8266iTachEmulatorGUI** is the most recent version that should be used. It uses [ESP Manager](https://github.com/sticilface/ESPmanager) to provide a GUI to set up the WLAN credentials, over-the-air updates, etc.


```
# Get the libraries
sudo apt-get install -y git
cd $HOME
# rm -rf Arduino/ # Uncomment this if you are sure what you are doing
mkdir -p Arduino/libraries/
cd Arduino/libraries/
git clone https://github.com/sebastienwarin/IRremoteESP8266.git
git clone https://github.com/sui77/rc-switch.git
git clone https://github.com/probonopd/ProntoHex.git
git clone https://github.com/sticilface/ESPmanager.git
git clone https://github.com/me-no-dev/ESPAsyncWebServer.git
git clone https://github.com/me-no-dev/ESPAsyncTCP.git
git clone https://github.com/bblanchon/ArduinoJson.git

# Get the SPIFFS uploader if you don't have it yet
mkdir $HOME/Arduino/tools
cd $HOME/Arduino/tools
wget -c "https://github.com/esp8266/arduino-esp8266fs-plugin/releases/download/0.2.0/ESP8266FS-0.2.0.zip"
unzip ESP8266FS-0.2.0.zip
rm ESP*.zip
cd -

# Then compile with
Arduino-1.6.11.hourly201608161225.esp497d19d-x86_64.AppImage

# Upload the SPIFFS for the ESP Manager from the ESP Manager sample sketch using the SPIFFS uploader.
```

### Versions of libaries known to work

* ArduinoJson 409ca7e
* ESPAsyncTCP 5987225
* ESPAsyncWebServer bfde9bc
* ESPmanager 6c902fb
* IRremoteESP8266 fee16e8
* ProntoHex 847a608
* rc-switch 62b9ebf

## TODO

* Switch everything to async, e.g., using ESPAsyncUDP
* Implement receiving/learning (sebastienwarin/IRremoteESP8266 supports it)
* A HTTP GUI for sending codes that are stored in SPIFFS and can be managed using a GUI
