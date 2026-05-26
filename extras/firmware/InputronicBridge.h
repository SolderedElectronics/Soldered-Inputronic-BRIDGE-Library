/**
 **************************************************
 *
 * @file        InputronicBridge.h
 * @brief       Firmware header for the Inputronic BRIDGE module.
 *              Declares the InputronicBridge singleton that receives USB HID
 *              and MIDI events and forwards them over I2C, SPI, or UART.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#ifndef INPUTRONIC_BRIDGE_H
#define INPUTRONIC_BRIDGE_H

#include "EspUsbHost.h"
#include "driver/i2c.h"
#include "driver/spi_slave.h"
#include <usb/usb_host.h>
#include <freertos/semphr.h>
#include <Preferences.h>
#include <map>
#include <queue>
#include <deque>
#include <vector>

class InputronicBridge : public EspUsbHost {
public:
  enum CommProtocol { protocolUart, protocolI2c, protocolSpi };

  /**
   * @brief       instance function returns the singleton bridge instance
   *
   * @return      InputronicBridge reference
   */
  static InputronicBridge &instance();

  /**
   * @brief       begin function initializes USB host and transport
   *
   * @note        Call once from setup before task
   *
   * @return      None
   */
  void begin();

  /**
   * @brief       setProtocol function selects the active transport
   *
   * @param       CommProtocol protocol
   *              Selected transport for outgoing messages
   *
   * @return      None
   */
  void setProtocol(CommProtocol protocol);

  /**
   * @brief       task function runs USB and transport processing
   *
   * @note        Call continuously from loop
   *
   * @return      None
   */
  void task();

  /**
   * @brief       onMouseButtons function handles mouse button changes
   *
   * @param       hid_mouse_report_t report
   *              Latest HID mouse report
   *
   * @param       uint8_t lastButtons
   *              Previous buttons bitmask
   *
   * @return      None
   */
  void onMouseButtons(hid_mouse_report_t report, uint8_t lastButtons) override;

  /**
   * @brief       onMouseMove function handles mouse motion reports
   *
   * @param       hid_mouse_report_t report
   *              Latest HID mouse report
   *
   * @return      None
   */
  void onMouseMove(hid_mouse_report_t report) override;

  /**
   * @brief       onKeyboard function handles boot keyboard reports
   *
   * @param       hid_keyboard_report_t report
   *              Latest HID keyboard report
   *
   * @param       hid_keyboard_report_t lastReport
   *              Previous HID keyboard report
   *
   * @return      None
   */
  void onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t lastReport) override;

  /**
   * @brief       onKeyboardKey function handles per-key callbacks
   *
   * @param       uint8_t ascii
   *              ASCII for key pressed
   *
   * @param       uint8_t keycode
   *              HID keycode for key pressed
   *
   * @param       uint8_t modifier
   *              Modifier mask at time of key press
   *
   * @return      None
   */
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override;


  /**
   * @brief       showConfigDescFullStatic function forwards USB config parsing
   *
   * @param       const usb_config_desc_t *configDesc
   *              USB configuration descriptor
   *
   * @return      None
   */
  static void showConfigDescFullStatic(const usb_config_desc_t *configDesc);

  /**
   * @brief       onConfig function processes USB descriptors for MIDI
   *
   * @param       uint8_t bDescriptorType
   *              Descriptor type value
   *
   * @param       const uint8_t *p
   *              Pointer to descriptor data
   *
   * @return      None
   */
  void onConfig(const uint8_t bDescriptorType, const uint8_t *p) override;
  void updateLastHidRaw(uint8_t *data, size_t len);

