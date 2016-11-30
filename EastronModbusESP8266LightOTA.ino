#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
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

const char* ssid_n = "sid";
const char* password = "password";
const char* writeUrl = "http://server/writeData";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";

static const int led = 2;

float data[13];

/*!
  We're using a MAX485-compatible RS485 Transceiver.
  Rx/Tx is hooked up to the hardware serial port at 'Serial'.
  The Data Enable and Receiver Enable pins are hooked up as follows:
*/
#define MAX485_DE      14  //D5 //3
#define MAX485_RE_NEG  12 //D6  //2

#define RDELAY 20000

SoftwareSerial mySerial(4, 5); // RX, TX

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

// instantiate ModbusMaster object
ModbusMaster node;

unsigned long u32wait;

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

  // Modbus slave ID 1
  node.begin(1, mySerial);
  // Callbacks allow us to configure the RS485 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  u32wait = millis() + 1000;
}


int currentParamIdx = 0;
int codeIdx = 0;
bool doRequest = false;

void loop() {
  if (millis() > u32wait) {
    if (mySerial.available()) {
      doRequest = true;
    }
    else {
      Serial.println("SoftSerial is not available");
      u32wait = millis() + RDELAY;
    }
  }

  if (doRequest && currentParamIdx < 3) {
    int codes_p0_37[6] = {0, 6, 12, 18, 24, 30};
    int codes_p70_80[5] = {70, 72, 74, 76, 78};
    int codes_p342_344[2] = {342, 344};

    uint8_t res;
    if (currentParamIdx == 0) {
      int csize = sizeof(codes_p0_37) / sizeof(int);
      int len = codes_p0_37[csize - 1] - codes_p0_37[0];
      res = node.readInputRegisters(codes_p0_37[0], len * 2);
    }
    if (currentParamIdx == 1) {
      int csize = sizeof(codes_p70_80) / sizeof(int);
      int len = codes_p70_80[csize - 1] - codes_p70_80[0];
      res = node.readInputRegisters(codes_p70_80[0], len * 2);
    }
    if (currentParamIdx == 2) {
      int csize = sizeof(codes_p342_344) / sizeof(int);
      int len = codes_p342_344[csize - 1] - codes_p342_344[0];
      res = node.readInputRegisters(codes_p342_344[0], len * 2);
    }
    if (res == node.ku8MBSuccess) {
      if (currentParamIdx == 0) {
        populateValues(codes_p0_37, sizeof(codes_p0_37) / sizeof(int));
      }
      if (currentParamIdx == 1) {
        populateValues(codes_p70_80, sizeof(codes_p70_80) / sizeof(int));
      }
      if (currentParamIdx == 2) {
        populateValues(codes_p342_344, sizeof(codes_p342_344) / sizeof(int));
      }
      currentParamIdx++;
    }
  }
  else {
    httpServer.handleClient();
  }

  if (currentParamIdx == 3) {
    /*
      for (int i = 0; i < 13; i++) {
      Serial.print(data[i]); Serial.print(",");
      }
      Serial.println("");
    */
    currentParamIdx = 0;
    codeIdx = 0;
    doRequest = false;
    sendData();
    u32wait = millis() + RDELAY;
  }
}

void populateValues(int *codes, int csize) {
  union {
    float f;
    uint16_t b[2];
  } u;
  /*
    for (int i = 0; i < 38; i++) {
      Serial.print(node.getResponseBuffer(i)); Serial.print(",");
    }
  */
  int prevCode = -1;
  int pos = 0;
  for (int i = 0; i < csize; i++) {
    int c = codes[i];

    if (prevCode >= 0) {
      pos = pos + (c - prevCode);
    }
    /*
      Serial.println("-------");
      Serial.print("Code: "); Serial.println(c);
      Serial.print("Pos: "); Serial.println(pos);
    */
    u.b[1] = node.getResponseBuffer(pos);
    u.b[0] = node.getResponseBuffer(pos + 1);
    float v = u.f;
    data[codeIdx] = v;
    codeIdx++;
    //Serial.println(u.f, 3);
    prevCode = c;
  }

}


void sendData() {
  //Serial.println("sendData");
  HTTPClient http;
  //Serial.println("http.begin");
  http.begin(writeUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  //Serial.println("adding params");
  String params = "params=";
  params += "{";
  for (int i = 0; i < CODES_SIZE; i++) {
    char vstr[10];
    dtostrf(data[i], 10, 6, vstr);
    params += "\"";
    params += codes[i].code;
    params += "\":";
    params += vstr;
    if (i < CODES_SIZE - 1) {
      params += ",";
    }
  }
  params += "}";
  // Serial.println(params);
  int httpCode = http.POST(params);

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    /*
      String payload = http.getString();
      Serial.println(payload);
    */
  }
  else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
