/**
 **************************************************
 *
 * @file        interruptEvents.ino
 * @brief       Example showing interrupt-driven event reading with the
 *              Inputronic BRIDGE parser.
 *
 *              Instead of polling the BRIDGE continuously, this sketch
 *              configures an interrupt pin so that the BRIDGE firmware
 *              signals the receiving MCU whenever new HID data is
 *              available.  The parser only performs a bus transaction
 *              when the interrupt fires, saving CPU time and bus
 *              bandwidth.
 *
 *              Hardware wiring:
 *              - Connect the BRIDGE interrupt output (GPIO 21) to the pin
 *                defined by INTERRUPT_PIN below.
 *
 * @authors     Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "Inputronic-BRIDGE.h"

InputronicParser parser;

// Pin on the receiving MCU connected to the BRIDGE interrupt output.
static const int8_t INTERRUPT_PIN = 5;

// Optional: flag set by the ISR callback and checked in loop().
volatile bool newDataFlag = false;

// Optional user callback invoked from ISR context when the BRIDGE signals
// new data.  Keep it very short — no Serial or blocking calls here.
void onBridgeDataReady()
{
    newDataFlag = true;
}

void setup()
{
    Serial.begin(115200);

    // --- Protocol selection ---
    // Uncomment ONE block depending on which protocol you wired up.

    // ---- I2C ----
    // Call Wire.begin() with your SDA/SCL pins, then pass Wire to begin().
    Wire.begin(8, 9);
    parser.configureI2c(0x50);
    if (!parser.begin(InputronicParser::PROTOCOL_I2C, Wire,
                      true, INTERRUPT_PIN, false)) // interrupt on FALLING edge
    {
        Serial.println("Could not connect to BRIDGE over I2C!");
        while (true);
    }

    // ---- UART ----
    //Serial1.begin(115200, SERIAL_8N1, 10, 12);
    //if (!parser.begin(InputronicParser::PROTOCOL_UART, Serial1,
    //                  true, INTERRUPT_PIN, false))
    //{
    //    Serial.println("Could not connect to BRIDGE over UART!");
    //    while (true);
    //}

    // ---- SPI ----
    //SPI.begin();
    //if (!parser.begin(InputronicParser::PROTOCOL_SPI, SPI, 10, 1000000,
    //                  true, INTERRUPT_PIN, false))
    //{
    //    Serial.println("Could not connect to BRIDGE over SPI!");
    //    while (true);
    //}

    // Optionally register a callback that fires inside the ISR.
    parser.onDataReady(onBridgeDataReady);

    Serial.println("BRIDGE connected — interrupt mode active.");
}

void loop()
{
    // pollEvents() returns immediately without bus traffic unless
    // the interrupt flag has been set by the BRIDGE.
    auto events = parser.pollEvents();

    if (events.keyboard.valid)
    {
        Serial.print("Keyboard: ");
        for (uint8_t i = 0; i < events.keyboard.keyCount; i++)
        {
            Serial.print(events.keyboard.keys[i]);
        }
        Serial.println();
    }

    if (events.mouse.valid)
    {
        Serial.printf("Mouse X:%d Y:%d L:%d R:%d M:%d Scroll:%d\n",
                      events.mouse.x, events.mouse.y,
                      events.mouse.btnLeft, events.mouse.btnRight,
                      events.mouse.btnMiddle, events.mouse.scroll);
    }

    if (events.midi.valid)
    {
        Serial.printf("MIDI %02X %02X %02X\n",
                      events.midi.b1, events.midi.b2, events.midi.b3);
    }

    if (newDataFlag)
    {
        newDataFlag = false;
        // events above already contain the latest data; this flag
        // can be used to trigger additional work on data arrival.
    }
}
