/**
 **************************************************
 *
 * @file        Inputronic-BRIDGE.cpp
 * @brief       Source file for Inputronic Bridge parser.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors     @ soldered.com
 ***************************************************/

#include "Inputronic-BRIDGE.h"

/**
 * @brief                   Initialize communication mode (UART, I2C or SPI).
 */
void InputronicParser::begin(CommProtocol p, uint8_t spiCs, uint32_t spiHz,
                             bool enableInterruptParam, int8_t interruptPinParam, bool activeHigh) {
    protocol = p;
    spiCsPin = spiCs;
    spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);
    configureInterrupt(enableInterruptParam, interruptPinParam, activeHigh);
    if (protocol == PROTOCOL_I2C && !i2cInitialized) {
        if (i2cSdaPin >= 0 && i2cSclPin >= 0) {
            Wire.begin(i2cSdaPin, i2cSclPin, i2cClock);
        } else {
            Wire.begin();
        }
        i2cInitialized = true;
    }
    if (protocol == PROTOCOL_SPI) {
        SPI.begin();
        pinMode(spiCsPin, OUTPUT);
        digitalWrite(spiCsPin, HIGH);
        spiInitialized = true;
    }
}

/**
 * @brief                   Initialize communication mode with custom SPI pins.
 */
void InputronicParser::begin(CommProtocol p, uint8_t spiCs, uint32_t spiHz,
                             int8_t spiSck, int8_t spiMiso, int8_t spiMosi,
                             bool enableInterruptParam, int8_t interruptPinParam, bool activeHigh) {
    protocol = p;
    spiCsPin = spiCs;
    spiSckPin = spiSck;
    spiMisoPin = spiMiso;
    spiMosiPin = spiMosi;
    spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);
    configureInterrupt(enableInterruptParam, interruptPinParam, activeHigh);
    if (protocol == PROTOCOL_SPI) {
        SPI.begin(spiSckPin, spiMisoPin, spiMosiPin, spiCsPin);
        pinMode(spiCsPin, OUTPUT);
        digitalWrite(spiCsPin, HIGH);
        spiInitialized = true;
    }
}

/**
 * @brief                   Configure I2C address and optional pins.
 */
void InputronicParser::configureI2c(uint8_t addr, int8_t sda, int8_t scl, uint32_t clock) {
    i2cSlaveAddr = addr;
    i2cSdaPin = sda;
    i2cSclPin = scl;
    i2cClock = clock;
    i2cInitialized = false;
}

/**
 * @brief                   Request device descriptor on next poll.
 */
void InputronicParser::requestDescriptor() {
    requestDescPending = true;
}

/**
 * @brief                   Request raw HID once on next poll.
 */
void InputronicParser::requestHidRawOnce() {
    requestHidRawPending = true;
    expectingHidRawOnly = true;
}

/**
 * @brief                   Enable or disable raw HID polling.
 */
void InputronicParser::setHidRawPolling(bool enabled) {
    pollHidRawEnabled = enabled;
    expectingHidRawOnly = enabled;
}

/**
 * @brief                   Enable or disable interrupt mode.
 */
void InputronicParser::setInterruptMode(bool enable, int8_t pin, bool activeHigh) {
    (void)pin;
    (void)activeHigh;
    enableInterrupt = enable;
}

/**
 * @brief                   Feed a raw line into the parser.
 */
void InputronicParser::feedLine(const String &line) {
    parseMessage(line);
}

/**
 * @brief                   Poll for events and return any newly parsed data.
 */
