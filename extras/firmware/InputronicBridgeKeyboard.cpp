#include "InputronicBridge.h"

void InputronicBridge::onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t lastReport) {
  auto appendToken = [&](const char *token) {
    latestReports.keyboard.payload += token;
  };
  auto wasInLast = [&](uint8_t keycode) -> bool {
    if (keycode == 0) {
      return false;
    }
    for (int j = 0; j < 6; j++) {
      if (lastReport.keycode[j] == keycode) {
        return true;
      }
    }
    return false;
  };
  auto modifierPressed = [&](uint8_t mask) -> bool {
    return (report.modifier & mask) && !(lastReport.modifier & mask);
  };

  latestReports.keyboard.payload = "";

  if (modifierPressed(KEYBOARD_MODIFIER_LEFTCTRL)) {
    appendToken("<LCTRL>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_RIGHTCTRL)) {
    appendToken("<RCTRL>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_LEFTSHIFT)) {
    appendToken("<LSHIFT>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_RIGHTSHIFT)) {
    appendToken("<RSHIFT>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_LEFTALT)) {
    appendToken("<LALT>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_RIGHTALT)) {
    appendToken("<RALT>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_LEFTGUI)) {
    appendToken("<LGUI>");
  }
  if (modifierPressed(KEYBOARD_MODIFIER_RIGHTGUI)) {
    appendToken("<RGUI>");
  }

  bool shift = (report.modifier & KEYBOARD_MODIFIER_LEFTSHIFT) ||
               (report.modifier & KEYBOARD_MODIFIER_RIGHTSHIFT);
  for (int i = 0; i < 6; i++) {
    uint8_t keycode = report.keycode[i];
    if (keycode == 0 || wasInLast(keycode)) {
      continue;
    }
    uint8_t ascii = getKeycodeToAscii(keycode, shift);
    if (' ' <= ascii && ascii <= '~') {
      latestReports.keyboard.payload += static_cast<char>(ascii);
      continue;
    }
    switch (keycode) {
      case HID_KEY_ESCAPE: appendToken("<ESC>"); break;
      case HID_KEY_TAB: appendToken("<TAB>"); break;
      case HID_KEY_BACKSPACE: appendToken("<BS>"); break;
      case HID_KEY_ENTER: appendToken("<ENTER>"); break;
      case HID_KEY_CAPS_LOCK: appendToken("<CAPS>"); break;
      case HID_KEY_PRINT_SCREEN: appendToken("<PRTSCR>"); break;
      case HID_KEY_SCROLL_LOCK: appendToken("<SCROLL>"); break;
      case HID_KEY_PAUSE: appendToken("<PAUSE>"); break;
      case HID_KEY_INSERT: appendToken("<INS>"); break;
      case HID_KEY_HOME: appendToken("<HOME>"); break;
      case HID_KEY_PAGE_UP: appendToken("<PGUP>"); break;
      case HID_KEY_DELETE: appendToken("<DEL>"); break;
      case HID_KEY_END: appendToken("<END>"); break;
      case HID_KEY_PAGE_DOWN: appendToken("<PGDN>"); break;
      case HID_KEY_ARROW_RIGHT: appendToken("<RIGHT>"); break;
      case HID_KEY_ARROW_LEFT: appendToken("<LEFT>"); break;
      case HID_KEY_ARROW_DOWN: appendToken("<DOWN>"); break;
      case HID_KEY_ARROW_UP: appendToken("<UP>"); break;
      case HID_KEY_F1: appendToken("<F1>"); break;
      case HID_KEY_F2: appendToken("<F2>"); break;
      case HID_KEY_F3: appendToken("<F3>"); break;
      case HID_KEY_F4: appendToken("<F4>"); break;
      case HID_KEY_F5: appendToken("<F5>"); break;
      case HID_KEY_F6: appendToken("<F6>"); break;
      case HID_KEY_F7: appendToken("<F7>"); break;
      case HID_KEY_F8: appendToken("<F8>"); break;
      case HID_KEY_F9: appendToken("<F9>"); break;
      case HID_KEY_F10: appendToken("<F10>"); break;
      case HID_KEY_F11: appendToken("<F11>"); break;
      case HID_KEY_F12: appendToken("<F12>"); break;
      default: break;
    }
  }
  if (latestReports.keyboard.payload.length() > 0) {
    updateLastHidRaw(reinterpret_cast<uint8_t *>(&report), sizeof(report));
    sendKeyboardReport();
  }
}

void InputronicBridge::onKeyboardKey(uint8_t /*ascii*/, uint8_t /*keycode*/, uint8_t /*modifier*/) {
}

void InputronicBridge::sendKeyboardReport() {
  switch (currentProtocol) {
    case protocolUart: sendKeyboardUart(); break;
    case protocolI2c: sendKeyboardI2c(); break;
    case protocolSpi: sendKeyboardSpi(); break;
  }
}

void InputronicBridge::sendKeyboardUart() {
  Serial.print("TS;K;" + latestReports.keyboard.payload + ";TE\n");
}

void InputronicBridge::sendKeyboardI2c() {
  if (currentProtocol != protocolI2c) {
    return;
  }
  lastI2cMsg = String("TS;K;") + latestReports.keyboard.payload + ";TE";
  i2cMsgPending = true;
  i2cMsgSent = false;
}

void InputronicBridge::sendKeyboardSpi() {
  if (currentProtocol != protocolSpi) {
    return;
  }
  lastSpiMsg = String("TS;K;") + latestReports.keyboard.payload + ";TE";
  Serial.print("TS;K;" + latestReports.keyboard.payload + ";TE\n");
  spiMsgPending = true;
}
