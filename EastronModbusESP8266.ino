#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ModbusMaster.h>
#include <SoftwareSerial.h>


typedef struct {
  int code;
  char* label;
  char* uom;
} Code;


static const Code codes[] = {
  {0x00, "Voltage", "V"},
  {0x06, "Current", "A"},
  {0x0C, "Active Power", "W"},
  {0x12, "Apparent Power", "VA"},
  {0x18, "Reactive Power", "VAr"},
  {0x1e, "Power Factor", ""},
  {0x46, "Frequency", "Hz"},
  {0x48, "Import active energy", "kwh"},
  {0x4a, "Export active energy", "kwh"},
  {0x4c, "Import reactive energy", "kvarh"},
  {0x4e, "Export reactive energy", "kvarh"},
  {0x56, "Total active energy", "kwh"},
  {0x58, "Total reactive energy", "kvarh"}
};

#define CODES_SIZE sizeof codes / sizeof codes[0]

enum {
  IDLE_STATE = 0,
  DO_REQUEST_STATE = 1,
  RECEIVING_STATE = 2,
  DATA_READY_STATE = 3
};

const char* ssid_n = "NETGEAR";
const char* password = "<password>";
const char* contentTplStart = "<html><title>Electricity supply meter</title><body>";
const char* contentTplEnd = "</body></html>";

static const int led = 2;

float data[13];

bool debug = true;
bool debug_detailed = debug && false;

/*!
  We're using a MAX485-compatible RS485 Transceiver.
  Rx/Tx is hooked up to the hardware serial port at 'Serial'.
  The Data Enable and Receiver Enable pins are hooked up as follows:
*/
#define MAX485_DE      14  //D5 //3
#define MAX485_RE_NEG  12 //D6  //2

#define RDELAY 60000

SoftwareSerial mySerial(4, 5); // RX, TX

// instantiate ModbusMaster object
ModbusMaster node;
ESP8266WebServer server(80);


unsigned long u32wait;
uint8_t u8RequestState;


void preTransmission() {
  digitalWrite(MAX485_RE_NEG, 1);
  digitalWrite(MAX485_DE, 1);

  digitalWrite(led, 0);

}

void postTransmission() {
  digitalWrite(MAX485_RE_NEG, 0);
  digitalWrite(MAX485_DE, 0);

  digitalWrite(led, 1);
}

void handleRoot() {
  server.sendContent(contentTplStart);
  server.sendContent("<table><tr><th>Parameter</th><th>Value</th><th>Unit</th></tr>");
  for (int i = 0; i < CODES_SIZE; i++) {
      char val_str[6];
    dtostrf(data[i], 9, 6, val_str);
    
    server.sendContent("<tr>");
    server.sendContent("<td>");
    server.sendContent(codes[i].label);
    server.sendContent("</td><td>");
    server.sendContent(val_str);
    server.sendContent("</td><td>");
    server.sendContent(codes[i].uom);
    server.sendContent("</td></tr>");
  }
  server.sendContent("</table>");
  server.sendContent(contentTplEnd);
}

void handleNotFound() {
  digitalWrite(led, 0);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 1);
}

void setupWiFi() {
  WiFi.begin(ssid_n, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupHTTPServer() {
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void setup() {
  pinMode(led, OUTPUT);

  pinMode(MAX485_RE_NEG, OUTPUT);
  pinMode(MAX485_DE, OUTPUT);
  // Init in receive mode
  digitalWrite(MAX485_RE_NEG, 0);
  digitalWrite(MAX485_DE, 0);

  // Modbus communication runs at 115200 baud
  Serial.begin(115200);
  mySerial.begin(9600);

  setupWiFi();
  setupHTTPServer();

  // Modbus slave ID 1
  node.begin(1, mySerial);
  // Callbacks allow us to configure the RS485 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  u32wait = millis() + 1000;
  u8RequestState = IDLE_STATE;
}

bool state = true;
int currentParamIdx = 0;

void loop() {

  switch (u8RequestState) {
    case IDLE_STATE:
      if (millis() > u32wait) {
        u8RequestState = DO_REQUEST_STATE;
      }
      else {
        server.handleClient();
      }
      break;
    case DO_REQUEST_STATE:
      // Toggle the coil at address 0x0002 (Manual Load Control)
      node.writeSingleCoil(0x0002, state);
      state = !state;
      u8RequestState = RECEIVING_STATE;
      break;

    case RECEIVING_STATE: {
        if (debug_detailed) {
          Serial.println(codes[currentParamIdx].label);
        }
        ESP.wdtDisable();
        uint8_t res = node.readInputRegisters(codes[currentParamIdx].code, 2);
        if (res == node.ku8MBSuccess) {
          u8RequestState = DATA_READY_STATE;
          ESP.wdtEnable(10);
        }
      }
      break;

    case DATA_READY_STATE: {
        float v = getValue();
        data[currentParamIdx] = v;

        currentParamIdx++;
        u8RequestState = DO_REQUEST_STATE;
        if (currentParamIdx == CODES_SIZE) {
          if (debug) {
            Serial.println("---------------------------------------------");
            for (int i = 0; i < currentParamIdx; i++) {
              Serial.print(codes[i].label);
              Serial.print(": ");
              Serial.print(data[i], 3);
              Serial.print(" ");
              Serial.println(codes[i].uom);
            }
            Serial.println("---------------------------------------------");
          }
          currentParamIdx = 0;
          u8RequestState = IDLE_STATE;
          u32wait = millis() + RDELAY;
        }
      }
      break;
  }
}

float getValue() {

  uint16_t arr[2];
  for (int i = 0; i < 2; i++) {
    uint16_t v = node.getResponseBuffer(i);
    if (debug_detailed) {
      Serial.println(v);
    }
    int a = 1;
    if (i == 1) {
      a = 0;
    }
    arr[a] = v;
  }
  uint32_t *val = (uint32_t *)arr;
  return convert(*val);
}

float convert(uint32_t x) {
  float y = *(float*)&x;
  return y;
}



