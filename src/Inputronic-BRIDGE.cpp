/**
 **************************************************
 *
 * @file        Inputronic-BRIDGE.cpp
 * @brief       Source file for Inputronic Bridge parser.
 *
 *
 * @copyright GNU General Public License v3.0
 * @authors   Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "Inputronic-BRIDGE.h"

volatile bool InputronicParser::interruptFlag = false;
void (*InputronicParser::userIsrCallback)() = nullptr;

/**
 * @brief                   Enable interrupt-driven event polling.
 */
void InputronicParser::enableInterruptPin(int8_t pin)
{
    if (pin < 0)
    {
        return;
    }
    interruptPin = pin;
    enableInterrupt = true;
    pinMode(interruptPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(interruptPin), isrHandler, FALLING);
}

/**
 * @brief                   Register a user callback for interrupt events.
 */
void InputronicParser::onDataReady(void (*callback)())
{
    userIsrCallback = callback;
}

/**
 * @brief                   ISR handler for interrupt pin.
 */
void IRAM_ATTR InputronicParser::isrHandler()
{
    interruptFlag = true;
    if (userIsrCallback != nullptr)
    {
        userIsrCallback();
    }
}

/**
 * @brief                   Initialise I2C mode.
 */
bool InputronicParser::begin(CommProtocol p, TwoWire &wire, bool enableInterruptParam, int8_t interruptPinParam,
                             bool activeHigh)
{
    protocol = p;
    i2cPort = &wire;
    configureInterrupt(enableInterruptParam, interruptPinParam, activeHigh);
    return checkConnection();
}

/**
 * @brief                   Initialise SPI mode.
 */
bool InputronicParser::begin(CommProtocol p, SPIClass &spi, uint8_t spiCs, uint32_t spiHz, bool enableInterruptParam,
                             int8_t interruptPinParam, bool activeHigh)
{
    protocol = p;
    spiPort = &spi;
    spiCsPin = spiCs;
    spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);
    configureInterrupt(enableInterruptParam, interruptPinParam, activeHigh);
    pinMode(spiCsPin, OUTPUT);
    digitalWrite(spiCsPin, HIGH);
    spiInitialized = true;
    return checkConnection();
}

/**
 * @brief                   Initialise UART mode.
 */
bool InputronicParser::begin(CommProtocol p, HardwareSerial &serial, bool enableInterruptParam,
                             int8_t interruptPinParam, bool activeHigh)
{
    protocol = p;
    uartPort = &serial;
    configureInterrupt(enableInterruptParam, interruptPinParam, activeHigh);
    return checkConnection();
}

/**
 * @brief                   Set the I2C slave address.
 */
void InputronicParser::configureI2c(uint8_t addr)
{
    i2cSlaveAddr = addr;
}

/**
 * @brief                   Request device descriptor on next poll.
 */
void InputronicParser::requestDescriptor()
{
    requestDescPending = true;
}

/**
 * @brief                   Request raw HID once on next poll.
 */
void InputronicParser::requestHidRawOnce()
{
    requestHidRawPending = true;
    expectingHidRawOnly = true;
}

/**
 * @brief                   Enable or disable raw HID polling.
 */
void InputronicParser::setHidRawPolling(bool enabled)
{
    pollHidRawEnabled = enabled;
    expectingHidRawOnly = enabled;
}

/**
 * @brief                   Enable or disable interrupt mode.
 */
void InputronicParser::setInterruptMode(bool enable, int8_t pin, bool activeHigh)
{
    enableInterrupt = enable;
    if (enable)
    {
        if (pin >= 0)
        {
            interruptPin = pin;
            pinMode(pin, INPUT);
            attachInterrupt(digitalPinToInterrupt(pin), isrHandler, activeHigh ? RISING : FALLING);
        }
    }
    else
    {
        if (interruptPin >= 0)
        {
            detachInterrupt(digitalPinToInterrupt(interruptPin));
        }
    }
}

/**
 * @brief                   Feed a raw line into the parser.
 */
void InputronicParser::feedLine(const String &line)
{
    parseMessage(line);
}

/**
 * @brief                   Send PING and wait for PONG to confirm the bridge is reachable.
 */
