/*
 Implements a subset of the iTach API Specification Version 1.5
 http://www.globalcache.com/files/docs/API-iTach.pdf
 and
 LIRC server including the SEND_CCF_ONCE extensions
 http://www.harctoolbox.org/lirc_ccf.html#Extensions+to+the+irsend+command
 and
 preliminary XHR (AJAX) requests containing Pronto Hex to /messages (seems not to work with Android browsers)
 
 Tested with
 - IrScrutinizer 1.1.2 from https://github.com/bengtmartensson/harctoolboxbundle
 - iPhone iRule app with remote created with cloud codes online at http://iruleathome.com

 TODO: Implement capture https://github.com/sebastienwarin/IRremoteESP8266
       Implement repeats
       Implement more commands, e.g., stopir
       Check with OpenRemote http://sourceforge.net/projects/openremote/
       Check with apps from http://www.globalcache.com/partners/control-apps/
       MQTT? 
       WebSockets?
       Decode and Encode using John S. Fine's algorithms?
       Art-Net DMX512-A (professional stage lighting; apps exist)
*/

/*
 * Circuit:
 * To get good range, attach a resistor to pin 12 of the ESP-12E and connect the resistor to the G pin of a 2N7000 transistor.
 * If you look at the flat side of the 2N7000 you have S, G, D pins.
 * Connect S to GND, G to the resistor to the MCU, and D to the IR LED short pin.
 * The long pin of the IR LED is connected to +3.3V.
 * I picked the 2N7000 because unlike others it will not pull pin 2 down which would prevent the chip from booting.
*/

#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <RCSwitch.h>
#include <ProntoHex.h>

// GPIO12 = 12 = labelled "D6" on the NodeMCU board
const int infraredLedPin = 12; // ############# CHECK IF THE LED OR TRANSISTOR (RECOMMENDED) IS ACTUALLY ATTACHED TO THIS PIN 

extern const String MYVAL;

const char* ssid = "******";
const char* password = ""******";

const char* host = "wf2ir";
const uint16_t ota_port = 8266;
WiFiServer TelnetServer(ota_port);
WiFiClient Telnet;
WiFiUDP OTA;

IRsend irsend(infraredLedPin);

unsigned int irrawCodes[99];
int rawCodeLen; // The length of the code
int freq;

RCSwitch mySwitch = RCSwitch();

ProntoHex ph = ProntoHex();

// A http server on port 80 because IRScrutinizer has a button
// which opens that URL; so far it does not do much
MDNSResponder mdns;
ESP8266WebServer httpserver(80);

// A WiFi server for telnet-like communication on port 4998
#define MAX_SRV_CLIENTS 10 // How many clients may connect at the same time
WiFiServer server(4998);
WiFiClient serverClients[MAX_SRV_CLIENTS];

// I use debugSend() instead of the serial console to send debug information
// TODO: Receive, too. This way I can free up the TX and RX pins on the ESP8266
// and use them for other purposes
#define MAX_DBG_SRV_CLIENTS 1 // How many clients may connect at the same time
WiFiServer debugServer(22);
WiFiClient debugServerClients[MAX_DBG_SRV_CLIENTS];

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;

// Interval for sending UDP Discovery Beacons
unsigned long previousMillis = 0;
const long interval = random(10000, 60000); // Send every 10...60 seconds like the original
// const long interval = 3000; // for debugging

// ESP SDK
extern "C" {
#include "user_interface.h"
}

void setup() {

  mySwitch.enableTransmit(3); // Pin 3 is RXD; 433 MHz
  // Serial.begin(115200);
  irsend.begin();
  WiFi.begin(ssid, password);
  // Serial.print("\nConnecting to "); // Serial.println(ssid);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    // Serial.print(".");
  }
  // Serial.println("");

  // Announce the server in the nework
  MDNS.begin(host);
  MDNS.addService("http", "tcp", 80);

  // Enable OTA
  initOTA();

  // mySwitch.switchOff(4, 2); // ###################################################### Works!

  // Start telnet server
  server.begin();
  server.setNoDelay(true);

  debugServer.begin();
  debugServer.setNoDelay(true);

  // Serial.print("Ready! Use IrScrutinizer to connect to port ");
  // Serial.print(WiFi.localIP());
  // Serial.println(" 4998");

  // Start http server

  httpserver.on("/", handleRoot);
  httpserver.on("/messages", handleMessages);
  httpserver.onNotFound(handleNotFound);
  httpserver.begin();
  // Serial.println("HTTP server started");

  // Send first UDP Discovery Beacon during setup; then do it periodically below
  sendDiscoveryBeacon();
}

String inData;
String lircInData;

