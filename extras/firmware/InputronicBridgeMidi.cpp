#include "InputronicBridge.h"

void InputronicBridge::sendMidiReport(uint8_t b1, uint8_t b2, uint8_t b3) {
  char buf[40];
  snprintf(buf, sizeof(buf), "TS;MIDI;%02X;%02X;%02X;TE", b1, b2, b3);
  if (currentProtocol == protocolI2c) {
    lastI2cMsg = buf;
    i2cMsgPending = true;
    i2cMsgSent = false;
  } else if (currentProtocol == protocolSpi) {
    lastSpiMsg = buf;
    spiMsgPending = true;
  } else if (currentProtocol == protocolUart) {
    pulseInterruptPin();
  }
}

void InputronicBridge::midiTransferCallback(usb_transfer_t *transfer) {
  auto &self = InputronicBridge::instance();
  if (self.deviceHandle == transfer->device_handle) {
    int inXfer = transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK;
    if ((transfer->status == 0) && inXfer) {
      uint8_t *const p = transfer->data_buffer;
      for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
        if ((p[i] + p[i + 1] + p[i + 2] + p[i + 3]) == 0) {
          break;
        }
        uint8_t b1 = p[i + 1];
        uint8_t b2 = p[i + 2];
        uint8_t b3 = p[i + 3];

        Serial.printf("TS;MIDI;%02X;%02X;%02X;TE", b1, b2, b3);

        self.sendMidiReport(b1, b2, b3);
      }

      esp_err_t err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK) {
      }
    }
  }
}

void InputronicBridge::checkInterfaceDescMidi(const void *p) {
  if (!p) {
    return;
  }
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
  if ((intf->bInterfaceSubClass == 0x03) && (intf->bInterfaceProtocol == 0x00)) {
    isMidi = true;
  }
}

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
