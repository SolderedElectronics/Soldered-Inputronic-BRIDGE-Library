#include "InputronicBridge.h"
#include "show_desc.hpp"
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
  if (msgMutex == nullptr) {
    msgMutex = xSemaphoreCreateMutex();
  }

  gpio_set_direction(jumperPin0, GPIO_MODE_INPUT);
  gpio_set_pull_mode(jumperPin0, GPIO_PULLDOWN_ONLY);
  gpio_set_direction(jumperPin1, GPIO_MODE_INPUT);
  gpio_set_pull_mode(jumperPin1, GPIO_PULLDOWN_ONLY);

  bool j0 = gpio_get_level(jumperPin0);
  bool j1 = gpio_get_level(jumperPin1);
  gpio_set_pull_mode(jumperPin0, GPIO_FLOATING);
  gpio_set_pull_mode(jumperPin1, GPIO_FLOATING);

  if (!j0 && !j1) {
    currentProtocol = protocolI2c;
    Serial.println("Set to I2C");
  } else if (j0 && j1) {
    currentProtocol = protocolSpi;
    Serial.println("Set to SPI");
  } else {
    currentProtocol = protocolUart;
    Serial.println("Set to UART");
  }

  setCpuFrequencyMhz(80);

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

  if (!deviceConnected) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }

  if (currentProtocol == protocolI2c) {
    handleI2cTransaction();
  } else if (currentProtocol == protocolSpi && spiInitialized) {
    handleSpiTransaction();
    vTaskDelay(1);
  } else if (currentProtocol == protocolUart) {
    static String uartRxBuf;
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        uartRxBuf.trim();
        if (uartRxBuf == "PING") {
          Serial.print("TS;PONG;TE\n");
          pulseInterruptPin();
        }
        uartRxBuf = "";
      } else {
        uartRxBuf += c;
      }
    }
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
    return;
  }
  String hex = toHexString(data, len);
  const size_t maxHexLen = 100;
  if (hex.length() > maxHexLen) {
    hex = hex.substring(0, maxHexLen);
  }
  String msg = String("TS;HIDRAW;") + hex + ";TE";
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    lastHidRawHex = hex;
    if (currentProtocol == protocolI2c) {
      lastHidRawI2cMsg = msg;
      hidRawI2cPending = true;
    }
    xSemaphoreGive(msgMutex);
  }
  if (currentProtocol == protocolUart) {
    Serial.print(msg);
    Serial.print('\n');
    pulseInterruptPin();
  }
}


