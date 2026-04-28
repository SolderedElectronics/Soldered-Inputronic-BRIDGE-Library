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

#include "Inputronic-BRIDGE.h"

InputronicParser parser;

void setup()
{
    Serial.begin(115200);

    // --- Protocol selection ---
    // Uncomment ONE block depending on which protocol you wired up.

    // ---- I2C ----
    // Call Wire.begin() with your SDA/SCL pins, then pass Wire to begin().
    Wire.begin(21, 22);
    parser.configureI2c(0x50);
    if (!parser.begin(InputronicParser::PROTOCOL_I2C, Wire))
    {
        Serial.println("Could not connect to BRIDGE over I2C!");
        while (true);
    }

    // ---- UART ----
    //Serial1.begin(115200, SERIAL_8N1, 14, 15);
    //if (!parser.begin(InputronicParser::PROTOCOL_UART, Serial1))
    //{
    //    Serial.println("Could not connect to BRIDGE over UART!");
    //    while (true);
    //}

    // ---- SPI ----
    //SPI.begin();
    //if (!parser.begin(InputronicParser::PROTOCOL_SPI, SPI, 5))
    //{
    //    Serial.println("Could not connect to BRIDGE over SPI!");
    //    while (true);
    //}

    Serial.println("BRIDGE connected.");

    // Push raw HID bytes on every poll.
    parser.setHidRawPolling(true);
}

void loop()
{
    auto events = parser.pollEvents();

    if (events.hidRaw.valid)
    {
        Serial.print("HID RAW HEX: ");
        Serial.println(events.hidRaw.hex);
    }

    delay(30);
}
