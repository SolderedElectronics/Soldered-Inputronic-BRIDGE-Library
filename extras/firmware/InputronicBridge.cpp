/**
 **************************************************
 *
 * @file        InputronicBridge.cpp
 * @brief       Core firmware source for the Inputronic BRIDGE module.
 *              Implements transport initialisation (I2C slave, SPI slave,
 *              interrupt pin), the I2C/SPI transaction handlers, USB hotplug
 *              detection, USB config descriptor parsing, and the HIDRAW helper.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "InputronicBridge.h"
#include "show_desc.hpp"
#include <esp_heap_caps.h>

String InputronicBridge::lastI2cMsg = "";
bool InputronicBridge::i2cMsgPending = false;
String InputronicBridge::lastSpiMsg = "";
bool InputronicBridge::spiMsgPending = false;

/**
 * @brief                   instance function returns the global singleton.
 */
InputronicBridge &InputronicBridge::instance() {
  static InputronicBridge instance;
  return instance;
}

/**
 * @brief                   begin function reads the protocol jumpers, starts
 *                          EspUsbHost, and initialises the selected transport
 *                          and interrupt pin. CPU is throttled to 80 MHz while
 *                          no USB device is connected.
 */
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

  Serial.println("[INIT] EspUsbHost::begin...");
  EspUsbHost::begin();
  Serial.println("[INIT] EspUsbHost::begin done");

  if (currentProtocol == protocolI2c && !i2cInitialized) {
    initI2cSlave();
  } else if (currentProtocol == protocolSpi && !spiInitialized) {
    initSpiSlave();
  }

  initInterruptPin();
  Serial.println("[INIT] begin() complete");
}

/**
 * @brief                   setProtocol function switches to the specified
 *                          transport at runtime, initialising it if needed.
 */
void InputronicBridge::setProtocol(CommProtocol protocol) {
  currentProtocol = protocol;
  if (currentProtocol == protocolI2c && !i2cInitialized) {
    initI2cSlave();
  } else if (currentProtocol == protocolSpi && !spiInitialized) {
    initSpiSlave();
  }

  initInterruptPin();
}

/**
 * @brief                   task function drives USB host processing, hotplug
 *                          detection, and the active transport handler. Call
 *                          continuously from the Arduino loop.
 */
void InputronicBridge::task() {
  EspUsbHost::task();
  handleHotplug();

  if (currentProtocol == protocolI2c) {
    handleI2cTransaction();
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

  if (!deviceConnected) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }
}

/**
 * @brief                   showConfigDescFullStatic function is the static
 *                          trampoline used by EspUsbHost to deliver the USB
 *                          configuration descriptor to the singleton instance.
 */
void InputronicBridge::showConfigDescFullStatic(const usb_config_desc_t *configDesc) {
  instance().showConfigDescFull(configDesc);
}

/**
 * @brief                   toHexString function converts a byte array to an
 *                          upper-case hexadecimal string (2 chars per byte).
 */
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

/**
 * @brief                   buildDescriptorMessage function formats the cached
 *                          USB configuration descriptor as a TS;DESC;...;TE
 *                          frame (first 50 bytes, hex-encoded).
 */
String InputronicBridge::buildDescriptorMessage() {
  if (configDescCache.empty()) {
    return "TS;DESC;EMPTY;TE";
  }
  // 25 raw bytes → 50 hex chars → total frame = 1+8+50+3 = 62 bytes ≤ spiBufLen(64).
  size_t maxBytes = 25;
  if (configDescCache.size() < maxBytes) {
    maxBytes = configDescCache.size();
  }
  String hex = toHexString(configDescCache.data(), maxBytes);
  return String("TS;DESC;") + hex + ";TE";
}

/**
 * @brief                   buildHidRawMessage function formats the most recent
 *                          raw HID report as a TS;HIDRAW;...;TE frame, capped
 *                          at 34 hex chars (17 bytes) to fit one 48-byte I2C
 *                          transfer.
 */
