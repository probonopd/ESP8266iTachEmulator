/*
  Implements a subset of the iTach API Specification Version 1.5
  http://www.globalcache.com/files/docs/API-iTach.pdf
  and
  LIRC server including the SEND_CCF_ONCE extensions
  http://www.harctoolbox.org/lirc_ccf.html#Extensions+to+the+irsend+command
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
  Circuit:
  To get good range, attach a resistor to pin 12 of the ESP-12E and connect the resistor to the G pin of a 2N7000 transistor.
  If you look at the flat side of the 2N7000 you have S, G, D pins.
  Connect S to GND, G to the resistor to the MCU, and D to the IR LED short pin.
  The long pin of the IR LED is connected to +3.3V.
  I picked the 2N7000 because unlike others it will not pull pin 2 down which would prevent the chip from booting.
*/

#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <RCSwitch.h>
#include <ProntoHex.h>

// For ESP Manager
#include <FS.h> //  Settings saved to SPIFFS
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h> // required for settings file to make it readable
#include <Hash.h>
#include <ESPmanager.h>

AsyncWebServer HTTP(80);
ESPmanager settings(HTTP, SPIFFS);

// GPIO12 = 12 = labelled "D6" on the NodeMCU board
const int infraredLedPin = 12; // ############# CHECK IF THE LED OR TRANSISTOR (RECOMMENDED) IS ACTUALLY ATTACHED TO THIS PIN

extern const String MYVAL;

WiFiClient Telnet;

IRsend irsend(infraredLedPin);

unsigned int irrawCodes[99];
int rawCodeLen; // The length of the code
int freq;

RCSwitch mySwitch = RCSwitch();

ProntoHex ph = ProntoHex();

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

int RECV_PIN = D1; // an IR detector/demodulatord
IRrecv irrecv(RECV_PIN);
decode_results results;

void setup() {

  irsend.begin();
  irrecv.enableIRIn(); // Start the receiver
  mySwitch.enableTransmit(3); // Pin 3 is RXD; 433 MHz

  ////////////

  Serial.begin(115200);
  SPIFFS.begin();

  Serial.println("");
  Serial.println(F("Example ESPconfig - using ESPAsyncWebServer"));

  Serial.printf("Sketch size: %u\n", ESP.getSketchSize());
  Serial.printf("Free size: %u\n", ESP.getFreeSketchSpace());

  settings.begin();

  // https://github.com/me-no-dev/ESPAsyncWebServer
  HTTP.on("/", HTTP_ANY, [](AsyncWebServerRequest * request) {
    request->send(200, "text/html", "<html><body><p>WF2IR on port 4998 and LIRC server on port 4998 and debug telnet server on port 22</p><p><a href='/espman/'>Administration</a></p></body></html>");
  });

  HTTP.begin();

  Serial.print(F("Free Heap: "));
  Serial.println(ESP.getFreeHeap());

  ////////////

  // mySwitch.switchOff(4, 2); // ###################################################### Works!

  // Start telnet server
  server.begin();
  server.setNoDelay(true);

  debugServer.begin();
  debugServer.setNoDelay(true);

  Serial.print("Ready! Use IrScrutinizer to connect to port ");
  Serial.print(WiFi.localIP());
  Serial.println(" 4998");

  // Send first UDP Discovery Beacon during setup; then do it periodically below
  sendDiscoveryBeacon();
}

String stringDecode;

int clientToSendReceivedCodeTo = MAX_SRV_CLIENTS;

void dump(decode_results *results) {
  int count = results->rawlen;
  unsigned long freq = 38400;        // FIXME DON'T HARDCODE _________________________________________
  stringDecode = "sendir,1:0,0,";
  stringDecode += freq;
  stringDecode += (",1,1,");
  for (int i = 0; i < count; i++) {
    if (i & 1) {
      stringDecode += results->rawbuf[i] * USECPERTICK * freq / 1000000 - 1, DEC;
    } else
    {
      stringDecode += (unsigned long) results->rawbuf[i] * USECPERTICK * freq / 1000000 - 1, DEC;
    }
    stringDecode += ",";
  }
  stringDecode += "999";
  Serial.println(stringDecode);
  debugSend(stringDecode);

  if (clientToSendReceivedCodeTo != MAX_SRV_CLIENTS) {
    send(clientToSendReceivedCodeTo, stringDecode);
    clientToSendReceivedCodeTo = MAX_SRV_CLIENTS;
  }
}

String inData;
String lircInData;

void loop() {

  settings.handle();

  if (irrecv.decode(&results))
  {
    dump(&results);
    irrecv.resume(); // Receive the next value
  }

  // Periodically send UDP Discovery Beacon
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;
    sendDiscoveryBeacon();
  }

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
            send(i, "IR Learner Enabled");
            clientToSendReceivedCodeTo = i;
            // send(i, "sendir,1:1,4,38400,1,69,347,173,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,65,22,65,22,65,22,65,22,65,22,65,22,65,22,65,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,65,22,65,22,65,22,65,22,65,22,65,22,65,22,65,22,1527,347,87,22,3692");
          }

          if (inData == "stop_IRL") {
            send(i, "IR Learner Disabled");
          }

          if (inData == "getversion")
            send(i, "1.0");
            
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
