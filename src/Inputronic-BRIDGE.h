/**
 **************************************************
 *
 * @file        Inputronic-BRIDGE.h
 * @brief       Header file for Inputronic Bridge parser.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#ifndef INPUTRONIC_BRIDGE_PARSER_H
#define INPUTRONIC_BRIDGE_PARSER_H

#include "Arduino.h"
#include <SPI.h>
#include <Wire.h>

class InputronicParser
{
  public:
    enum CommProtocol
    {
        PROTOCOL_UART,
        PROTOCOL_I2C,
        PROTOCOL_SPI
    };

    struct KeyboardEvent
    {
        String payload;
        char key = 0;
        String keys[8];
        uint8_t keyCount = 0;
        bool valid = false;
    };
    struct MouseEvent
    {
        int16_t x = 0, y = 0;
        int8_t scroll = 0;
        bool btnLeft = 0, btnRight = 0, btnMiddle = 0, btnBackward = 0, btnForward = 0;
        bool btnScrollWheel = false;
        bool valid = false;
    };
    struct MIDIEvent
    {
        uint8_t b1 = 0, b2 = 0, b3 = 0;
        bool valid = false;
    };
    struct DescriptorEvent
    {
        String hex;
        bool valid = false;
    };
    struct HidRawEvent
    {
        String hex;
        bool valid = false;
    };

    struct EventBundle
    {
        KeyboardEvent keyboard;
        MouseEvent mouse;
        MIDIEvent midi;
        DescriptorEvent descriptor;
        HidRawEvent hidRaw;
    };

    /**
     * @brief Set the I2C slave address (default 0x50).
     *        Call before begin() if the bridge uses a non-default address.
     */
    void configureI2c(uint8_t addr = 0x50);

    /**
     * @brief Initialise I2C mode and verify the bridge is present.
     *
     * @param p                    Must be PROTOCOL_I2C.
     * @param wire                 Wire port to use (e.g. Wire, Wire1).
     *                             Call wire.begin(...) before this.
     * @param enableInterruptParam Enable interrupt-driven polling.
     * @param interruptPinParam    MCU pin connected to the bridge interrupt output.
     * @param activeHigh           true = interrupt fires on RISING, false = FALLING.
     * @return true  Bridge responded to PING.
     * @return false No response within timeout (bridge not connected or not ready).
     */
    bool begin(CommProtocol p, TwoWire &wire,
               bool enableInterruptParam = false, int8_t interruptPinParam = -1, bool activeHigh = true);

    /**
     * @brief Initialise SPI mode and verify the bridge is present.
     *
     * @param p                    Must be PROTOCOL_SPI.
     * @param spi                  SPI port to use (e.g. SPI, SPI1).
     *                             Call spi.begin(...) before this.
     * @param spiCs                Chip-select pin (output, driven by this library).
     * @param spiHz                SPI clock frequency in Hz (default 1 MHz).
     * @param enableInterruptParam Enable interrupt-driven polling.
     * @param interruptPinParam    MCU pin connected to the bridge interrupt output.
     * @param activeHigh           true = interrupt fires on RISING, false = FALLING.
     * @return true  Bridge responded to PING.
     * @return false No response within timeout.
     */
    bool begin(CommProtocol p, SPIClass &spi, uint8_t spiCs, uint32_t spiHz = 1000000,
               bool enableInterruptParam = false, int8_t interruptPinParam = -1, bool activeHigh = true);

    /**
     * @brief Initialise UART mode and verify the bridge is present.
     *
     * @param p                    Must be PROTOCOL_UART.
     * @param serial               HardwareSerial port to use (e.g. Serial1).
     *                             Call serial.begin(...) before this.
     * @param enableInterruptParam Enable interrupt-driven polling.
     * @param interruptPinParam    MCU pin connected to the bridge interrupt output.
     * @param activeHigh           true = interrupt fires on RISING, false = FALLING.
     * @return true  Bridge responded to PING within 500 ms.
     * @return false No response (bridge not connected or no USB device attached yet).
     */
    bool begin(CommProtocol p, HardwareSerial &serial,
               bool enableInterruptParam = false, int8_t interruptPinParam = -1, bool activeHigh = true);

    void requestDescriptor();
    void requestHidRawOnce();
    void setHidRawPolling(bool enabled);
    void setInterruptMode(bool enable, int8_t pin = -1, bool activeHigh = true);
    void enableInterruptPin(int8_t pin);
    void onDataReady(void (*callback)());
    void feedLine(const String &line);
    EventBundle pollEvents();

  private:
    CommProtocol protocol;
    EventBundle latest;

    TwoWire *i2cPort = nullptr;
    uint8_t i2cSlaveAddr = 0x50;

    SPIClass *spiPort = nullptr;
    uint8_t spiCsPin = 10;
    SPISettings spiSettings = SPISettings(1000000, MSBFIRST, SPI_MODE0);
    bool spiInitialized = false;
    static constexpr uint8_t SPI_MAX_LEN = 64;
    bool spiPendingAck = false;
    uint32_t spiFrameStartMs = 0;
    String spiPendingCommand;

    HardwareSerial *uartPort = nullptr;

    bool requestDescPending = false;
    bool requestHidRawPending = false;
    bool pollHidRawEnabled = false;
    bool enableInterrupt = false;
    bool expectingHidRawOnly = false;
    int8_t interruptPin = -1;
    static volatile bool interruptFlag;
    static void (*userIsrCallback)();

    bool checkConnection();
    void pollSpi();
    void sendSpiCommand(const char *command);
    void sendI2cCommand(const char *command);
    void configureInterrupt(bool enable, int8_t pin, bool activeHigh);
    static void IRAM_ATTR isrHandler();
    void parseMessage(const String &msgIn);
    void parseKeyboard(const String &msg);
    void parseMIDI(const String &msgIn);
    void parseMouse(const String &msgIn);
    void parseDescriptor(const String &msg);
    void parseHidRaw(const String &msg);
};

#endif