bool InputronicParser::checkConnection()
{
    switch (protocol)
    {
    case PROTOCOL_I2C: {
        if (!i2cPort)
        {
            return false;
        }
        for (int attempt = 0; attempt < 3; attempt++)
        {
            i2cPort->beginTransmission(i2cSlaveAddr);
            i2cPort->write((const uint8_t *)"PING", 4);
            if (i2cPort->endTransmission() != 0)
            {
                delay(50);
                continue;
            }
            uint32_t deadline = millis() + 50;
            while (millis() < deadline)
            {
                delay(5);
                i2cPort->requestFrom(i2cSlaveAddr, (uint8_t)48);
                uint8_t rawBuf[48] = {0};
                uint8_t rawLen = 0;
                while (i2cPort->available() && rawLen < 48)
                {
                    rawBuf[rawLen++] = i2cPort->read();
                }
                if (rawLen > 1)
                {
                    uint8_t payloadLen = rawBuf[0];
                    if (payloadLen > 0 && payloadLen < 48 && payloadLen <= (rawLen - 1))
                    {
                        String msg;
                        msg.reserve(payloadLen);
                        for (uint8_t i = 1; i <= payloadLen; i++)
                        {
                            msg += (char)rawBuf[i];
                        }
                        msg.trim();
                        if (msg == "TS;PONG;TE")
                        {
                            i2cPort->beginTransmission(i2cSlaveAddr);
                            i2cPort->write((const uint8_t *)"ACK", 3);
                            i2cPort->endTransmission();
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    case PROTOCOL_SPI: {
        if (!spiPort)
        {
            return false;
        }
        uint8_t txBuf[SPI_MAX_LEN] = {0};
        uint8_t rxBuf[SPI_MAX_LEN] = {0};

        // Transaction 1: send PING
        memcpy(txBuf, "PING", 4);
        spiPort->beginTransaction(spiSettings);
        digitalWrite(spiCsPin, LOW);
        spiPort->transferBytes(txBuf, rxBuf, SPI_MAX_LEN);
        digitalWrite(spiCsPin, HIGH);
        spiPort->endTransaction();

        // Give firmware time to process PING and queue PONG
        delay(50);

        // Transaction 2: read PONG
        memset(txBuf, 0, SPI_MAX_LEN);
        memset(rxBuf, 0, SPI_MAX_LEN);
        spiPort->beginTransaction(spiSettings);
        digitalWrite(spiCsPin, LOW);
        spiPort->transferBytes(txBuf, rxBuf, SPI_MAX_LEN);
        digitalWrite(spiCsPin, HIGH);
        spiPort->endTransaction();

        uint8_t payloadLen = rxBuf[0];
        if (payloadLen > 0 && payloadLen < SPI_MAX_LEN)
        {
            String msg;
            msg.reserve(payloadLen);
            for (uint8_t i = 0; i < payloadLen; i++)
            {
                msg += (char)rxBuf[i + 1];
            }
            if (msg == "TS;PONG;TE")
            {
                memset(txBuf, 0, SPI_MAX_LEN);
                memcpy(txBuf, "ACK", 3);
                spiPort->beginTransaction(spiSettings);
                digitalWrite(spiCsPin, LOW);
                spiPort->transferBytes(txBuf, rxBuf, SPI_MAX_LEN);
                digitalWrite(spiCsPin, HIGH);
                spiPort->endTransaction();
                return true;
            }
        }
        return false;
    }

    case PROTOCOL_UART: {
        if (!uartPort)
        {
            return false;
        }
        uartPort->print("PING\n");
        uint32_t deadline = millis() + 500;
        String buf;
        while (millis() < deadline)
        {
            while (uartPort->available())
            {
                buf += (char)uartPort->read();
            }
            if (buf.indexOf("TS;PONG;TE") >= 0)
            {
                return true;
            }
            delay(10);
        }
        return false;
    }
    }
    return false;
}

/**
 * @brief                   Poll for events and return any newly parsed data.
 */
InputronicParser::EventBundle InputronicParser::pollEvents()
{
    if (protocol == PROTOCOL_UART)
    {
        if (!uartPort)
        {
            return latest;
        }

        noInterrupts();
        bool flagWasSet = interruptFlag;
        if (flagWasSet)
            interruptFlag = false;
        interrupts();

        if (enableInterrupt && !flagWasSet)
        {
            EventBundle out = latest;
            latest.keyboard.valid = false;
            latest.mouse.valid = false;
            latest.midi.valid = false;
            latest.descriptor.valid = false;
            latest.hidRaw.valid = false;
            return out;
        }

        static String uartBuffer = "";

        while (uartPort->available())
        {
            uartBuffer += (char)uartPort->read();
        }

        if (uartBuffer.length() > 512)
        {
            int lastTs = uartBuffer.lastIndexOf("TS;");
            if (lastTs >= 0)
                uartBuffer = uartBuffer.substring(lastTs);
            else
                uartBuffer = "";
        }

        int tePos;
        while ((tePos = uartBuffer.indexOf(";TE")) != -1)
        {
            String fullMsg = uartBuffer.substring(0, tePos + 3);
            feedLine(fullMsg);
            uartBuffer.remove(0, tePos + 3);
            uartBuffer.trim();
        }
    }
    else if (protocol == PROTOCOL_I2C)
    {
        if (!i2cPort)
        {
            return latest;
        }

        const uint8_t MAX_LEN = 48;

        noInterrupts();
        bool flagWasSet = interruptFlag;
        if (flagWasSet)
            interruptFlag = false;
        interrupts();

        if (enableInterrupt && !flagWasSet && !requestDescPending && !requestHidRawPending)
        {
            EventBundle out = latest;
            latest.keyboard.valid = false;
            latest.mouse.valid = false;
            latest.midi.valid = false;
            latest.descriptor.valid = false;
            latest.hidRaw.valid = false;
            return out;
        }

        if (!enableInterrupt)
        {
            if (requestDescPending)
            {
                sendI2cCommand("REQ:DESC");
                requestDescPending = false;
            }
            if (requestHidRawPending || pollHidRawEnabled)
            {
                sendI2cCommand("REQ:HIDRAW");
                requestHidRawPending = false;
            }
        }

        i2cPort->requestFrom(i2cSlaveAddr, (uint8_t)MAX_LEN);

        // Each I2C read carries exactly one length-prefixed message.
        // Process it as a standalone unit — no accumulation across reads.
        bool gotData = false;

        uint8_t rawBuf[MAX_LEN] = {0};
        uint8_t rawLen = 0;
        while (i2cPort->available() && rawLen < MAX_LEN)
        {
            rawBuf[rawLen++] = i2cPort->read();
        }

        if (rawLen > 1)
        {
            uint8_t payloadLen = rawBuf[0];
            if (payloadLen > 0 && payloadLen < MAX_LEN && payloadLen <= (rawLen - 1))
            {
                String msg;
                msg.reserve(payloadLen);
                for (uint8_t idx = 1; idx <= payloadLen; idx++)
                {
                    msg += (char)rawBuf[idx];
                }
                msg.trim();
                if (msg.startsWith("TS;") && msg.endsWith(";TE"))
                {
                    feedLine(msg);
                }
                gotData = true;
            }
        }

        if (gotData)
        {
            i2cPort->beginTransmission(i2cSlaveAddr);
            i2cPort->write((const uint8_t *)"ACK", 3);
            i2cPort->endTransmission();
        }
    }
    else if (protocol == PROTOCOL_SPI)
    {
        noInterrupts();
        bool flagWasSet = interruptFlag;
        if (flagWasSet)
            interruptFlag = false;
        interrupts();

        if (enableInterrupt && !flagWasSet && !requestDescPending && !requestHidRawPending)
        {
            EventBundle out = latest;
            latest.keyboard.valid = false;
            latest.mouse.valid = false;
            latest.midi.valid = false;
            latest.descriptor.valid = false;
            latest.hidRaw.valid = false;
            return out;
        }

        if (enableInterrupt && flagWasSet)
        {
            // Wait for the SPI slave to enter its blocking receive after
            // pulsing the interrupt pin.  The firmware needs ~25 us
            // (20 us pulse + a few us to call spi_slave_transmit).
            delayMicroseconds(50);
        }

        if (!enableInterrupt)
        {
            if (requestDescPending)
            {
                sendSpiCommand("REQ:DESC");
                requestDescPending = false;
            }
            if (requestHidRawPending || pollHidRawEnabled)
            {
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
void InputronicParser::pollSpi()
{
    if (!spiInitialized || !spiPort)
    {
        return;
    }

    uint8_t txBuf[SPI_MAX_LEN] = {0};
    uint8_t rxBuf[SPI_MAX_LEN] = {0};
    if (spiPendingAck)
    {
        const char *ack = "ACK";
        size_t len = strlen(ack);
        if (len > SPI_MAX_LEN)
        {
            len = SPI_MAX_LEN;
        }
        memcpy(txBuf, ack, len);
        spiPendingAck = false;
    }
    else if (spiPendingCommand.length() > 0)
    {
        size_t len = spiPendingCommand.length();
        if (len > SPI_MAX_LEN)
        {
            len = SPI_MAX_LEN;
        }
        memcpy(txBuf, spiPendingCommand.c_str(), len);
        spiPendingCommand = "";
    }

    spiPort->beginTransaction(spiSettings);
    digitalWrite(spiCsPin, LOW);
    spiPort->transferBytes(txBuf, rxBuf, SPI_MAX_LEN);
    digitalWrite(spiCsPin, HIGH);
    spiPort->endTransaction();

    uint8_t payloadLen = rxBuf[0];
    String msg = "";
    bool hasData = (payloadLen > 0 && payloadLen < SPI_MAX_LEN);

    if (hasData)
    {
        for (uint8_t i = 0; i < payloadLen; i++)
        {
            msg += static_cast<char>(rxBuf[i + 1]);
        }
        spiPendingAck = true;
    }
    if (msg.length() > 0)
    {
        static String spiBuffer = "";
        spiBuffer += msg;

        const size_t maxSpiBuffer = 256;
        if (spiBuffer.length() > maxSpiBuffer)
        {
            int lastTs = spiBuffer.lastIndexOf("TS;");
            if (lastTs >= 0)
            {
                spiBuffer = spiBuffer.substring(lastTs);
            }
            else
            {
                spiBuffer = "";
            }
        }

        while (true)
        {
            int tsPos = spiBuffer.indexOf("TS;");
            if (tsPos < 0)
            {
                spiBuffer = "";
                spiFrameStartMs = 0;
                break;
            }
            if (tsPos > 0)
            {
                spiBuffer.remove(0, tsPos);
            }
            if (spiFrameStartMs == 0)
            {
                spiFrameStartMs = millis();
            }
            int tePos = spiBuffer.indexOf(";TE");
            if (tePos < 0)
            {
                if (spiFrameStartMs > 0 && (millis() - spiFrameStartMs) > 30)
                {
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
void InputronicParser::sendSpiCommand(const char *command)
{
    if (!spiInitialized)
    {
        return;
    }
    if (strcmp(command, "ACK") == 0)
    {
        spiPendingAck = true;
    }
    else if (spiPendingCommand.length() == 0)
    {
        spiPendingCommand = command;
    }
}

/**
 * @brief                   Send a command over I2C.
 */
void InputronicParser::sendI2cCommand(const char *command)
{
    if (!i2cPort)
    {
        return;
    }
    i2cPort->beginTransmission(i2cSlaveAddr);
    i2cPort->write((const uint8_t *)command, strlen(command));
    i2cPort->endTransmission();
}

void InputronicParser::configureInterrupt(bool enable, int8_t pin, bool activeHigh)
{
    if (!enable || pin < 0)
    {
        return;
    }
    interruptPin = pin;
    enableInterrupt = true;
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), isrHandler, activeHigh ? RISING : FALLING);
}

/**
 * @brief                   Parse a single framed message.
 */
void InputronicParser::parseMessage(const String &msgIn)
{
    String msg = msgIn;
    msg.trim();

    if (msg.indexOf("TS;MIDI;") >= 0)
    {
        parseMIDI(msg);
    }
    else if (!expectingHidRawOnly && msg.startsWith("TS;M;"))
    {
        parseMouse(msg);
    }
    else if (!expectingHidRawOnly && msg.startsWith("TS;K;"))
    {
        parseKeyboard(msg);
    }
    else if (msg.indexOf("TS;DESC;") >= 0)
    {
        parseDescriptor(msg);
    }
    else if (msg.indexOf("TS;HIDRAW;") >= 0)
    {
        parseHidRaw(msg);
        expectingHidRawOnly = false;
    }
}

/**
 * @brief                   Parse keyboard message payload.
 */
void InputronicParser::parseKeyboard(const String &msg)
{
    int kPos = msg.indexOf(";K;");
    int end = msg.indexOf(";TE");
    if (kPos == -1 || end == -1 || end <= kPos)
    {
        return;
    }
    int start = kPos + 3;
    if (end > start)
    {
        String keyStr = msg.substring(start, end);
        keyStr.replace("\\;", ";");
        latest.keyboard.payload = keyStr;
        latest.keyboard.keyCount = 0;
        for (uint8_t i = 0; i < 8; i++)
        {
            latest.keyboard.keys[i] = "";
        }
        latest.keyboard.key = (keyStr.length() == 1) ? keyStr[0] : 0;
        for (int i = 0; i < keyStr.length() && latest.keyboard.keyCount < 8;)
        {
            if (keyStr[i] == '<')
            {
                int closeAngle = keyStr.indexOf('>', i + 1);
                if (closeAngle > i)
                {
                    latest.keyboard.keys[latest.keyboard.keyCount++] = keyStr.substring(i, closeAngle + 1);
                    i = closeAngle + 1;
                    continue;
                }
            }
            if (latest.keyboard.keyCount < 8)
            {
                latest.keyboard.keys[latest.keyboard.keyCount++] = String(keyStr[i]);
            }
            i += 1;
        }
        if (latest.keyboard.keyCount == 0 && keyStr.length() == 1)
        {
            latest.keyboard.keys[latest.keyboard.keyCount++] = keyStr;
        }
        latest.keyboard.valid = true;
    }
}

/**
 * @brief                   Parse MIDI message payload.
 *
 * The firmware accumulates multiple MIDI events per USB transfer into a single
 * packet: TS;MIDI;b1;b2;b3|b1;b2;b3|...;TE  Only the first event is stored in
 * the struct; the struct can be extended in a future revision to hold all events.
 */
void InputronicParser::parseMIDI(const String &msgIn)
{
    int start = msgIn.indexOf("TS;MIDI;");
    if (start < 0)
    {
        return;
    }
    String payload = msgIn.substring(start + 8);
    int tePos = payload.indexOf(";TE");
    if (tePos >= 0)
    {
        payload = payload.substring(0, tePos);
    }

    int pipePos = payload.indexOf('|');
    String firstEvent = (pipePos >= 0) ? payload.substring(0, pipePos) : payload;

    int parts[3] = {0};
    int idx = 0;
    String token = "";
    for (int i = 0; i < firstEvent.length() && idx < 3; i++)
    {
        char c = firstEvent[i];
        if (c == ';')
        {
            if (token.length() > 0)
            {
                parts[idx++] = (int)strtol(token.c_str(), nullptr, 16);
                token = "";
            }
        }
        else
        {
            token += c;
        }
    }
    if (token.length() > 0 && idx < 3)
    {
        parts[idx++] = (int)strtol(token.c_str(), nullptr, 16);
    }

    if (idx == 3)
    {
        latest.midi.b1 = parts[0];
        latest.midi.b2 = parts[1];
        latest.midi.b3 = parts[2];
        latest.midi.valid = true;
    }
}

/**
 * @brief                   Parse mouse message payload.
 */
void InputronicParser::parseMouse(const String &msgIn)
{
    String msg = msgIn;
    msg.trim();

    int mPos = msg.indexOf(";M;");
    int end = msg.indexOf(";TE");
    if (mPos == -1 || end == -1 || end <= mPos)
    {
        return;
    }
    int start = mPos + 3;

    String body = msg.substring(start, end);

    int vals[9] = {0};
    int idx = 0;
    String token = "";

    for (int i = 0; i < body.length(); i++)
    {
        char c = body[i];
        if (c == ';')
        {
            if (token.length() > 0 && idx < 9)
            {
                vals[idx++] = atoi(token.c_str());
                token = "";
            }
        }
        else
        {
            token += c;
        }
    }
    if (token.length() > 0 && idx < 9)
    {
        vals[idx++] = atoi(token.c_str());
    }

    if (idx >= 9)
    {
        latest.mouse.x = vals[0];
        latest.mouse.y = vals[1];
        latest.mouse.scroll = vals[2];
        latest.mouse.btnLeft = vals[3];
        latest.mouse.btnRight = vals[4];
        latest.mouse.btnMiddle = vals[5];
        latest.mouse.btnBackward = vals[6];
        latest.mouse.btnForward = vals[7];
        latest.mouse.btnScrollWheel = vals[8];
        latest.mouse.valid = true;
    }
}

/**
 * @brief                   Parse descriptor message payload.
 */
void InputronicParser::parseDescriptor(const String &msg)
{
    int dPos = msg.indexOf(";DESC;");
    int end = msg.indexOf(";TE");
    if (dPos == -1 || end == -1 || end <= dPos)
    {
        return;
    }
    int start = dPos + 6;
    if (end > start)
    {
        latest.descriptor.hex = msg.substring(start, end);
        latest.descriptor.valid = true;
    }
}

/**
 * @brief                   Parse raw HID message payload.
 */
void InputronicParser::parseHidRaw(const String &msg)
{
    int hPos = msg.indexOf(";HIDRAW;");
    int end = msg.indexOf(";TE");
    if (hPos == -1 || end == -1 || end <= hPos)
    {
        return;
    }
    int start = hPos + 8;
    if (end > start)
    {
        String hex = msg.substring(start, end);
        if (hex.length() > 0)
        {
            latest.hidRaw.hex = hex;
            latest.hidRaw.valid = true;
        }
    }
}
