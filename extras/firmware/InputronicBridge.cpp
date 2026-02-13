#include "InputronicBridge.h"
#include "show_desc.hpp"
#include "usbhhelp.hpp"
#include <esp_heap_caps.h>

String InputronicBridge::lastI2cMsg = "";
bool InputronicBridge::i2cMsgPending = false;
String InputronicBridge::lastSpiMsg = "";
bool InputronicBridge::spiMsgPending = false;

InputronicBridge &InputronicBridge::instance() {
  static InputronicBridge instance;
  return instance;
}

void InputronicBridge::begin() {
  EspUsbHost::begin();

  if (currentProtocol == protocolI2c && !i2cInitialized) {
    initI2cSlave();
  } else if (currentProtocol == protocolSpi && !spiInitialized) {
    initSpiSlave();
  }

  initInterruptPin();
}

void InputronicBridge::setProtocol(CommProtocol protocol) {
  currentProtocol = protocol;
  if (currentProtocol == protocolI2c && !i2cInitialized) {
    initI2cSlave();
  } else if (currentProtocol == protocolSpi && !spiInitialized) {
    initSpiSlave();
  }

  initInterruptPin();
}

void InputronicBridge::task() {
  EspUsbHost::task();
  handleHotplug();

  if (currentProtocol == protocolI2c) {
    handleI2cTransaction();
  } else if (currentProtocol == protocolSpi && spiInitialized) {
    handleSpiTransaction();
    vTaskDelay(1);
  }
}

void InputronicBridge::showConfigDescFullStatic(const usb_config_desc_t *configDesc) {
  instance().showConfigDescFull(configDesc);
}

static String toHexString(const uint8_t *data, size_t len) {
  String out;
  out.reserve(len * 2);
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < len; i++) {
    uint8_t v = data[i];
    out += hex[(v >> 4) & 0x0F];
    out += hex[v & 0x0F];
  }
  return out;
}

String InputronicBridge::buildDescriptorMessage() {
  if (configDescCache.empty()) {
    return "TS;DESC;EMPTY;TE";
  }
  size_t maxBytes = 50;
  if (configDescCache.size() < maxBytes) {
    maxBytes = configDescCache.size();
  }
  String hex = toHexString(configDescCache.data(), maxBytes);
  return String("TS;DESC;") + hex + ";TE";
}

String InputronicBridge::buildHidRawMessage() {
  if (lastHidRawHex.isEmpty()) {
    return "";
  }
  const size_t maxHexLen = 100;
  String hex = lastHidRawHex;
  if (hex.length() > maxHexLen) {
    hex = hex.substring(0, maxHexLen);
  }
  return String("TS;HIDRAW;") + hex + ";TE";
}

void InputronicBridge::updateLastHidRaw(uint8_t *data, size_t len) {
  if (!data || len == 0) {
    lastHidRawHex = "";
    return;
  }
  lastHidRawHex = toHexString(data, len);
  if (currentProtocol == protocolUart) {
    String msg = buildHidRawMessage();
    if (!msg.isEmpty()) {
      Serial.print(msg + "\n");
    }
  }
}


void InputronicBridge::onConfig(const uint8_t bDescriptorType, const uint8_t *p) {
  EspUsbHost::onConfig(bDescriptorType, p);
  switch (bDescriptorType) {
    case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
      {
        const usb_config_desc_t *config_desc = (const usb_config_desc_t *)p;
        configDescCache.clear();
        if (config_desc && config_desc->wTotalLength > 0) {
          configDescCache.insert(configDescCache.end(), config_desc->val, config_desc->val + config_desc->wTotalLength);
        }
      }
      show_config_desc(p);
      break;
    case USB_B_DESCRIPTOR_TYPE_INTERFACE: {
      show_interface_desc(p);
      const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
      if (intf->bInterfaceClass == USB_CLASS_AUDIO) {
        checkInterfaceDescMidi(p);
      }
      break;
    }
    case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
      show_endpoint_desc(p);
      if (isMidi && !isMidiReady) {
        prepareEndpoints(p);
      }
      break;
    default:
      break;
  }
}

void InputronicBridge::initI2cSlave() {
  i2c_config_t conf = {};
  conf.sda_io_num = i2cSda;
  conf.scl_io_num = i2cScl;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.mode = I2C_MODE_SLAVE;
  conf.slave.addr_10bit_en = 0;
  conf.slave.slave_addr = i2cSlaveAddr;

  i2c_param_config(i2cPort, &conf);
  i2c_driver_install(i2cPort, I2C_MODE_SLAVE, i2cBufLen, i2cBufLen, 0);
  i2cInitialized = true;

}