InputronicParser::EventBundle InputronicParser::pollEvents() {
    if (protocol == PROTOCOL_UART) {
        static String uartBuffer = "";

        while (Serial1.available()) {
            uartBuffer += (char)Serial1.read();
        }

        int tePos;
        while ((tePos = uartBuffer.indexOf(";TE")) != -1) {
            String fullMsg = uartBuffer.substring(0, tePos + 3);
            feedLine(fullMsg);
            uartBuffer.remove(0, tePos + 3);
            uartBuffer.trim();
        }
    } else if (protocol == PROTOCOL_I2C) {
        const uint8_t MAX_LEN = 128;

        if (enableInterrupt && !requestDescPending && !requestHidRawPending) {
            EventBundle out = latest;
            latest.keyboard.valid = false;
            latest.mouse.valid = false;
            latest.midi.valid = false;
            latest.descriptor.valid = false;
            latest.hidRaw.valid = false;
            return out;
        }

        if (!i2cInitialized) {
            if (i2cSdaPin >= 0 && i2cSclPin >= 0) {
                Wire.begin(i2cSdaPin, i2cSclPin, i2cClock);
            } else {
                Wire.begin();
            }
            i2cInitialized = true;
        }

        if (!enableInterrupt) {
            if (requestDescPending) {
                sendI2cCommand("REQ:DESC");
                requestDescPending = false;
            }
            if (requestHidRawPending || pollHidRawEnabled) {
                sendI2cCommand("REQ:HIDRAW");
                requestHidRawPending = false;
            }
        }

        Wire.requestFrom(i2cSlaveAddr, (uint8_t)MAX_LEN);

        if (Wire.available()) {
            uint8_t payloadLen = Wire.read();
            if (payloadLen > 0 && payloadLen < MAX_LEN) {
                String msg = "";
                while (Wire.available() && msg.length() < payloadLen) {
                    msg += (char)Wire.read();
                }

                static String i2cBuffer = "";
                if (msg.length() == payloadLen) {
                    i2cBuffer += msg;
                }
                if (i2cBuffer.length() > 256) {
                    i2cBuffer = "";
                }

                int tePos;
                while ((tePos = i2cBuffer.indexOf(";TE")) != -1) {
                    String fullMsg = i2cBuffer.substring(0, tePos + 3);
                    feedLine(fullMsg);
                    i2cBuffer.remove(0, tePos + 3);
                    i2cBuffer.trim();
                }

                Wire.beginTransmission(i2cSlaveAddr);
                Wire.write((const uint8_t *)"ACK", 3);
                Wire.endTransmission();
            }
        }
    } else if (protocol == PROTOCOL_SPI) {
        if (enableInterrupt && !requestDescPending && !requestHidRawPending) {
            EventBundle out = latest;
            latest.keyboard.valid = false;
            latest.mouse.valid = false;
            latest.midi.valid = false;
            latest.descriptor.valid = false;
            latest.hidRaw.valid = false;
            return out;
        }
        if (!enableInterrupt) {
            if (requestDescPending) {
                sendSpiCommand("REQ:DESC");
                requestDescPending = false;
            }
            if (requestHidRawPending || pollHidRawEnabled) {
                sendSpiCommand("REQ:HIDRAW");
                requestHidRawPending = false;
            }
        }
        pollSpi();
    }

    EventBundle out = latest;
    latest.keyboard.valid = false;
    latest.mouse.valid = false;
    latest.midi.valid = false;
    latest.descriptor.valid = false;
    latest.hidRaw.valid = false;
    return out;
}

/**
 * @brief                   Poll the SPI transport and parse any frames.
 */
