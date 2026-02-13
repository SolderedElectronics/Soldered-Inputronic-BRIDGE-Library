#include "InputronicBridge.h"

InputronicBridge &usbHost = InputronicBridge::instance();

void setup() {
  Serial.begin(115200);
  usbHost.setProtocol(InputronicBridge::protocolSpi);
  delay(500);
  usbHost.begin();
}

void loop() {
  usbHost.task();
}