void loop() {

  checkOTA();

  // Periodically send UDP Discovery Beacon
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    sendDiscoveryBeacon();
  }

  httpserver.handleClient();

  uint8_t i;

  // Check if there are any new clients
  if (server.hasClient()) {
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      // Find free/disconnected spot
      if (!serverClients[i] || !serverClients[i].connected()) {
        if (serverClients[i]) serverClients[i].stop();
        serverClients[i] = server.available();
        // Serial.print("New client: "); // Serial.println(i);
        continue;
      }
    }
    // No free/disconnected spot so reject
    WiFiClient serverClient = server.available();
    serverClient.stop();
  }

  // Check if there are any new debug clients
  if (debugServer.hasClient()) {
    for (i = 0; i < MAX_DBG_SRV_CLIENTS; i++)
    {
      // Find free/disconnected spot
      if (!debugServerClients[i] || !debugServerClients[i].connected()) {
        if (debugServerClients[i]) debugServerClients[i].stop();
        debugServerClients[i] = debugServer.available();
        // Serial.print("New debug client: "); // Serial.println(i);
        debugSend("Welcome debug client");
        continue;
      }
    }
    // No free/disconnected spot so reject
    WiFiClient debugServerClient = debugServer.available();
    debugServerClient.stop();
  }

  // Check Global Caché clients for data
  for (i = 0; i < MAX_SRV_CLIENTS; i++) {
    if (serverClients[i] && serverClients[i].connected())
    {
      while (serverClients[i].available() > 0)

      {
        char recieved = serverClients[i].read();
        if (recieved != '\n' && recieved != '\r') // Actually we get "\n" from IRScrutinizer
        {
          inData += recieved;
        }
        else
        {
          inData.trim(); // Remove extraneous whitespace; this is important
          // Serial.print("> ");
          // Serial.println(inData);
          debugSend(inData);

          if (inData == "getdevices") {
            send(i, "device,1,3 IR");
            send(i, "endlistdevices");
          }

          if (inData == "get_IRL") {
            send(i, "IR Learner Disabled"); // TODO: Implement capture (once IRremoteESP8266 supports it)
          }

          if (inData == "stop_IRL") {
            send(i, "IR Learner Disabled"); // TODO: Implement capture (once IRremoteESP8266 supports it)
          }

          if (inData == "getversion,0")
            send(i, "1.0");

          if (inData == "VERSION") // LIRC
            lircSend(i, inData, "1.0 implementing http://www.harctoolbox.org/lirc_ccf.html#Extensions+to+the+irsend+command");

          if (inData == "END") // LIRC
            lircSend(i, inData, "");

          if (inData.startsWith("SET_TRANSMITTERS")) // LIRC
            lircSend(i, inData, "");

          if (inData.startsWith("SEND_CCF_ONCE")) // LIRC
          {
            lircSend(i, inData, "");
            inData.replace("SEND_CCF_ONCE 0 ", "");
            ph.convert(inData);
            irsend.sendRaw(ph.convertedRaw, ph.length, ph.frequency);
            debugSend("# Sent Pronto Hex converted to raw --> " + ph.join(ph.convertedRaw, ph.length));
          }

          if (inData == "LIST") // LIRC
            lircSend(i, inData, "MyAwesomeRemote");

          if (inData == "LIST MyAwesomeRemote") // LIRC
            lircSend(i, inData, "MyAwesomeCommand");

          if (inData.startsWith("SEND_ONCE MyAwesomeRemote MyAwesomeCommand")) // LIRC
            lircSend(i, inData, "TODO: Implement actual sending here!"); // TODO: Implement actual sending here!

          // To see what gets sent to the device, enable "Verbose" in the "Options" menu of IRScrutinizer

          if (inData.startsWith("sendir,"))
          {
            int numberOfStringParts = getNumberOfDelimiters(inData, ',');
            int numberOfIrElements = numberOfStringParts - 5;

            int Array[numberOfStringParts];
            char *tmp;
            int z = 0;
            tmp = strtok(&inData[0], ",");
            while (tmp) {
              Array[z++] = atoi(tmp);
              tmp = strtok(NULL, ",");
            }

            // Calculate the number of array elements that are not part of the repeat sequence
            int numberOfNonrepeatIrElements = numberOfIrElements;
            if ( Array[4] > 5 ) numberOfNonrepeatIrElements = Array[5] - 1; // If repeat is >1, then Offset minus 1; TODO: Correct this. Using >5 for iRule Samsung TV codes. Check this
            // Serial.println("numberOfNonrepeatIrElements: ");
            // Serial.println(numberOfNonrepeatIrElements);

            // Construct an array that holds all elements that are not part of the repeat sequence
            unsigned int RawArray[numberOfNonrepeatIrElements];
            for (int iii = 0; iii < numberOfNonrepeatIrElements; iii++)
            {
              RawArray[iii] = ((1000000 / Array[3]) * Array[6 + iii]) + 0.5 ;
              // Serial.print(RawArray[iii]);
              // Serial.print(" ");
            }

            // Calculate the frequency in KHz so that it can be passed to irsend.sendRaw()
            unsigned int freq = (Array[3] / 1000) + 0.5;
            // Serial.println("freq: ");
            // Serial.println(freq);

            // Serial.println("Sending");
            irsend.sendRaw(RawArray, numberOfNonrepeatIrElements, freq);
            send(i, "completeir,1:1," + String(Array[2], DEC));
            debugSend("# Sent Global Caché converted to raw --> " + ph.join(RawArray, numberOfNonrepeatIrElements));
          }
          inData = ""; // Clear recieved buffer
        }
      }
    }
  }
}

