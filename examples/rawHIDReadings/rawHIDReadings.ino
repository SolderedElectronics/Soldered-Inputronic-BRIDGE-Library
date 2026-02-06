/**
 **************************************************
 *
 * @file        rawHIDReadings.ino
 * @brief       Example showing how to read raw HID reports.
 *
 *
 *
 * @authors     Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include <Wire.h>
#include "Inputronic-BRIDGE.h"

// Parser instance used for all communication protocols.
InputronicParser parser;

void setup()
{
    // USB serial for monitoring raw HID output.
    Serial.begin(115200);

    // Uncomment the protocol you want to use:
    // - UART uses Serial1.
    // - I2C uses Wire (handled internally by the parser).
    // - SPI uses the default SPI bus.

    // UART settings (TX/RX pins depend on your board):
    //Serial1.begin(115200, SERIAL_8N1, 14, 15);
    //parser.begin(InputronicParser::PROTOCOL_UART);

    // I2C settings (device address and optional SDA/SCL pins):
    parser.configureI2c(0x50, 21, 22);
    parser.begin(InputronicParser::PROTOCOL_I2C);

    // SPI settings (CS pin example):
    //parser.begin(InputronicParser::PROTOCOL_SPI, 5);

    // Request raw HID on every poll.
    parser.setHidRawPolling(true);
}

void loop()
{
    // Polling reads incoming data and returns any newly parsed events.
    auto events = parser.pollEvents();

    if (events.hidRaw.valid)
    {
        // HID RAW is a hex string representing the report bytes.
        Serial.print("HID RAW HEX: ");
        Serial.println(events.hidRaw.hex);
    }

    delay(30);
}
