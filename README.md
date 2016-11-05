# SDM220-Modbus-RTU-ESP8266
Simple ModBus master for Electricity supply meter Eastron SDM220Modbus and HTTP server implementation on ESP8266.
ESP8266 and SDM220 are communicating using Modbus RTU via RS485.

SoftSerial is used on GPIO4 and GPIO5 (RX/TX) because hardware RX/TX pins are not working for some reason on ESP8266.

Web server on ESP8266 is not responding while it is fetching data from SDM220.