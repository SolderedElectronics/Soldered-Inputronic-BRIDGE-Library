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
 *              - Connect the BRIDGE interrupt output to the pin defined
 *                by INTERRUPT_PIN below.
 *              - The BRIDGE outputs an active-LOW pulse on GPIO 12 when
 *                in I2C mode and on GPIO 9 when in UART or SPI mode.
 *
 * @authors     Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include <Wire.h>
#include "Inputronic-BRIDGE.h"

// Parser instance used for all communication protocols.
InputronicParser parser;

// Pin on the receiving MCU connected to the BRIDGE interrupt output.
// Change this to match your wiring.
static const int8_t INTERRUPT_PIN = 5;

// Optional: this flag can be set by the ISR callback and checked in loop().
volatile bool newDataFlag = false;

// Optional user callback invoked from ISR context when the BRIDGE signals
// new data.  Keep it very short -- do not use Serial or blocking calls here.
void onBridgeDataReady()
{
    newDataFlag = true;
}

void setup()
{
    // USB serial for monitoring events in Serial Monitor.
    Serial.begin(115200);

    // --- Protocol selection ---
    // Uncomment ONE of the following blocks depending on which protocol
    // you are using between the BRIDGE and this MCU.

    // ---- I2C ----
    // The BRIDGE interrupt output is on GPIO 12 in I2C mode.
    //parser.configureI2c(0x50, 8, 9);
    //parser.begin(InputronicParser::PROTOCOL_I2C);

    // ---- UART ----
    // The BRIDGE interrupt output is on GPIO 9 in UART mode.
    //Serial1.begin(115200, SERIAL_8N1, 10, 12);
    //parser.begin(InputronicParser::PROTOCOL_UART);

    // ---- SPI ----
    // The BRIDGE interrupt output is on GPIO 9 in SPI mode.
    parser.begin(InputronicParser::PROTOCOL_SPI, 10);

    // Enable the interrupt pin.  This configures INTERRUPT_PIN as
    // INPUT_PULLUP and attaches an ISR on the FALLING edge so that
    // pollEvents() only reads the bus when the BRIDGE has new data.
    parser.enableInterruptPin(INTERRUPT_PIN);

    // Optionally register a callback that fires inside the ISR.
    parser.onDataReady(onBridgeDataReady);

    Serial.println("Inputronic BRIDGE interrupt example ready.");
}

void loop()
{
    // pollEvents() will return immediately without bus traffic unless
    // the interrupt flag has been set by the BRIDGE.
    auto events = parser.pollEvents();

    // Keyboard event: prints the received keys.
    if (events.keyboard.valid)
    {
        Serial.print("Keyboard: ");
        for (uint8_t i = 0; i < events.keyboard.keyCount; i++)
        {
            Serial.print(events.keyboard.keys[i]);
        }
        Serial.println();
    }

    // Mouse event: prints movement, buttons, and scroll.
    if (events.mouse.valid)
    {
        Serial.printf("Mouse X:%d Y:%d L:%d R:%d M:%d Scroll:%d\n",
                      events.mouse.x, events.mouse.y,
                      events.mouse.btnLeft, events.mouse.btnRight,
                      events.mouse.btnMiddle, events.mouse.scroll);
    }

    // MIDI event: prints three MIDI bytes in hex.
    if (events.midi.valid)
    {
        Serial.printf("MIDI %02X %02X %02X\n",
                      events.midi.b1, events.midi.b2, events.midi.b3);
    }

    // Alternatively, you can check the flag set by the ISR callback
    // to do additional work only when new data actually arrived.
    if (newDataFlag)
    {
        newDataFlag = false;
        // The events above already contain the latest data, so this
        // is just a demonstration of using the callback flag.
    }
}