void InputronicParser::pollSpi() {
    if (!spiInitialized) {
        return;
    }

    uint8_t txBuf[SPI_MAX_LEN] = {0};
    uint8_t rxBuf[SPI_MAX_LEN] = {0};
    if (spiPendingAck) {
        const char *ack = "ACK";
        size_t len = strlen(ack);
        if (len > SPI_MAX_LEN) {
            len = SPI_MAX_LEN;
        }
        memcpy(txBuf, ack, len);
        spiPendingAck = false;
    } else if (spiPendingCommand.length() > 0) {
        size_t len = spiPendingCommand.length();
        if (len > SPI_MAX_LEN) {
            len = SPI_MAX_LEN;
        }
        memcpy(txBuf, spiPendingCommand.c_str(), len);
        spiPendingCommand = "";
    }

    SPI.beginTransaction(spiSettings);
    digitalWrite(spiCsPin, LOW);
    SPI.transferBytes(txBuf, rxBuf, SPI_MAX_LEN);
    digitalWrite(spiCsPin, HIGH);
    SPI.endTransaction();

    uint8_t payloadLen = rxBuf[0];
    String msg = "";
    int startIndex = -1;
    bool hasData = false;

    for (uint8_t i = 0; i + 2 < SPI_MAX_LEN; i++) {
        if (rxBuf[i] != 0) {
            hasData = true;
        }
        if (rxBuf[i] == 'T' && rxBuf[i + 1] == 'S' && rxBuf[i + 2] == ';') {
            startIndex = i;
            break;
        }
    }
    if (hasData && startIndex < 0) {
        for (uint8_t i = 0; i < SPI_MAX_LEN; i++) {
            if (rxBuf[i] != 0) {
                startIndex = i;
                break;
            }
        }
    }

    if (payloadLen > 0 && payloadLen < SPI_MAX_LEN) {
        for (uint8_t i = 0; i < payloadLen; i++) {
            msg += static_cast<char>(rxBuf[i + 1]);
        }
    } else if (startIndex >= 0) {
        for (uint8_t i = startIndex; i < SPI_MAX_LEN; i++) {
            if (rxBuf[i] == 0) {
                break;
            }
            msg += static_cast<char>(rxBuf[i]);
        }
    }

    if (hasData) {
        spiPendingAck = true;
    }
    if (msg.length() > 0) {
        static String spiBuffer = "";
        spiBuffer += msg;

        const size_t maxSpiBuffer = 256;
        if (spiBuffer.length() > maxSpiBuffer) {
            int lastTs = spiBuffer.lastIndexOf("TS;");
            if (lastTs >= 0) {
                spiBuffer = spiBuffer.substring(lastTs);
            } else {
                spiBuffer = "";
            }
        }

        while (true) {
            int tsPos = spiBuffer.indexOf("TS;");
            if (tsPos < 0) {
                spiBuffer = "";
                spiFrameStartMs = 0;
                break;
            }
            if (tsPos > 0) {
                spiBuffer.remove(0, tsPos);
            }
            if (spiFrameStartMs == 0) {
                spiFrameStartMs = millis();
            }
            int tePos = spiBuffer.indexOf(";TE");
            if (tePos < 0) {
                if (spiFrameStartMs > 0 && (millis() - spiFrameStartMs) > 30) {
                    spiBuffer.remove(0, 3);
                    spiFrameStartMs = 0;
                    continue;
                }
                break;
            }
            String fullMsg = spiBuffer.substring(0, tePos + 3);
            feedLine(fullMsg);
            spiBuffer.remove(0, tePos + 3);
            spiFrameStartMs = 0;
        }
    }
}

/**
 * @brief                   Queue an SPI command or ACK.
 */
void InputronicParser::sendSpiCommand(const char *command) {
    if (!spiInitialized) {
        return;
    }
    if (strcmp(command, "ACK") == 0) {
        spiPendingAck = true;
    } else if (spiPendingCommand.length() == 0) {
        spiPendingCommand = command;
    }
}

/**
 * @brief                   Send a command over I2C.
 */
void InputronicParser::sendI2cCommand(const char *command) {
    Wire.beginTransmission(i2cSlaveAddr);
    Wire.write((const uint8_t *)command, strlen(command));
    Wire.endTransmission();
}

/**
 * @brief                   Interrupt configuration hook (currently unused).
 */
void InputronicParser::configureInterrupt(bool enable, int8_t pin, bool activeHigh) {
    (void)enable;
    (void)pin;
    (void)activeHigh;
}

/**
 * @brief                   Parse a single framed message.
 */
void InputronicParser::parseMessage(const String &msgIn) {
    String msg = msgIn;
    msg.trim();

    if (msg.indexOf("TS;MIDI;") >= 0) {
        parseMIDI(msg);
    } else if (!expectingHidRawOnly && msg.startsWith("TS;M;")) {
        parseMouse(msg);
    } else if (!expectingHidRawOnly && msg.startsWith("TS;K;")) {
        parseKeyboard(msg);
    } else if (msg.indexOf("TS;DESC;") >= 0) {
        parseDescriptor(msg);
    } else if (msg.indexOf("TS;HIDRAW;") >= 0) {
        parseHidRaw(msg);
        expectingHidRawOnly = false;
    }
}

/**
 * @brief                   Parse keyboard message payload.
 */