void send(int client, String str)
{
  if (serverClients[client] && serverClients[client].connected()) {
    serverClients[client].print(str + "\r");
    // Serial.print("< ");
    // Serial.println(str + "\n");
    delay(1);
  }
}

// lircd's reply packets are described at http://www.lirc.org/html/technical.html
void lircSend(int client, String command, String data) // LIRC
{
  // Determine the number of lines to be sent
  int numberOfLines;
  if (data == "")
  {
    numberOfLines = 0;
  }
  else
  {
    numberOfLines = getNumberOfDelimiters(data, '\n') + getNumberOfDelimiters(data, '\r') + 1; // Assuming new lines are done with \n or \r
  }
  send(client, "BEGIN");
  send(client, command);
  send(client, "SUCCESS");
  send(client, "DATA");
  send(client, String(numberOfLines));
  if (data != "") send(client, data);
  send(client, "END");
}

void debugSend(String str)
{
  int client = 0;
  if (debugServerClients[client] && debugServerClients[client].connected()) {
    debugServerClients[client].print(str + "\r\n");
    delay(1);
  }
}


// Count the number of times separator character appears in the text
int getNumberOfDelimiters(String data, char delimiter)
{
  int stringData = 0;        //variable to count data part nr
  String dataPart = "";      //variable to hole the return text
  for (int i = 0; i < data.length() - 1; i++) { //Walk through the text one letter at a time
    if (data[i] == delimiter) {
      stringData++;
    }
  }
  return stringData;
}

// The Discovery Beacon is a UDP packet sent to
// the multicast IP address 239.255.250.250 on UDP port number 9131
// To check its correctness, use the AmxBeaconListener that is built into IrScrutinizer
// The Beacon message must include Device-SDKClass, Device-Make, and Device-Model.
// For IP controlled devices Device-UUID is added for identification purposes.
void sendDiscoveryBeacon()
{
  IPAddress ipMulti(239, 255, 250, 250); // 239.255.250.250
  unsigned int portMulti = 9131;      // local port to listen on
  // Serial.println("Sending UDP Discovery Beacon");
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String clientName = macToStr(mac);
  clientName.replace(":", "");
  Udp.beginPacket(ipMulti, portMulti);
  Udp.print("AMXB<-UUID=WF2IR_");
  Udp.print(clientName);
  Udp.print("><-SDKClass=Utility><-Make=GlobalCache><-Model=WF2IR><-Config-URL=http://");
  Udp.print(WiFi.localIP());
  Udp.print("><-Status=Ready>\r");
  // AMX beacons needs to be terminated by a carriage return (‘\r’, 0x0D)
  Udp.endPacket();
}

// Convert the MAC address to a String
// This kind of stuff is why I dislike C. Taken from https://gist.github.com/igrr/7f7e7973366fc01d6393
String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

void handleRoot() {
  httpserver.send(200, "text/plain", "WF2IR on port 4998 and LIRC server on port 4998 and debug telnet server on port 22");
}


void handleMessages() {
  String code = httpserver.arg("plain");
  ph.convert(code);
  irsend.sendRaw(ph.convertedRaw, ph.length, ph.frequency);
  debugSend(code);
  // String s = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/javascript\r\n\r\n";
  httpserver.send(200, "text/plain", "OK");
  delay(1);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += httpserver.uri();
  message += "\nMethod: ";
  message += (httpserver.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += httpserver.args();
  message += "\n";
  for (uint8_t i = 0; i < httpserver.args(); i++) {
    message += " " + httpserver.argName(i) + ": " + httpserver.arg(i) + "\n";
  }
  httpserver.send(404, "text/plain", message);
}

void initOTA() {
  MDNS.addService("arduino", "tcp", ota_port);
  OTA.begin(ota_port);
}

void checkOTA() {
  if (OTA.parsePacket()) {
    IPAddress remote = OTA.remoteIP();
    int cmd  = OTA.parseInt();
    int port = OTA.parseInt();
    int size   = OTA.parseInt();
    Serial.printf("Update Start: %d\n", size);
    if (!Update.begin(size)) {
      Update.printError(Serial);
      return;
    }
    WiFiClient client;
    if (client.connect(remote, port)) {
      Serial.setDebugOutput(true);
      uint32_t written;
      while (!Update.isFinished()) {
        written = Update.write(client);
        if (written > 0) client.print(written, DEC);
      }
      Serial.setDebugOutput(false);
      if (Update.end()) {
        client.print("OK");
        Serial.printf("Update Success\n");
        system_restart();
      } else {
        Update.printError(client);
        Update.printError(Serial);
      }
    } else {
      Serial.printf("Connect Failed\n");
    }
  }
}
