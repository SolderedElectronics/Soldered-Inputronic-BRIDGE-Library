/**
 **************************************************
 *
 * @file        InputronicBridgeMouse.cpp
 * @brief       Mouse HID event handling for the Inputronic BRIDGE firmware.
 *              Receives USB HID mouse reports from EspUsbHost, maps them to
 *              the internal MouseReport cache, and routes formatted packets to
 *              the active transport (UART, I2C, or SPI).
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "InputronicBridge.h"

/**
 * @brief                   onMouseButtons function handles HID mouse reports
 *                          that contain a button state change. It updates the
 *                          internal mouse cache and forwards a new report to
 *                          the active transport.
 */
void InputronicBridge::onMouseButtons(hid_mouse_report_t report, uint8_t /*lastButtons*/) {
  updateMouseReport(report);
  updateLastHidRaw(reinterpret_cast<uint8_t *>(&report), sizeof(report));
  sendMouseReport();
}

/**
 * @brief                   onMouseMove function handles HID mouse reports that
 *                          contain position or scroll wheel changes. It updates
 *                          the internal mouse cache and forwards a new report
 *                          to the active transport.
 */
void InputronicBridge::onMouseMove(hid_mouse_report_t report) {
  updateMouseReport(report);
  updateLastHidRaw(reinterpret_cast<uint8_t *>(&report), sizeof(report));
  sendMouseReport();
}

/**
 * @brief                   updateMouseReport function copies position, scroll,
 *                          and all button fields from a HID report into the
 *                          latestReports.mouse cache.
 */
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

/**
 * @brief                   sendMouseReport function dispatches the current
 *                          mouse state to whichever transport is active.
 */
void InputronicBridge::sendMouseReport() {
  switch (currentProtocol) {
    case protocolUart: sendMouseUart(); break;
    case protocolI2c: sendMouseI2c(); break;
    case protocolSpi: sendMouseSpi(); break;
  }
}

/**
 * @brief                   sendMouseUart function formats the current mouse
 *                          state as a TS;M;...;TE frame, transmits it over
 *                          UART, and pulses the interrupt pin.
 */
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

/**
 * @brief                   sendMouseI2c function formats the current mouse
 *                          state as a TS;M;...;TE frame and enqueues it on
 *                          the I2C Channel A queue. Button state changes and
 *                          non-zero scroll deltas are marked critical so they
 *                          cannot be coalesced away by subsequent movement
 *                          updates, ensuring reliable click and scroll delivery.
 */
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
    uint8_t buttonsMask = 0;
    if (latestReports.mouse.btnLeft) buttonsMask |= 0x01;
    if (latestReports.mouse.btnRight) buttonsMask |= 0x02;
    if (latestReports.mouse.btnMiddle) buttonsMask |= 0x04;
    if (latestReports.mouse.btnBackward) buttonsMask |= 0x08;
    if (latestReports.mouse.btnForward) buttonsMask |= 0x10;
    if (latestReports.mouse.btnScrollWheel) buttonsMask |= 0x20;

    // Only coalesce pure move updates. Scroll deltas and button transitions
    // are edge-sensitive and must not be merged away.
    bool buttonStateChanged = !haveLastQueuedMouseState || (buttonsMask != lastQueuedMouseButtonsMask);
    bool hasScrollDelta = (latestReports.mouse.scroll != 0);
    bool allowMouseCoalesce = !buttonStateChanged && !hasScrollDelta;
    bool isCritical = buttonStateChanged || hasScrollDelta;

    enqueueI2cChannelAMessageLocked(String(txBuf), true, allowMouseCoalesce, isCritical);
    lastQueuedMouseButtonsMask = buttonsMask;
    haveLastQueuedMouseState = true;
    xSemaphoreGive(msgMutex);
  }
}

/**
 * @brief                   sendMouseSpi function formats the current mouse
 *                          state as a TS;M;...;TE frame and stores it as the
 *                          next SPI outbound message, replacing any unsent
 *                          previous mouse packet.
 */
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