String InputronicBridge::buildHidRawMessage() {
  if (lastHidRawHex.isEmpty()) {
    return "";
  }
  // Same 34-char limit as updateLastHidRaw — must fit in one 48-byte transfer.
  const size_t maxHexLen = 34;
  String hex = lastHidRawHex;
  if (hex.length() > maxHexLen) {
    hex = hex.substring(0, maxHexLen);
  }
  return String("TS;HIDRAW;") + hex + ";TE";
}

/**
 * @brief                   updateLastHidRaw function stores the latest raw HID
 *                          report for later delivery on explicit REQ:HIDRAW.
 *                          For UART it transmits immediately; for I2C and SPI
 *                          the data is cached and sent only on host request to
 *                          avoid polluting the TX ring buffer.
 */
void InputronicBridge::updateLastHidRaw(uint8_t *data, size_t len) {
  if (!data || len == 0) {
    return;
  }
  String hex = toHexString(data, len);
  // Limit to 34 hex chars (17 raw bytes) so the full HIDRAW message fits in
  // one 48-byte I2C transfer: 1 (len) + 10 ("TS;HIDRAW;") + 34 + 3 (";TE") = 48.
  // Exceeding this leaves leftover bytes in the ESP32 I2C slave TX ring buffer
  // which corrupt subsequent Channel A (keyboard/mouse) transfers.
  const size_t maxHexLen = 34;
  if (hex.length() > maxHexLen) {
    hex = hex.substring(0, maxHexLen);
  }
  String msg = String("TS;HIDRAW;") + hex + ";TE";
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    lastHidRawHex = hex;
    if (currentProtocol == protocolI2c) {
      lastHidRawI2cMsg = msg;
      // Do NOT set hidRawI2cPending here — proactive HIDRAW would overflow the
      // 48-byte I2C read window, leaving garbage in the TX ring buffer that
      // causes the master to ACK stale data and silently drop queued events.
      // hidRawI2cPending is set only when the host explicitly sends REQ:HIDRAW.
    }
    xSemaphoreGive(msgMutex);
  }
  if (currentProtocol == protocolUart) {
    Serial.print(msg);
    Serial.print('\n');
    pulseInterruptPin();
  }
}


/**
 * @brief                   onConfig function is the EspUsbHost callback for
 *                          each USB descriptor sub-type. It caches the config
 *                          descriptor, logs interface and endpoint descriptors,
 *                          and detects MIDI streaming interfaces.
 */
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

/**
 * @brief                   initI2cSlave function installs the ESP32 I2C slave
 *                          driver with a 128-byte RX/TX ring buffer on the
 *                          SDA/SCL pins defined by i2cSda and i2cScl.
 */
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

/**
 * @brief                   initSpiSlave function allocates DMA-capable RX/TX
 *                          buffers and installs the ESP32 SPI slave driver on
 *                          SPI2_HOST using the pins defined in the header.
 */
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

  esp_err_t err = spi_slave_initialize(spiHost, &buscfg, &slvcfg, SPI_DMA_DISABLED);
  if (err == ESP_OK) {
    spiInitialized = true;
    Serial.println("[SPI] slave init OK");
    if (spiTaskHandle == nullptr) {
      xTaskCreate(spiTaskEntry, "spi_bridge", 4096, this, 3, &spiTaskHandle);
    }
  } else {
    Serial.printf("[SPI] slave init FAILED: 0x%x (%s)\n", err, esp_err_to_name(err));
  }
}

/**
 * @brief                   initInterruptPin function configures kInterruptPin
 *                          as a push-pull output, drives it high, and holds
 *                          the level across sleep cycles via gpio_hold_en.
 */
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

/**
 * @brief                   pulseInterruptPin function briefly drives the
 *                          interrupt output low (20 µs) and then returns it
 *                          high so the host MCU's falling-edge ISR fires once.
 */
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

