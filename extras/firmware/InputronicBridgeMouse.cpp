#include "InputronicBridge.h"

void InputronicBridge::onMouseButtons(hid_mouse_report_t report, uint8_t /*lastButtons*/) {
  updateMouseReport(report);
  updateLastHidRaw(reinterpret_cast<uint8_t *>(&report), sizeof(report));
  sendMouseReport();
}

void InputronicBridge::onMouseMove(hid_mouse_report_t report) {
  updateMouseReport(report);
  updateLastHidRaw(reinterpret_cast<uint8_t *>(&report), sizeof(report));
  sendMouseReport();
}

void InputronicBridge::updateMouseReport(const hid_mouse_report_t &report) {
  latestReports.mouse.x = report.x;
  latestReports.mouse.y = report.y;
  latestReports.mouse.scroll = report.wheel;
  latestReports.mouse.btnLeft = report.buttons & MOUSE_BUTTON_LEFT;
  latestReports.mouse.btnRight = report.buttons & MOUSE_BUTTON_RIGHT;
  latestReports.mouse.btnMiddle = report.buttons & MOUSE_BUTTON_MIDDLE;
  latestReports.mouse.btnBackward = report.buttons & MOUSE_BUTTON_BACKWARD;
  latestReports.mouse.btnForward = report.buttons & MOUSE_BUTTON_FORWARD;
}

void InputronicBridge::sendMouseReport() {
  switch (currentProtocol) {
    case protocolUart: sendMouseUart(); break;
    case protocolI2c: sendMouseI2c(); break;
    case protocolSpi: sendMouseSpi(); break;
  }
}

void InputronicBridge::sendMouseUart() {
  String msg = String("TS;M;") + latestReports.mouse.x + ";" + latestReports.mouse.y + ";" +
               latestReports.mouse.scroll + ";" +
               latestReports.mouse.btnLeft + ";" +
               latestReports.mouse.btnRight + ";" +
               latestReports.mouse.btnMiddle + ";" +
               latestReports.mouse.btnBackward + ";" +
               latestReports.mouse.btnForward + ";TE\n";
  Serial.print(msg);
}

void InputronicBridge::sendMouseI2c() {
  if (currentProtocol != protocolI2c) {
    return;
  }
  lastI2cMsg = String("TS;M;") + latestReports.mouse.x + ";" + latestReports.mouse.y + ";" +
               latestReports.mouse.scroll + ";" +
               latestReports.mouse.btnLeft + ";" +
               latestReports.mouse.btnRight + ";" +
               latestReports.mouse.btnMiddle + ";" +
               latestReports.mouse.btnBackward + ";" +
               latestReports.mouse.btnForward + ";TE";
  i2cMsgPending = true;
  i2cMsgSent = false;
}

void InputronicBridge::sendMouseSpi() {
  if (currentProtocol != protocolSpi) {
    return;
  }
  lastSpiMsg = String("TS;M;") + latestReports.mouse.x + ";" + latestReports.mouse.y + ";" +
               latestReports.mouse.scroll + ";" +
               latestReports.mouse.btnLeft + ";" +
               latestReports.mouse.btnRight + ";" +
               latestReports.mouse.btnMiddle + ";" +
               latestReports.mouse.btnBackward + ";" +
               latestReports.mouse.btnForward + ";TE";
  spiMsgPending = true;
}
