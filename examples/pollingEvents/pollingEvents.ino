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

#include <Wire.h>
#include "Inputronic-BRIDGE.h"

// Parser instance used for all communication protocols.
InputronicParser parser;

void setup()
{
    // USB serial for monitoring events in Serial Monitor.
    Serial.begin(115200);

    // Uncomment the protocol you want to use:
    // - UART uses Serial1.
    // - I2C uses Wire (handled internally by the parser).
    // - SPI uses the default SPI bus.

    // UART settings (TX/RX pins depend on your board):
    //Serial1.begin(115200, SERIAL_8N1, 14, 15);

    // I2C settings (device address and optional SDA/SCL pins):
    parser.configureI2c(0x50, 21, 22);
    parser.begin(InputronicParser::PROTOCOL_I2C);

    // SPI settings (CS pin example):
    //parser.begin(InputronicParser::PROTOCOL_SPI, 5);
}

void loop()
{
    // Polling reads incoming data and returns any newly parsed events.
    auto events = parser.pollEvents();

    // Keyboard event: prints the received keys.
    if (events.keyboard.valid)
    {
        for (uint8_t i = 0; i < events.keyboard.keyCount; i++)
        {
            Serial.print(events.keyboard.keys[i]);
        }
    }

    // Mouse event: prints movement, buttons, and scroll.
    if (events.mouse.valid)
        Serial.printf("Mouse X:%d Y:%d L:%d R:%d Scroll:%d\n",
                      events.mouse.x, events.mouse.y, events.mouse.btnLeft,
                      events.mouse.btnRight, events.mouse.scroll);

    // MIDI event: prints three MIDI bytes in hex.
    if (events.midi.valid)
        Serial.printf("MIDI %02X %02X %02X\n",
                      events.midi.b1, events.midi.b2, events.midi.b3);
}

