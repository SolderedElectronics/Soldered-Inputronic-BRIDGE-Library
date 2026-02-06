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

class InputronicParser {
public:
    enum CommProtocol { PROTOCOL_UART, PROTOCOL_I2C, PROTOCOL_SPI };

    struct KeyboardEvent {
        String payload;
        char key = 0;
        String keys[8];
        uint8_t keyCount = 0;
        bool valid = false;
    };
    struct MouseEvent {
        int16_t x = 0, y = 0;
        int8_t scroll = 0;
        bool btnLeft = 0, btnRight = 0, btnMiddle = 0, btnBackward = 0, btnForward = 0;
        bool valid = false;
    };
    struct MIDIEvent {
        uint8_t b1 = 0, b2 = 0, b3 = 0;
        bool valid = false;
    };
    struct DescriptorEvent {
        String hex;
        bool valid = false;
    };
    struct HidRawEvent {
        String hex;
        bool valid = false;
    };

    struct EventBundle {
        KeyboardEvent keyboard;
        MouseEvent mouse;
        MIDIEvent midi;
        DescriptorEvent descriptor;
        HidRawEvent hidRaw;
    };

    void begin(CommProtocol p, uint8_t spiCs = 10, uint32_t spiHz = 1000000,
               bool enableInterruptParam = false, int8_t interruptPinParam = -1, bool activeHigh = true);
    void begin(CommProtocol p, uint8_t spiCs, uint32_t spiHz,
               int8_t spiSck, int8_t spiMiso, int8_t spiMosi,
               bool enableInterruptParam = false, int8_t interruptPinParam = -1, bool activeHigh = true);

    void configureI2c(uint8_t addr, int8_t sda = -1, int8_t scl = -1, uint32_t clock = 100000);
    void requestDescriptor();
    void requestHidRawOnce();
    void setHidRawPolling(bool enabled);
    void setInterruptMode(bool enable, int8_t pin = -1, bool activeHigh = true);
    void feedLine(const String &line);
    EventBundle pollEvents();

private:
    CommProtocol protocol;
    String inputBuffer;
    EventBundle latest;
    uint8_t i2cSlaveAddr = 0x50;
    int8_t i2cSdaPin = -1;
    int8_t i2cSclPin = -1;
    uint32_t i2cClock = 100000;
    bool i2cInitialized = false;
    uint8_t spiCsPin = 10;
    int8_t spiSckPin = -1;
    int8_t spiMisoPin = -1;
    int8_t spiMosiPin = -1;
    SPISettings spiSettings = SPISettings(1000000, MSBFIRST, SPI_MODE0);
    bool spiInitialized = false;
    static constexpr uint8_t SPI_MAX_LEN = 128;
    bool spiPendingAck = false;
    uint32_t spiFrameStartMs = 0;
    String spiPendingCommand;
    bool requestDescPending = false;
    bool requestHidRawPending = false;
    bool pollHidRawEnabled = false;
    String lastHidRawHex;
    bool enableInterrupt = false;
    bool expectingHidRawOnly = false;

    void pollSpi();
    void sendSpiCommand(const char *command);
    void sendI2cCommand(const char *command);
    void configureInterrupt(bool enable, int8_t pin, bool activeHigh);
    void parseMessage(const String &msgIn);
    void parseKeyboard(const String &msg);
    void parseMIDI(const String &msgIn);
    void parseMouse(const String &msgIn);
    void parseDescriptor(const String &msg);
    void parseHidRaw(const String &msg);
};

#endif
