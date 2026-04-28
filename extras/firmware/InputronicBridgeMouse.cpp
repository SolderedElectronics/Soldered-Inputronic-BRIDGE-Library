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
  latestReports.mouse.btnScrollWheel = report.buttons & MOUSE_BUTTON_MIDDLE;
}

void InputronicBridge::sendMouseReport() {
  switch (currentProtocol) {
    case protocolUart: sendMouseUart(); break;
    case protocolI2c: sendMouseI2c(); break;
    case protocolSpi: sendMouseSpi(); break;
  }
}

void InputronicBridge::sendMouseUart() {
  static char txBuf[128];
  snprintf(txBuf, sizeof(txBuf), "TS;M;%d;%d;%d;%d;%d;%d;%d;%d;%d;TE",
           (int)latestReports.mouse.x,
           (int)latestReports.mouse.y,
           (int)latestReports.mouse.scroll,
           (int)latestReports.mouse.btnLeft,
           (int)latestReports.mouse.btnRight,
           (int)latestReports.mouse.btnMiddle,
           (int)latestReports.mouse.btnBackward,
           (int)latestReports.mouse.btnForward,
           (int)latestReports.mouse.btnScrollWheel);
  Serial.print(txBuf);
  Serial.print('\n');
  pulseInterruptPin();
}

void InputronicBridge::sendMouseI2c() {
  static char txBuf[128];
  snprintf(txBuf, sizeof(txBuf), "TS;M;%d;%d;%d;%d;%d;%d;%d;%d;%d;TE",
           (int)latestReports.mouse.x,
           (int)latestReports.mouse.y,
           (int)latestReports.mouse.scroll,
           (int)latestReports.mouse.btnLeft,
           (int)latestReports.mouse.btnRight,
           (int)latestReports.mouse.btnMiddle,
           (int)latestReports.mouse.btnBackward,
           (int)latestReports.mouse.btnForward,
           (int)latestReports.mouse.btnScrollWheel);
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    lastI2cMsg = String(txBuf);
    i2cMsgPending = true;
    if (!i2cSentHidRaw) i2cMsgSent = false;
    xSemaphoreGive(msgMutex);
  }
}

void InputronicBridge::sendMouseSpi() {
  static char txBuf[128];
  snprintf(txBuf, sizeof(txBuf), "TS;M;%d;%d;%d;%d;%d;%d;%d;%d;%d;TE",
           (int)latestReports.mouse.x,
           (int)latestReports.mouse.y,
           (int)latestReports.mouse.scroll,
           (int)latestReports.mouse.btnLeft,
           (int)latestReports.mouse.btnRight,
           (int)latestReports.mouse.btnMiddle,
           (int)latestReports.mouse.btnBackward,
           (int)latestReports.mouse.btnForward,
           (int)latestReports.mouse.btnScrollWheel);
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    lastSpiMsg = String(txBuf);
    spiMsgPending = true;
    xSemaphoreGive(msgMutex);
  }
}