/**
 * @brief                   refreshI2cChannelAFrontLocked function promotes the
 *                          head of the I2C message queue into lastI2cMsg so the
 *                          TX section can write it, or clears lastI2cMsg when
 *                          the queue is empty. Must be called with msgMutex held.
 */
void InputronicBridge::refreshI2cChannelAFrontLocked() {
  if (!i2cMsgQueue.empty()) {
    lastI2cMsg = i2cMsgQueue.front().payload;
    i2cMsgPending = true;
  } else {
    lastI2cMsg.clear();
    i2cMsgPending = false;
  }
}

/**
 * @brief                   enqueueI2cChannelAMessageLocked function adds msg
 *                          to the ordered I2C outbox, coalescing consecutive
 *                          mouse-movement updates to keep the queue shallow.
 *                          When the queue is full, the oldest non-critical mouse
 *                          packet is evicted first; critical events (button
 *                          changes, scroll) are protected. Must be called with
 *                          msgMutex held.
 */
void InputronicBridge::enqueueI2cChannelAMessageLocked(const String &msg, bool isMouse, bool allowMouseCoalesce,
                                                       bool isCritical) {
  if (isMouse && allowMouseCoalesce && !i2cMsgQueue.empty() && i2cMsgQueue.back().isMouse) {
    // Coalesce consecutive mouse updates while preserving keyboard/MIDI order.
    i2cMsgQueue.back().payload = msg;
  } else {
    if (i2cMsgQueue.size() >= i2cQueueMaxDepth) {
      // Front packet may already be in-flight waiting for ACK; never drop/reorder it.
      const bool frontLocked = i2cMsgSent && !i2cSentHidRaw && !i2cMsgQueue.empty();
      const size_t scanStart = frontLocked ? 1 : 0;

      // Prefer dropping the oldest non-critical mouse movement packet first.
      size_t dropIdx = i2cMsgQueue.size();
      for (size_t i = scanStart; i < i2cMsgQueue.size(); i++) {
        if (i2cMsgQueue[i].isMouse && !i2cMsgQueue[i].isCritical) {
          dropIdx = i;
          break;
        }
      }
      if (dropIdx < i2cMsgQueue.size()) {
        i2cMsgQueue.erase(i2cMsgQueue.begin() + dropIdx);
      } else {
        // No preferred drop candidate. Drop the newest non-critical packet if possible.
        // This preserves ordering and protects the in-flight front packet.
        size_t tailDropIdx = i2cMsgQueue.size();
        for (size_t i = i2cMsgQueue.size(); i > scanStart; i--) {
          size_t idx = i - 1;
          if (!i2cMsgQueue[idx].isCritical) {
            tailDropIdx = idx;
            break;
          }
        }
        if (tailDropIdx < i2cMsgQueue.size()) {
          i2cMsgQueue.erase(i2cMsgQueue.begin() + tailDropIdx);
        } else if (!frontLocked && !i2cMsgQueue.empty()) {
          i2cMsgQueue.pop_front();
        } else {
          // Queue is full of critical items and front is locked; keep queue unchanged.
          // For non-critical new packets, drop the newcomer.
          if (!isCritical) {
            return;
          }
          // For critical newcomer, replace oldest non-front critical as last resort.
          if (scanStart < i2cMsgQueue.size()) {
            i2cMsgQueue.erase(i2cMsgQueue.begin() + scanStart);
          } else {
            return;
          }
        }
      }
    }
    I2cQueuedMessage queued;
    queued.payload = msg;
    queued.isMouse = isMouse;
    queued.isCritical = isCritical;
    i2cMsgQueue.push_back(queued);
  }
  i2cMsgSeq++;
  refreshI2cChannelAFrontLocked();
}

