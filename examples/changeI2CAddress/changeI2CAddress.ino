/**
 **************************************************
 *
 * @file        changeI2CAddress.ino
 * @brief       Example showing how to change the I2C address of the
 *              Inputronic BRIDGE device and store it in EEPROM.
 *
 *              The new address is written to the device's non-volatile
 *              storage (NVS/EEPROM) and takes effect immediately — no
 *              power cycle required. The address is remembered across reboots.
 *
 *              Default address is 0x50. Valid range is 0x08–0x77.
 *
 *              Usage:
 *                1. Flash this sketch.
 *                2. Open Serial Monitor at 115200 baud.
 *                3. The sketch connects at the current address, changes it
 *                   to NEW_I2C_ADDR, then reconnects at the new address to
 *                   confirm it worked.
 *
 * @authors     Josip Šimun Kuči @ soldered.com
 ***************************************************/

#include "Inputronic-BRIDGE.h"

// Current address the device is responding on.
#define CURRENT_I2C_ADDR 0x50

// Address to change to. Modify this to your desired address (0x08–0x77).
#define NEW_I2C_ADDR 0x30

InputronicParser parser;

void setup()
{
    Serial.begin(115200);
    delay(500);

    Wire.begin(21, 22); // Adjust SDA/SCL pins for your board.

    // --- Connect at the current address ---
    parser.configureI2c(CURRENT_I2C_ADDR);
    if (!parser.begin(InputronicParser::PROTOCOL_I2C, Wire))
    {
        Serial.println("Could not connect to BRIDGE at address 0x" + String(CURRENT_I2C_ADDR, HEX));
        Serial.println("Check wiring and that CURRENT_I2C_ADDR matches the device.");
        while (true);
    }
    Serial.println("Connected at address 0x" + String(CURRENT_I2C_ADDR, HEX));

    // --- Send the address change command ---
    // The bridge writes the new address to EEPROM and reinitialises its I2C
    // slave driver immediately. A 50 ms delay is built into changeI2CAddress()
    // to let the device settle before the next transaction.
    Serial.println("Changing address to 0x" + String(NEW_I2C_ADDR, HEX) + "...");
    if (!parser.changeI2CAddress(NEW_I2C_ADDR))
    {
        Serial.println("changeI2CAddress() failed — check protocol.");
        while (true);
    }

    // --- Reconnect at the new address to verify ---
    if (!parser.begin(InputronicParser::PROTOCOL_I2C, Wire))
    {
        Serial.println("Could not reconnect at new address 0x" + String(NEW_I2C_ADDR, HEX));
        Serial.println("Address change may have failed.");
        while (true);
    }
    Serial.println("Success! BRIDGE now responds at address 0x" + String(NEW_I2C_ADDR, HEX));
    Serial.println("This address is stored in EEPROM and will survive power cycles.");
    Serial.println("Update CURRENT_I2C_ADDR in future sketches to 0x" + String(NEW_I2C_ADDR, HEX));
}

void loop()
{
    // Normal event polling works as usual after the address change.
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
}