void InputronicBridge::onConfig(const uint8_t bDescriptorType, const uint8_t *p) {
  EspUsbHost::onConfig(bDescriptorType, p);
  switch (bDescriptorType) {
    case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
      {
        const usb_config_desc_t *config_desc = (const usb_config_desc_t *)p;
        configDescCache.clear();
          if (config_desc && config_desc->wTotalLength > 0 && config_desc->wTotalLength <= 4096) {
          configDescCache.insert(configDescCache.end(), config_desc->val, config_desc->val + config_desc->wTotalLength);
        } else if (config_desc && config_desc->wTotalLength > 4096) {
          ESP_LOGI("InputronicBridge", "onConfig: wTotalLength=%d exceeds limit, skipping cache", config_desc->wTotalLength);
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
  gpio_num_t pin = kInterruptPin;

  if (pin != GPIO_NUM_NC && pin != currentInterruptPin) {
    if (interruptPinInitialized && currentInterruptPin != GPIO_NUM_NC) {
      gpio_hold_dis(currentInterruptPin);
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
    gpio_hold_en(pin);

    currentInterruptPin = pin;
    interruptPinInitialized = true;
  }
}

void InputronicBridge::pulseInterruptPin() {
  if (!interruptPinInitialized || currentInterruptPin == GPIO_NUM_NC) {
    return;
  }

  gpio_hold_dis(currentInterruptPin);
  gpio_set_level(currentInterruptPin, 0);
  delayMicroseconds(20);
  gpio_set_level(currentInterruptPin, 1);
  gpio_hold_en(currentInterruptPin);
}

void InputronicBridge::handleI2cTransaction() {
  static uint8_t rxBuf[i2cBufLen];

  int bytesRead = i2c_slave_read_buffer(i2cPort, rxBuf, i2cBufLen, 0);
  if (bytesRead > 0) {
    if (bytesRead < i2cBufLen) {
      memset(rxBuf + bytesRead, 0, i2cBufLen - bytesRead);
    }
    String received;
    for (int i = 0; i < bytesRead; i++) {
      received += static_cast<char>(rxBuf[i]);
    }
    received.trim();

    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (received == "PING") {
        lastI2cMsg = "TS;PONG;TE";
        i2cMsgPending = true;
        if (!i2cSentHidRaw) i2cMsgSent = false;
      } else if (received == "REQ:DESC") {
        lastI2cMsg = buildDescriptorMessage();
        i2cMsgPending = true;
        if (!i2cSentHidRaw) i2cMsgSent = false;
      } else if (received == "REQ:HIDRAW") {
        String hidMsg = buildHidRawMessage();
        if (!hidMsg.isEmpty()) {
          lastI2cMsg = hidMsg;
          i2cMsgPending = true;
          if (!i2cSentHidRaw) i2cMsgSent = false;
        }
      } else if (received == "ACK") {
        if (i2cSentHidRaw) {
          // ACK is for the HIDRAW channel — don't disturb Channel A
          i2cMsgSent = false;
          i2cSentHidRaw = false;
        } else {
          lastI2cMsg.clear();
          i2cMsgPending = false;
          i2cMsgSent = false;
        }
        xSemaphoreGive(msgMutex);
        return;
      }
      xSemaphoreGive(msgMutex);
    }
  }

  bool pending = false;
  bool sent = false;
  String msgSnapshot;
  bool isHidRawSend = false;

  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    // HIDRAW ACK timeout: unstick if master never responded
    if (i2cSentHidRaw && hidRawSentMs > 0 && (millis() - hidRawSentMs > 200)) {
      i2cMsgSent = false;
      i2cSentHidRaw = false;
    }
    pending = i2cMsgPending;
    sent = i2cMsgSent;
    // Promote HIDRAW when Channel A is idle, or when starved >10ms by mouse events.
    // Never preempt a keyboard report — keyboard latency is perceptible.
    bool channelAIsMouse = lastI2cMsg.startsWith("TS;M;");
    bool hidRawStarved = hidRawI2cPending && !lastHidRawI2cMsg.isEmpty() &&
                         !sent && channelAIsMouse && (millis() - hidRawSentMs) > 10;
    if (hidRawI2cPending && !lastHidRawI2cMsg.isEmpty() &&
        ((!pending && !sent) || hidRawStarved)) {
      msgSnapshot = lastHidRawI2cMsg;
      hidRawI2cPending = false;
      isHidRawSend = true;
      pending = true;
      sent = false;
    } else {
      msgSnapshot = lastI2cMsg;
    }
    xSemaphoreGive(msgMutex);
  }

  if (!pending || msgSnapshot.isEmpty()) {
    return;
  }

  if (sent) {
    return;
  }

  uint32_t now = millis();
  if (now - lastI2cWriteMs < 2) {
    return;
  }
  lastI2cWriteMs = now;

  uint8_t txBuf[i2cBufLen] = {0};
  uint8_t len = msgSnapshot.length();
  if (len > i2cBufLen - 1) {
    len = i2cBufLen - 1;
  }

  txBuf[0] = len;
  memcpy(&txBuf[1], msgSnapshot.c_str(), len);

  int written = i2c_slave_write_buffer(i2cPort, txBuf, len + 1, 0);
  if (written > 0) {
    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      i2cMsgSent = true;
      i2cSentHidRaw = isHidRawSend;
      xSemaphoreGive(msgMutex);
    }
    if (isHidRawSend) {
      hidRawSentMs = millis();
    }
    pulseInterruptPin();
  } else if (isHidRawSend) {
    // Write failed — restore pending so it retries next cycle
    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      hidRawI2cPending = true;
      xSemaphoreGive(msgMutex);
    }
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

  bool pending = false;
  String msgSnapshot;
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    pending = spiMsgPending;
    msgSnapshot = lastSpiMsg;
    xSemaphoreGive(msgMutex);
  }

  // In interrupt mode, skip idle transactions to prevent stale empty
  // transfers from sitting in the SPI hardware queue.  Without this,
  // the master's next transfer after an interrupt serves the old empty
  // transaction instead of the new one carrying data ("trailing by one").
  if (interruptPinInitialized && !pending) {
    return;
  }

  memset(spiRxBuf, 0, spiBufLen);
  memset(spiTxBuf, 0, spiBufLen);

  bool sentThisTransaction = false;
  if (pending && !msgSnapshot.isEmpty()) {
    uint8_t len = msgSnapshot.length();
    if (len > spiBufLen - 1) {
      len = spiBufLen - 1;
    }
    spiTxBuf[0] = len;
    memcpy(&spiTxBuf[1], msgSnapshot.c_str(), len);
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
      if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (received == "PING") {
          lastSpiMsg = "TS;PONG;TE";
          spiMsgPending = true;
        } else if (received == "REQ:DESC") {
          lastSpiMsg = buildDescriptorMessage();
          spiMsgPending = true;
        } else if (received == "REQ:HIDRAW") {
          String hidMsg = buildHidRawMessage();
          if (!hidMsg.isEmpty()) {
            lastSpiMsg = hidMsg;
            spiMsgPending = true;
          }
        } else if (received == "ACK") {
          lastSpiMsg.clear();
          spiMsgPending = false;
        }
        xSemaphoreGive(msgMutex);
      }
    }
  }
  if (sentThisTransaction) {
    if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      spiMsgPending = false;
      lastSpiMsg.clear();
      xSemaphoreGive(msgMutex);
    }
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
  TickType_t now = xTaskGetTickCount();
  if (now - lastHotplugCheck < pdMS_TO_TICKS(100)) {
    return;
  }
  lastHotplugCheck = now;

  uint8_t addrList[8] = {0};
  int addrCount = 0;
  esp_err_t err = usb_host_device_addr_list_fill(sizeof(addrList), addrList, &addrCount);

  if (err == ESP_OK) {
    if (!deviceConnected && addrCount > 0) {
      deviceConnected = true;
      setCpuFrequencyMhz(240);
    } else if (deviceConnected && addrCount == 0) {
      deviceConnected = false;
      setCpuFrequencyMhz(80);
      resetUsbHost();
    }
  }
}

void InputronicBridge::resetUsbHost() {
  freeMidiTransfers();

  configDescCache.clear();
  lastHidRawHex.clear();

  esp_err_t err = usb_host_client_deregister(clientHandle);
  if (err != ESP_OK) {
    ESP_LOGI("InputronicBridge", "usb_host_client_deregister() err=%x", err);
  }

  err = usb_host_uninstall();
  if (err == ESP_OK) {
    isMidi = false;
    isMidiReady = false;
    midiOut = nullptr;
    for (auto &in : midiIn) {
      in = nullptr;
    }
    // Reset transfer / interface bookkeeping before re-init
    usbTransferSize = 0;
    usbInterfaceSize = 0;
    isReady = false;
    EspUsbHost::begin();
  } else {
    ESP_LOGI("InputronicBridge", "usb_host_uninstall() err=%x", err);
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