private:
  static constexpr uint8_t i2cDefaultAddr = 0x50;
  uint8_t i2cSlaveAddr = i2cDefaultAddr;
  static constexpr gpio_num_t i2cSda = GPIO_NUM_8;
  static constexpr gpio_num_t i2cScl = GPIO_NUM_9;
  static constexpr i2c_port_t i2cPort = I2C_NUM_0;
  static constexpr int i2cBufLen = 128;
  static constexpr gpio_num_t spiCs = GPIO_NUM_10;
  static constexpr gpio_num_t spiClk = GPIO_NUM_12;
  static constexpr gpio_num_t spiMiso = GPIO_NUM_13;
  static constexpr gpio_num_t spiMosi = GPIO_NUM_11;
  static constexpr int spiBufLen = 64;
  static constexpr spi_host_device_t spiHost = SPI2_HOST;
  static constexpr gpio_num_t kInterruptPin = GPIO_NUM_21;
  static constexpr gpio_num_t jumperPin0 = GPIO_NUM_6;
  static constexpr gpio_num_t jumperPin1 = GPIO_NUM_7;

  CommProtocol currentProtocol = protocolI2c;
  bool i2cInitialized = false;
  bool spiInitialized = false;
  uint32_t lastI2cWriteMs = 0;
  uint32_t channelASentMs = 0;
  uint8_t channelATimeoutRetries = 0;
  uint32_t i2cMsgSeq = 0;
  uint32_t i2cSentSeq = 0;
  gpio_num_t currentInterruptPin = GPIO_NUM_NC;
  bool interruptPinInitialized = false;

  struct KeyboardReport {
    String payload;
  };

  struct MouseReport {
    int16_t x = 0;
    int16_t y = 0;
    int8_t scroll = 0;
    bool btnLeft = false;
    bool btnRight = false;
    bool btnMiddle = false;
    bool btnBackward = false;
    bool btnForward = false;
    bool btnScrollWheel = false;
  };

  struct LatestReports {
    KeyboardReport keyboard;
    MouseReport mouse;
  } latestReports;

  bool isMidi = false;
  bool isMidiReady = false;
  static constexpr size_t midiInBuffers = 8;
  usb_transfer_t *midiOut = nullptr;
  usb_transfer_t *midiIn[midiInBuffers] = {nullptr};


  bool deviceConnected = false;
  TickType_t lastHotplugCheck = 0;


  static String lastI2cMsg;
  static bool i2cMsgPending;
  struct I2cQueuedMessage {
    String payload;
    bool isMouse = false;
    bool isCritical = false;
  };
  static constexpr size_t i2cQueueMaxDepth = 24;
  std::deque<I2cQueuedMessage> i2cMsgQueue;
  bool haveLastQueuedMouseState = false;
  uint8_t lastQueuedMouseButtonsMask = 0;
  bool i2cMsgSent = false;
  bool i2cSentHidRaw = false;
  uint32_t hidRawSentMs = 0;
  String lastHidRawI2cMsg;
  bool hidRawI2cPending = false;
  uint8_t *spiRxBuf = nullptr;
  uint8_t *spiTxBuf = nullptr;
  TaskHandle_t spiTaskHandle = nullptr;
  static void spiTaskEntry(void *arg);
  std::vector<uint8_t> configDescCache;
  String lastHidRawHex;
  static String lastSpiMsg;
  static bool spiMsgPending;

  SemaphoreHandle_t msgMutex = nullptr;
  Preferences prefs;

  /**
   * @brief       initI2cSlave function configures the ESP32 I2C slave driver
   *              on the pins and address stored in i2cSlaveAddr.
   */
  void initI2cSlave();

  /**
   * @brief       applyI2cAddressChange function validates newAddr, persists it
   *              to NVS, updates i2cSlaveAddr, and reinitialises the I2C slave
   *              driver so the new address takes effect immediately.
   *
   * @param       uint8_t newAddr
   *              7-bit I2C address (0x08–0x77).
   */
  void applyI2cAddressChange(uint8_t newAddr);

  /**
   * @brief       initSpiSlave function configures the ESP32 SPI slave driver
   *              with DMA-capable RX/TX buffers.
   */
  void initSpiSlave();

  /**
   * @brief       initInterruptPin function configures the interrupt output pin
   *              that is pulsed low to signal the host that data is ready.
   */
  void initInterruptPin();

  /**
   * @brief       pulseInterruptPin function drives the interrupt pin low for
   *              20 µs then returns it high so the host ISR fires once.
   */
  void pulseInterruptPin();

  /**
   * @brief       enqueueI2cChannelAMessageLocked function adds a message to the
   *              ordered I2C outbox queue.
   *
   * @note        Must be called with msgMutex held.
   *
   * @param       const String &msg
   *              Framed message string to send (e.g. "TS;M;...;TE").
   *
   * @param       bool isMouse
   *              true if the message carries mouse data (eligible for coalescing).
   *
   * @param       bool allowMouseCoalesce
   *              When true and the queue tail is also a non-critical mouse entry,
   *              the tail payload is replaced instead of adding a new entry.
   *
   * @param       bool isCritical
   *              true for button-state changes and scroll deltas; these entries
   *              are never used as the coalesce target by subsequent updates.
   */
  void enqueueI2cChannelAMessageLocked(const String &msg, bool isMouse, bool allowMouseCoalesce = false,
                                       bool isCritical = false);

  /**
   * @brief       refreshI2cChannelAFrontLocked function copies the front queue
   *              entry into lastI2cMsg so the TX section can send it, or clears
   *              lastI2cMsg when the queue is empty.
   *
   * @note        Must be called with msgMutex held.
   */
  void refreshI2cChannelAFrontLocked();

  /**
   * @brief       handleI2cTransaction function processes one I2C slave cycle:
   *              reads any pending host command from the RX ring buffer (ACK,
   *              PING, REQ:DESC, REQ:HIDRAW), then writes the next outbound
   *              message to the TX ring buffer and pulses the interrupt pin.
   */
  void handleI2cTransaction();

  /**
   * @brief       handleSpiTransaction function queues the pending outbound
   *              message as the SPI slave TX buffer, blocks until the master
   *              completes one transfer, then processes the received command.
   */
  void handleSpiTransaction();

  /**
   * @brief       extractSpiCommand function scans a raw SPI RX buffer and
   *              returns the first non-null, trimmed command string found.
   *
   * @param       const uint8_t *buf
   *              Pointer to the received SPI bytes.
   *
   * @param       size_t len
   *              Number of bytes in the buffer.
   *
   * @return      Extracted command string, or empty string if buffer is blank.
   */
  static String extractSpiCommand(const uint8_t *buf, size_t len);

  /**
   * @brief       buildDescriptorMessage function formats the cached USB
   *              configuration descriptor bytes as a hex string inside a
   *              TS;DESC;...;TE frame.
   *
   * @return      Framed descriptor message, or "TS;DESC;EMPTY;TE" if no
   *              descriptor has been cached yet.
   */
  String buildDescriptorMessage();

  /**
   * @brief       buildHidRawMessage function formats the most recent raw HID
   *              report bytes as a hex string inside a TS;HIDRAW;...;TE frame.
   *
   * @return      Framed HIDRAW message, or empty string if no report available.
   */
  String buildHidRawMessage();

  /**
   * @brief       updateMouseReport function copies all fields from a USB HID
   *              mouse report into the latestReports.mouse cache.
   *
   * @param       const hid_mouse_report_t &report
   *              HID mouse report received from EspUsbHost.
   */
  void updateMouseReport(const hid_mouse_report_t &report);

  /**
   * @brief       sendKeyboardReport function dispatches the current keyboard
   *              payload to the active transport (UART, I2C, or SPI).
   */
  void sendKeyboardReport();

  /**
   * @brief       sendMouseReport function dispatches the current mouse state
   *              to the active transport (UART, I2C, or SPI).
   */
  void sendMouseReport();

  /**
   * @brief       sendKeyboardUart function formats and transmits a keyboard
   *              packet over UART, then pulses the interrupt pin.
   */
  void sendKeyboardUart();

  /**
   * @brief       sendKeyboardI2c function formats a keyboard packet and
   *              enqueues it on the I2C Channel A queue as a critical entry.
   */
  void sendKeyboardI2c();

  /**
   * @brief       sendKeyboardSpi function formats a keyboard packet and
   *              stores it as the next SPI outbound message.
   */
  void sendKeyboardSpi();

  /**
   * @brief       sendMouseUart function formats and transmits a mouse packet
   *              over UART, then pulses the interrupt pin.
   */
  void sendMouseUart();

  /**
   * @brief       sendMouseI2c function formats a mouse packet and enqueues it
   *              on the I2C Channel A queue, marking it critical when button
   *              state or scroll changes so it cannot be coalesced away.
   */
  void sendMouseI2c();

  /**
   * @brief       sendMouseSpi function formats a mouse packet and stores it
   *              as the next SPI outbound message.
   */
  void sendMouseSpi();

  /**
   * @brief       sendMidiReport function formats a three-byte MIDI event as a
   *              TS;MIDI;...;TE frame and routes it to the active transport.
   *
   * @param       uint8_t b1
   *              First MIDI byte (status).
   *
   * @param       uint8_t b2
   *              Second MIDI byte (data 1).
   *
   * @param       uint8_t b3
   *              Third MIDI byte (data 2).
   */
  void sendMidiReport(uint8_t b1, uint8_t b2, uint8_t b3);

  /**
   * @brief       midiTransferCallback function is the USB transfer completion
   *              callback for MIDI IN endpoints. It unpacks all 4-byte USB MIDI
   *              packets from the transfer, formats them into a single
   *              TS;MIDI;...;TE frame, routes it to the active transport, and
   *              resubmits the transfer.
   *
   * @param       usb_transfer_t *transfer
   *              Completed USB transfer descriptor.
   */
  static void midiTransferCallback(usb_transfer_t *transfer);

  /**
   * @brief       checkInterfaceDescMidi function examines a USB interface
   *              descriptor and sets the isMidi flag when the interface matches
   *              the MIDI Streaming subclass.
   *
   * @param       const void *p
   *              Pointer to a usb_intf_desc_t descriptor.
   */
  void checkInterfaceDescMidi(const void *p);

  /**
   * @brief       prepareEndpoints function allocates USB host transfers for a
   *              MIDI bulk endpoint and immediately submits the IN transfers
   *              to begin receiving MIDI data.
   *
   * @param       const void *p
   *              Pointer to a usb_ep_desc_t descriptor.
   */
  void prepareEndpoints(const void *p);

  /**
   * @brief       showConfigDescFull function iterates the full USB
   *              configuration descriptor and dispatches each sub-descriptor
   *              to onConfig for logging and MIDI detection.
   *
   * @param       const usb_config_desc_t *configDesc
   *              Pointer to the USB configuration descriptor.
   */
  void showConfigDescFull(const usb_config_desc_t *configDesc);

  /**
   * @brief       handleHotplug function polls the USB device address list at
   *              100 ms intervals and updates deviceConnected, adjusting CPU
   *              frequency and resetting the USB host on disconnect.
   */
  void handleHotplug();

  /**
   * @brief       resetUsbHost function tears down the current USB host client,
   *              frees MIDI transfers, and reinitialises EspUsbHost so a newly
   *              connected device can be enumerated.
   */
  void resetUsbHost();

  /**
   * @brief       freeMidiTransfers function releases all allocated USB host
   *              transfer objects for MIDI IN and OUT endpoints.
   */
  void freeMidiTransfers();
};

#endif
