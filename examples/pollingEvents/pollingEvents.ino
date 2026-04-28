/**
 **************************************************
 *
 * @file        pollingEvents.ino
 * @brief       Example showing how to poll and print Inputronic events.
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
    parser.configureI2c(0x50); // I2C address (default 0x50, optional)
    if (!parser.begin(InputronicParser::PROTOCOL_I2C, Wire))
    {
        Serial.println("Could not connect to BRIDGE over I2C!");
        while (true);
    }

    // ---- UART ----
    // Call Serial1.begin() with your baud rate and RX/TX pins first.
    //Serial1.begin(115200, SERIAL_8N1, 14, 15);
    //if (!parser.begin(InputronicParser::PROTOCOL_UART, Serial1))
    //{
    //    Serial.println("Could not connect to BRIDGE over UART!");
    //    while (true);
    //}

    // ---- SPI ----
    // Call SPI.begin() first, then pass SPI and the CS pin to begin().
    //SPI.begin();
    //if (!parser.begin(InputronicParser::PROTOCOL_SPI, SPI, 5))
    //{
    //    Serial.println("Could not connect to BRIDGE over SPI!");
    //    while (true);
    //}

    Serial.println("BRIDGE connected.");
}

void loop()
{
    auto events = parser.pollEvents();

    if (events.keyboard.valid)
    {
        for (uint8_t i = 0; i < events.keyboard.keyCount; i++)
        {
            Serial.print(events.keyboard.keys[i]);
        }
        Serial.println();
    }

    if (events.mouse.valid)
        Serial.printf("Mouse X:%d Y:%d L:%d R:%d Scroll:%d\n",
                      events.mouse.x, events.mouse.y, events.mouse.btnLeft,
                      events.mouse.btnRight, events.mouse.scroll);

    if (events.midi.valid)
        Serial.printf("MIDI %02X %02X %02X\n",
                      events.midi.b1, events.midi.b2, events.midi.b3);
}