/**
 * @brief                   handleI2cTransaction function is the main I2C
 *                          slave pump called every task() iteration. It reads
 *                          any host command from the RX ring buffer (ACK, PING,
 *                          REQ:DESC, REQ:HIDRAW), advances the outbox queue on
 *                          ACK, then writes the next pending message to the TX
 *                          ring buffer and pulses the interrupt pin.
 *
 *                          A minimum 2 ms gap between successive TX writes
 *                          prevents back-to-back slave buffer writes that can
 *                          confuse the ESP32 I2C slave hardware.
 */
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
      // The RX FIFO may contain multiple back-to-back control writes
      // (e.g. "ACKREQ:HIDRAW"). Parse by token presence, not exact equality.
      if (received.indexOf("ACK") >= 0 && i2cMsgSent) {
        if (i2cSentHidRaw) {
          // ACK is for the HIDRAW channel — don't disturb Channel A
          i2cMsgSent = false;
          i2cSentHidRaw = false;
        } else {
          if (!i2cMsgQueue.empty()) {
            i2cMsgQueue.pop_front();
          }
          i2cMsgSent = false;
          channelATimeoutRetries = 0;
          refreshI2cChannelAFrontLocked();
        }
      }
      if (received.indexOf("PING") >= 0) {
        enqueueI2cChannelAMessageLocked("TS;PONG;TE", false);
      }
      if (received.indexOf("REQ:DESC") >= 0) {
        enqueueI2cChannelAMessageLocked(buildDescriptorMessage(), false);
      }
      if (received.indexOf("REQ:HIDRAW") >= 0) {
        String hidMsg = buildHidRawMessage();
        if (!hidMsg.isEmpty()) {
          // Keep HIDRAW on its dedicated pending channel so frequent REQ:HIDRAW
          // polling does not overwrite Channel A mouse/keyboard packets.
          lastHidRawI2cMsg = hidMsg;
          hidRawI2cPending = true;
        }
      }
      xSemaphoreGive(msgMutex);
    }
  }

  bool pending = false;
  bool sent = false;
  String msgSnapshot;
  bool isHidRawSend = false;

  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    // Channel A ACK timeout: avoid permanent stalls on dropped ACKs.
    if (!i2cSentHidRaw && i2cMsgSent && channelASentMs > 0 && (millis() - channelASentMs > 120)) {
      i2cMsgSent = false;
      // Don't let one repeatedly un-ACKed packet stall mouse flow forever.
      if (++channelATimeoutRetries >= 2) {
        if (!i2cMsgQueue.empty()) {
          i2cMsgQueue.pop_front();
        }
        channelATimeoutRetries = 0;
        refreshI2cChannelAFrontLocked();
      }
    }
    // HIDRAW ACK timeout: unstick if master never responded
    if (i2cSentHidRaw && hidRawSentMs > 0 && (millis() - hidRawSentMs > 200)) {
      i2cMsgSent = false;
      i2cSentHidRaw = false;
    }
    pending = i2cMsgPending;
    sent = i2cMsgSent;
    // Serve HIDRAW when Channel A is idle, plus occasional fair-share slots
    // while Channel A is mouse-heavy so raw polling does not starve.
    bool allowHidRaw = (!pending && !sent);
    if (!allowHidRaw && !sent && !i2cMsgQueue.empty() && i2cMsgQueue.front().isMouse &&
        (millis() - hidRawSentMs) > 12) {
      allowHidRaw = true;
    }
    if (hidRawI2cPending && !lastHidRawI2cMsg.isEmpty() && allowHidRaw) {
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
      if (!isHidRawSend) {
        i2cSentSeq = i2cMsgSeq;
        channelASentMs = millis();
      }
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

/**
 * @brief                   extractSpiCommand function skips leading null bytes
 *                          in the SPI RX buffer and returns the first non-null,
 *                          null-terminated, whitespace-trimmed string.
 */
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

/**
 * @brief                   spiTaskEntry runs handleSpiTransaction in a tight
 *                          loop. spi_slave_transmit inside handleSpiTransaction
 *                          blocks (suspending this task) while waiting for the
 *                          master, giving the Arduino loop full CPU access to
 *                          call EspUsbHost::task() and fire HID callbacks.
 */
void InputronicBridge::spiTaskEntry(void *arg) {
  InputronicBridge *self = static_cast<InputronicBridge *>(arg);
  while (true) {
    self->handleSpiTransaction();
  }
}

/**
 * @brief                   handleSpiTransaction function services one SPI slave
 *                          cycle. spi_slave_transmit suspends this task for up
 *                          to 50 ms while waiting for the master, so the
 *                          FreeRTOS scheduler can run the Arduino loop and USB
 *                          host daemon. SPI_DMA_DISABLED avoids GDMA channel
 *                          conflicts with the USB host stack on ESP32-S3.
 */
void InputronicBridge::handleSpiTransaction() {
  if (!spiInitialized) {
    vTaskDelay(pdMS_TO_TICKS(100));
    spi_slave_free(spiHost);
    initSpiSlave();
    return;
  }

  bool pending = false;
  String msgSnapshot;
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    pending = spiMsgPending;
    msgSnapshot = lastSpiMsg;
    xSemaphoreGive(msgMutex);
  }

  memset(spiRxBuf, 0, spiBufLen);
  memset(spiTxBuf, 0, spiBufLen);

  bool willSend = pending && !msgSnapshot.isEmpty();
  if (willSend) {
    uint8_t len = (uint8_t)min((int)msgSnapshot.length(), spiBufLen - 1);
    spiTxBuf[0] = len;
    memcpy(&spiTxBuf[1], msgSnapshot.c_str(), len);
  }

  spi_slave_transaction_t t = {};
  t.length = spiBufLen * 8;
  t.rx_buffer = spiRxBuf;
  t.tx_buffer = spiTxBuf;

  if (willSend) {
    pulseInterruptPin();
  }

  // pdMS_TO_TICKS(50) is always >= 1 tick even at 100 Hz FreeRTOS (50/10 = 5).
  // This blocks and suspends the task so the scheduler runs other work.
  esp_err_t err = spi_slave_transmit(spiHost, &t, pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    return;
  }

  String received = extractSpiCommand(spiRxBuf, spiBufLen);
  if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (received == "PING") {
      lastSpiMsg = "TS;PONG;TE";
      spiMsgPending = true;
    } else if (received == "ACK") {
      lastSpiMsg.clear();
      spiMsgPending = false;
    } else if (received == "REQ:DESC") {
      lastSpiMsg = buildDescriptorMessage();
      spiMsgPending = true;
    } else if (received == "REQ:HIDRAW") {
      String hidMsg = buildHidRawMessage();
      if (!hidMsg.isEmpty()) {
        lastSpiMsg = hidMsg;
        spiMsgPending = true;
      }
    }
    xSemaphoreGive(msgMutex);
  }

  if (willSend) {
    bool sentPong = (msgSnapshot == "TS;PONG;TE");
    if (!sentPong) {
      if (msgMutex && xSemaphoreTake(msgMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        spiMsgPending = false;
        lastSpiMsg.clear();
        xSemaphoreGive(msgMutex);
      }
    }
  }
}

/**
 * @brief                   showConfigDescFull function walks every descriptor
 *                          in the USB configuration descriptor and dispatches
 *                          each one to the appropriate show_*_desc logger and
 *                          to checkInterfaceDescMidi / prepareEndpoints.
 */
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

/**
 * @brief                   handleHotplug function polls the USB device address
 *                          list every 100 ms. On connect it boosts the CPU to
 *                          240 MHz; on disconnect it drops to 80 MHz and calls
 *                          resetUsbHost to re-enumerate the next device.
 */
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

/**
 * @brief                   resetUsbHost function tears down the active USB host
 *                          client, frees MIDI transfers, and calls
 *                          EspUsbHost::begin() to reinitialise the stack so the
 *                          next device plugged in is enumerated cleanly.
 */
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

/**
 * @brief                   freeMidiTransfers function releases all USB host
 *                          transfer objects allocated for MIDI IN and OUT
 *                          endpoints, setting all pointers to nullptr.
 */
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