# ESP8266iTachEmulator

Send infrared remote control signals via WLAN using an ESP8266

![irbox](https://cloud.githubusercontent.com/assets/2480569/17837757/ea087514-67bb-11e6-9638-3812f706d5da.JPG)

## Purpose

Implements sending of infrared remote control commands using (subsets of)
* [iTach API Specification Version 1.5](http://www.globalcache.com/files/docs/API-iTach.pdf)
* LIRC with SEND_CCF_ONCE extensions

## Compatibility

Known to work with
* IrScrutinizer 1.1.2 from https://github.com/bengtmartensson/harctoolboxbundle
* iPhone iRule app with remote created with cloud codes online at http://iruleathome.com

## Example use case

Send infrared remote codes via WLAN from [IRScrutinizer](https://github.com/bengtmartensson/harctoolboxbundle)

![IRScrutinizer](http://www.hifi-remote.com/wiki/images/7/77/Irscrutinizer_F9.png)

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

### Hardware

Minimal hardware needed:

* NodeMCU 1.0 module (around USD 3 from China shipped) or, if it should fit a small enclosure as shown on the picture, 1 bare ESP12E module (around USD 2 from China shipped; in this case need a matching programming jig too in order to flash it initially, and you need to use the usual minimal circuit to pull up CH_PD and pull down GPIO15)
* Infrared LED
* 2N7000 N-channel transistor
* Resistor to drive the transistor

Circut:

* To get good range, attach a resistor to pin 12 of the ESP-12E and connect the resistor to the G pin of a 2N7000 transistor
* If you look at the flat side of the 2N7000 you have S, G, D pins.
* Connect S to GND, G to the resistor to the MCU, and D to the IR LED short pin.
* The long pin of the IR LED is connected to +3.3V.
* I picked the 2N7000 because unlike others it will not pull GPIO2 down which would prevent the chip from booting (if using an ESP-1 module; I prefer ESP-12 now)

Improvements regarding the hardware setup welcome! 

Issue with this hardware setup: 
* Probably does not use full range potential of the LED; should change the circuit so that it maximizes the allowable burst current of the LED
* During bootup of the ESP, the LED is lit. If we want to change the circuit so that it maximizes the allowable burst current of the LED, then this must not happen
* Not using a resistor in series with the LED is not a good idea

### Versions of libaries known to work

* ArduinoJson 409ca7e
* ESPAsyncTCP 5987225
* ESPAsyncWebServer bfde9bc
* ESPmanager 6c902fb
* IRremoteESP8266 fee16e8
* ProntoHex 847a608
* rc-switch 62b9ebf

## Debugging

Connect with telnet to port 22 for debug output. I am using this debug port instead of/in addition to a serial line.

## TODO

Pull requests welcome!

* Switch everything to async, e.g., using ESPAsyncUDP
* Implement receiving (sebastienwarin/IRremoteESP8266 supports it) using a TSOP
* Implement learning - ideally we could use the sending LED for this 
* A HTTP GUI for sending codes that are stored in SPIFFS and can be managed using a GUI
* Implement a [GIRS server](https://github.com/bengtmartensson/AGirs/tree/master/src/GirsLite)

## Credits

* igrr for esp8266/Arduino
* The authors of the libraries used
* sticilface for ESPmanager and help while debugging