void InputronicParser::parseKeyboard(const String &msg) {
    int start = msg.indexOf(";K;") + 3;
    int end = msg.indexOf(";TE");
    if (start > 0 && end > start) {
        String keyStr = msg.substring(start, end);
        latest.keyboard.payload = keyStr;
        latest.keyboard.keyCount = 0;
        for (uint8_t i = 0; i < 8; i++) {
            latest.keyboard.keys[i] = "";
        }
        latest.keyboard.key = (keyStr.length() == 1) ? keyStr[0] : 0;
        for (int i = 0; i < keyStr.length() && latest.keyboard.keyCount < 8; ) {
            if (keyStr[i] == '<') {
                int end = keyStr.indexOf('>', i + 1);
                if (end > i) {
                    latest.keyboard.keys[latest.keyboard.keyCount++] = keyStr.substring(i, end + 1);
                    i = end + 1;
                    continue;
                }
            }
            latest.keyboard.keys[latest.keyboard.keyCount++] = String(keyStr[i]);
            i += 1;
        }
        if (latest.keyboard.keyCount == 0 && keyStr.length() == 1) {
            latest.keyboard.keys[latest.keyboard.keyCount++] = keyStr;
        }
        latest.keyboard.valid = true;
    }
}

/**
 * @brief                   Parse MIDI message payload.
 */
void InputronicParser::parseMIDI(const String &msgIn) {
    int start = msgIn.indexOf("TS;MIDI;");
    if (start < 0) {
        return;
    }
    String msg = msgIn.substring(start + 8);
    int parts[3] = {0};
    int idx = 0;
    String token = "";
    for (int i = 0; i < msg.length(); i++) {
        char c = msg[i];
        if (c == ';' || c == '\n' || c == '\r') {
            if (token.length() > 0 && idx < 3) {
                parts[idx++] = strtol(token.c_str(), nullptr, 16);
                token = "";
            }
            if (msg.startsWith("TE", i + 1)) {
                break;
            }
        } else {
            token += c;
        }
    }
    if (idx == 3) {
        latest.midi.b1 = parts[0];
        latest.midi.b2 = parts[1];
        latest.midi.b3 = parts[2];
        latest.midi.valid = true;
    }
}

/**
 * @brief                   Parse mouse message payload.
 */
void InputronicParser::parseMouse(const String &msgIn) {
    String msg = msgIn;
    msg.trim();

    int start = msg.indexOf(";M;") + 3;
    int end = msg.indexOf(";TE");
    if (end == -1) end = msg.length();

    String body = msg.substring(start, end);

    int vals[8] = {0};
    int idx = 0;
    String token = "";

    for (int i = 0; i < body.length(); i++) {
        char c = body[i];
        if (c == ';') {
            if (token.length() > 0 && idx < 8) {
                vals[idx++] = atoi(token.c_str());
                token = "";
            }
        } else {
            token += c;
        }
    }
    if (token.length() > 0 && idx < 8) {
        vals[idx++] = atoi(token.c_str());
    }

    if (idx >= 8) {
        latest.mouse.x = vals[0];
        latest.mouse.y = vals[1];
        latest.mouse.scroll = vals[2];
        latest.mouse.btnLeft = vals[3];
        latest.mouse.btnRight = vals[4];
        latest.mouse.btnMiddle = vals[5];
        latest.mouse.btnBackward = vals[6];
        latest.mouse.btnForward = vals[7];
        latest.mouse.valid = true;
    }
}

/**
 * @brief                   Parse descriptor message payload.
 */
void InputronicParser::parseDescriptor(const String &msg) {
    int start = msg.indexOf(";DESC;") + 6;
    int end = msg.indexOf(";TE");
    if (start > 5 && end > start) {
        latest.descriptor.hex = msg.substring(start, end);
        latest.descriptor.valid = true;
    }
}

/**
 * @brief                   Parse raw HID message payload.
 */
void InputronicParser::parseHidRaw(const String &msg) {
    int start = msg.indexOf(";HIDRAW;") + 8;
    int end = msg.indexOf(";TE");
    if (start > 7 && end > start) {
        String hex = msg.substring(start, end);
        if (hex.length() > 0 && hex != lastHidRawHex) {
            latest.hidRaw.hex = hex;
            latest.hidRaw.valid = true;
            lastHidRawHex = hex;
        }
    }
}
