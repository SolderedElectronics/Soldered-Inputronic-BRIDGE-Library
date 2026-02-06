#ifndef INPUTRONIC_BRIDGE_H
#define INPUTRONIC_BRIDGE_H

#include "EspUsbHost.h"
#include "driver/i2c.h"
#include "driver/spi_slave.h"
#include <usb/usb_host.h>
#include <map>
#include <queue>
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
  static constexpr uint8_t i2cSlaveAddr = 0x50;
  static constexpr gpio_num_t i2cSda = GPIO_NUM_8;
  static constexpr gpio_num_t i2cScl = GPIO_NUM_9;
  static constexpr i2c_port_t i2cPort = I2C_NUM_0;
  static constexpr int i2cBufLen = 128;
  static constexpr gpio_num_t spiCs = GPIO_NUM_10;
  static constexpr gpio_num_t spiClk = GPIO_NUM_12;
  static constexpr gpio_num_t spiMiso = GPIO_NUM_13;
  static constexpr gpio_num_t spiMosi = GPIO_NUM_11;
  static constexpr int spiBufLen = 128;
  static constexpr spi_host_device_t spiHost = SPI2_HOST;

  CommProtocol currentProtocol = protocolI2c;
  bool i2cInitialized = false;
  bool spiInitialized = false;
  uint32_t lastI2cWriteMs = 0;

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
  bool i2cMsgSent = false;
  uint8_t *spiRxBuf = nullptr;
  uint8_t *spiTxBuf = nullptr;
  std::vector<uint8_t> configDescCache;
  String lastHidRawHex;
  static String lastSpiMsg;
  static bool spiMsgPending;

  void initI2cSlave();
  void initSpiSlave();
  void handleI2cTransaction();
  void handleSpiTransaction();
  static String extractSpiCommand(const uint8_t *buf, size_t len);
  String buildDescriptorMessage();
  String buildHidRawMessage();

  void updateMouseReport(const hid_mouse_report_t &report);
  void sendKeyboardReport();
  void sendMouseReport();
  void sendKeyboardUart();
  void sendKeyboardI2c();
  void sendKeyboardSpi();
  void sendMouseUart();
  void sendMouseI2c();
  void sendMouseSpi();
  void sendMidiReport(uint8_t b1, uint8_t b2, uint8_t b3);

  static void midiTransferCallback(usb_transfer_t *transfer);
  void checkInterfaceDescMidi(const void *p);
  void prepareEndpoints(const void *p);


  void showConfigDescFull(const usb_config_desc_t *configDesc);
  void handleHotplug();
  void resetUsbHost();
  void freeMidiTransfers();
};

#endif
