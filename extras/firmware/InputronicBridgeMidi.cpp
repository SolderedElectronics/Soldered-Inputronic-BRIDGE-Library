/**
 **************************************************
 *
 * @file        InputronicBridgeMidi.cpp
 * @brief       MIDI USB event handling for the Inputronic BRIDGE firmware.
 *              Detects MIDI streaming interfaces in the USB config descriptor,
 *              allocates bulk IN/OUT transfers, and forwards MIDI events to the
 *              active transport as TS;MIDI;b1;b2;b3|...;TE frames.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "InputronicBridge.h"

/**
 * @brief                   sendMidiReport function formats a three-byte MIDI
 *                          event as a TS;MIDI;HH;HH;HH;TE frame and routes it
 *                          to the active transport.
 */
void InputronicBridge::sendMidiReport(uint8_t b1, uint8_t b2, uint8_t b3) {
  static char txBuf[40];
  snprintf(txBuf, sizeof(txBuf), "TS;MIDI;%02X;%02X;%02X;TE", b1, b2, b3);
  if (currentProtocol == protocolI2c) {
    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      enqueueI2cChannelAMessageLocked(String(txBuf), false);
      xSemaphoreGive(msgMutex);
    }
  } else if (currentProtocol == protocolSpi) {
    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      lastSpiMsg = String(txBuf);
      spiMsgPending = true;
      xSemaphoreGive(msgMutex);
    }
  } else if (currentProtocol == protocolUart) {
    Serial.print(txBuf);
    Serial.print('\n');
    pulseInterruptPin();
  }
}

/**
 * @brief                   midiTransferCallback function is the USB transfer
 *                          completion callback for MIDI bulk IN endpoints.
 *                          It iterates the 4-byte USB MIDI packets in the
 *                          transfer buffer, assembles all events in one
 *                          TS;MIDI;b1;b2;b3|...;TE frame, routes the frame to
 *                          the active transport, and resubmits the transfer.
 */
void InputronicBridge::midiTransferCallback(usb_transfer_t *transfer) {
  auto &self = InputronicBridge::instance();

  // Snapshot deviceHandle under a critical section to avoid a race with the
  // device-gone event callback that may write deviceHandle concurrently.
  usb_device_handle_t localHandle;
  {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    localHandle = self.deviceHandle;
    portEXIT_CRITICAL(&mux);
  }

  if (localHandle == transfer->device_handle) {
    int inXfer = transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK;
    if ((transfer->status == 0) && inXfer) {
      uint8_t *const p = transfer->data_buffer;

      // Accumulate all events from this USB transfer into one packet.
      // Format: TS;MIDI;b1;b2;b3|b1;b2;b3|...;TE  (events separated by '|')
      static char packetBuf[256];
      int offset = snprintf(packetBuf, sizeof(packetBuf), "TS;MIDI;");
      bool hasEvents = false;

      for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
        if (i + 3 >= transfer->actual_num_bytes) {
          break;
        }
        if ((p[i] + p[i + 1] + p[i + 2] + p[i + 3]) == 0) {
          break;
        }
        uint8_t b1 = p[i + 1];
        uint8_t b2 = p[i + 2];
        uint8_t b3 = p[i + 3];

        if (hasEvents) {
          if (offset < (int)sizeof(packetBuf) - 1) {
            packetBuf[offset++] = '|';
          }
        }
        int written = snprintf(packetBuf + offset, sizeof(packetBuf) - offset,
                               "%02X;%02X;%02X", b1, b2, b3);
        if (written > 0 && offset + written < (int)sizeof(packetBuf)) {
          offset += written;
        }
        hasEvents = true;
      }

      if (hasEvents) {
        snprintf(packetBuf + offset, sizeof(packetBuf) - offset, ";TE");
        Serial.print(packetBuf);
        Serial.print('\n');

        if (self.currentProtocol == protocolI2c) {
          if (self.msgMutex && xSemaphoreTake(self.msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            self.enqueueI2cChannelAMessageLocked(String(packetBuf), false);
            xSemaphoreGive(self.msgMutex);
          }
        } else if (self.currentProtocol == protocolSpi) {
          if (self.msgMutex && xSemaphoreTake(self.msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            self.lastSpiMsg = String(packetBuf);
            self.spiMsgPending = true;
            xSemaphoreGive(self.msgMutex);
          }
        } else if (self.currentProtocol == protocolUart) {
          self.pulseInterruptPin();
        }
      }

      esp_err_t err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK) {
      }
    }
  }
}

/**
 * @brief                   checkInterfaceDescMidi function inspects a USB
 *                          interface descriptor and sets the isMidi flag when
 *                          the interface matches AudioControl subclass 0x03
 *                          (MIDI Streaming) with protocol 0x00.
 */
void InputronicBridge::checkInterfaceDescMidi(const void *p) {
  if (!p) {
    return;
  }
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
  if ((intf->bInterfaceSubClass == 0x03) && (intf->bInterfaceProtocol == 0x00)) {
    isMidi = true;
  }
}

/**
 * @brief                   prepareEndpoints function allocates USB host
 *                          transfers for a MIDI bulk endpoint and immediately
 *                          submits the IN transfers to begin receiving MIDI
 *                          data, then sets isMidiReady when both IN and OUT
 *                          endpoints are configured.
 */
void InputronicBridge::prepareEndpoints(const void *p) {
  if (!p) {
    return;
  }
  const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;

  if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
    return;
  }

  esp_err_t err;

  if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
    for (int i = 0; i < static_cast<int>(midiInBuffers); i++) {
      err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &midiIn[i]);
      if (err != ESP_OK) {
        midiIn[i] = nullptr;
      } else {
        midiIn[i]->device_handle = deviceHandle;
        midiIn[i]->bEndpointAddress = endpoint->bEndpointAddress;
        midiIn[i]->callback = midiTransferCallback;
        midiIn[i]->context = (void *)i;
        midiIn[i]->num_bytes = endpoint->wMaxPacketSize;
        esp_err_t err2 = usb_host_transfer_submit(midiIn[i]);
        if (err2 != ESP_OK) {
        }
      }
    }
  } else {
    err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &midiOut);
    if (err != ESP_OK) {
      midiOut = nullptr;
    } else {
      midiOut->device_handle = deviceHandle;
      midiOut->bEndpointAddress = endpoint->bEndpointAddress;
      midiOut->callback = midiTransferCallback;
      midiOut->context = nullptr;
      midiOut->num_bytes = endpoint->wMaxPacketSize;
    }
  }

  isMidiReady = ((midiOut != nullptr) && (midiIn[0] != nullptr));
}