void InputronicBridge::initSpiSlave() {
  if (currentProtocol != protocolSpi) {
    return;
  }
  if (!spiRxBuf) {
    spiRxBuf = (uint8_t *)heap_caps_malloc(spiBufLen, MALLOC_CAP_DMA);
  }
  if (!spiTxBuf) {
    spiTxBuf = (uint8_t *)heap_caps_malloc(spiBufLen, MALLOC_CAP_DMA);
  }
  if (!spiRxBuf || !spiTxBuf) {
    if (spiRxBuf) {
      heap_caps_free(spiRxBuf);
      spiRxBuf = nullptr;
    }
    if (spiTxBuf) {
      heap_caps_free(spiTxBuf);
      spiTxBuf = nullptr;
    }
    return;
  }

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = spiMosi;
  buscfg.miso_io_num = spiMiso;
  buscfg.sclk_io_num = spiClk;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = spiBufLen;

  spi_slave_interface_config_t slvcfg = {};
  slvcfg.mode = 0;
  slvcfg.spics_io_num = spiCs;
  slvcfg.queue_size = 1;

  esp_err_t err = spi_slave_initialize(spiHost, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (err == ESP_OK) {
    spiInitialized = true;
  }
}

void InputronicBridge::initInterruptPin() {
  gpio_num_t pin = GPIO_NUM_NC;

  if (currentProtocol == protocolI2c) {
    pin = interruptPinI2c;
  } else if (currentProtocol == protocolSpi || currentProtocol == protocolUart) {
    pin = interruptPinSpiUart;
  }

  if (pin != GPIO_NUM_NC && pin != currentInterruptPin) {
    if (interruptPinInitialized && currentInterruptPin != GPIO_NUM_NC) {
      gpio_reset_pin(currentInterruptPin);
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(pin, 1);

    currentInterruptPin = pin;
    interruptPinInitialized = true;
  }
}

void InputronicBridge::pulseInterruptPin() {
  if (!interruptPinInitialized || currentInterruptPin == GPIO_NUM_NC) {
    return;
  }

  gpio_set_level(currentInterruptPin, 0);
  delayMicroseconds(20);
  gpio_set_level(currentInterruptPin, 1);
}

void InputronicBridge::handleI2cTransaction() {
  uint8_t rxBuf[i2cBufLen] = {0};

  int bytesRead = i2c_slave_read_buffer(i2cPort, rxBuf, i2cBufLen, 0);
  if (bytesRead > 0) {
    String received;
    for (int i = 0; i < bytesRead; i++) {
      received += static_cast<char>(rxBuf[i]);
    }
    received.trim();

    if (received == "PING") {
      lastI2cMsg = "TS;PONG;TE";
      i2cMsgPending = true;
      i2cMsgSent = false;
    } else if (received == "REQ:DESC") {
      lastI2cMsg = buildDescriptorMessage();
      i2cMsgPending = true;
      i2cMsgSent = false;
    } else if (received == "REQ:HIDRAW") {
      lastI2cMsg = buildHidRawMessage();
      i2cMsgPending = true;
      i2cMsgSent = false;
    } else if (received == "ACK") {
      lastI2cMsg.clear();
      i2cMsgPending = false;
      i2cMsgSent = false;
      return;
    }
  }

  if (!i2cMsgPending || lastI2cMsg.isEmpty()) {
    return;
  }

  if (i2cMsgSent) {
    return;
  }

  uint32_t now = millis();
  if (now - lastI2cWriteMs < 5) {
    return;
  }
  lastI2cWriteMs = now;

  uint8_t txBuf[i2cBufLen] = {0};
  uint8_t len = lastI2cMsg.length();
  if (len > i2cBufLen - 1) {
    len = i2cBufLen - 1;
  }

  txBuf[0] = len;
  memcpy(&txBuf[1], lastI2cMsg.c_str(), len);

  int written = i2c_slave_write_buffer(i2cPort, txBuf, len + 1, 0);
  if (written > 0) {
    i2cMsgSent = true;
    pulseInterruptPin();
  } else {
    // Buffer full or write failed — leave pending for next attempt
  }
}

String InputronicBridge::extractSpiCommand(const uint8_t *buf, size_t len) {
  if (!buf || len == 0) {
    return "";
  }
  size_t start = 0;
  while (start < len && buf[start] == 0) {
    start++;
  }
  if (start >= len) {
    return "";
  }
  String msg;
  for (size_t i = start; i < len; ++i) {
    if (buf[i] == 0) {
      break;
    }
    msg += static_cast<char>(buf[i]);
  }
  msg.trim();
  return msg;
}

void InputronicBridge::handleSpiTransaction() {
  if (!spiInitialized) {
    return;
  }

  // In interrupt mode, skip idle transactions to prevent stale empty
  // transfers from sitting in the SPI hardware queue.  Without this,
  // the master's next transfer after an interrupt serves the old empty
  // transaction instead of the new one carrying data ("trailing by one").
  if (interruptPinInitialized && !spiMsgPending) {
    return;
  }

  memset(spiRxBuf, 0, spiBufLen);
  memset(spiTxBuf, 0, spiBufLen);

  bool sentThisTransaction = false;
  if (spiMsgPending && !lastSpiMsg.isEmpty()) {
    uint8_t len = lastSpiMsg.length();
    if (len > spiBufLen - 1) {
      len = spiBufLen - 1;
    }
    spiTxBuf[0] = len;
    memcpy(&spiTxBuf[1], lastSpiMsg.c_str(), len);
    sentThisTransaction = true;
  }

  spi_slave_transaction_t t = {};
  t.length = spiBufLen * 8;
  t.rx_buffer = spiRxBuf;
  t.tx_buffer = spiTxBuf;

  // Pulse interrupt before blocking so master knows to initiate a transfer.
  // The slave enters spi_slave_transmit within microseconds of the pulse,
  // well before the master can respond.
  if (sentThisTransaction) {
    pulseInterruptPin();
  }

  esp_err_t err = spi_slave_transmit(spiHost, &t, pdMS_TO_TICKS(10));
  if (err == ESP_ERR_TIMEOUT) {
    return;
  }
  if (err == ESP_OK) {
    String received = extractSpiCommand(spiRxBuf, spiBufLen);
    if (received.length() > 0) {
      if (received == "PING") {
        lastSpiMsg = "TS;PONG;TE";
        spiMsgPending = true;
      } else if (received == "REQ:DESC") {
        lastSpiMsg = buildDescriptorMessage();
        spiMsgPending = true;
      } else if (received == "REQ:HIDRAW") {
        lastSpiMsg = buildHidRawMessage();
        spiMsgPending = true;
      } else if (received == "ACK") {
        lastSpiMsg.clear();
        spiMsgPending = false;
      }
    }
  }
  if (sentThisTransaction) {
    spiMsgPending = false;
    lastSpiMsg.clear();
  }
}

void InputronicBridge::showConfigDescFull(const usb_config_desc_t *configDesc) {
  const uint8_t *p = &configDesc->val[0];
  uint8_t bLength;
  for (int i = 0; i < configDesc->wTotalLength; i += bLength, p += bLength) {
    bLength = *p;
    if ((i + bLength) <= configDesc->wTotalLength) {
      const uint8_t bDescriptorType = *(p + 1);
      switch (bDescriptorType) {
        case USB_B_DESCRIPTOR_TYPE_DEVICE:
          break;
        case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
          show_config_desc(p);
          break;
        case USB_B_DESCRIPTOR_TYPE_INTERFACE: {
          show_interface_desc(p);
          const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;

          if (intf->bInterfaceClass == USB_CLASS_AUDIO) {
            checkInterfaceDescMidi(p);
          }
          break;
        }
        case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
          show_endpoint_desc(p);
          if (isMidi && !isMidiReady) {
            prepareEndpoints(p);
          }
          break;
        default:
          if (bDescriptorType == 0x21) {
          }
          break;
      }
    } else {
      return;
    }
  }
}

void InputronicBridge::handleHotplug() {
  if (xTaskGetTickCount() - lastHotplugCheck < pdMS_TO_TICKS(100)) {
    return;
  }
  lastHotplugCheck = xTaskGetTickCount();

  uint8_t addrList[8] = {0};
  int addrCount = 0;
  esp_err_t err = usb_host_device_addr_list_fill(sizeof(addrList), addrList, &addrCount);

  if (err == ESP_OK) {
    if (!deviceConnected && addrCount > 0) {
      deviceConnected = true;
      ;
    } else if (deviceConnected && addrCount == 0) {
      deviceConnected = false;
      resetUsbHost();
    }
  }
}

void InputronicBridge::resetUsbHost() {
  freeMidiTransfers();

  configDescCache.clear();
  lastHidRawHex.clear();

  esp_err_t err = usb_host_uninstall();
  if (err == ESP_OK) {
    isMidi = false;
    isMidiReady = false;
    midiOut = nullptr;
    for (auto &in : midiIn) {
      in = nullptr;
    }
    usbh_setup(showConfigDescFullStatic);
  }
}

void InputronicBridge::freeMidiTransfers() {
  for (int i = 0; i < static_cast<int>(midiInBuffers); i++) {
    if (midiIn[i]) {
      usb_host_transfer_free(midiIn[i]);
      midiIn[i] = nullptr;
    }
  }
  if (midiOut) {
    usb_host_transfer_free(midiOut);
    midiOut = nullptr;
  }
}