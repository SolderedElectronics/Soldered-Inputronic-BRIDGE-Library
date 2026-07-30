# Soldered Inputronic BRIDGE Arduino library

[![Make docs and publish to GitHub Pages](https://github.com/SolderedElectronics/Soldered-Inputronic-BRIDGE-Library/actions/workflows/make_docs.yml/badge.svg?branch=dev)](https://github.com/SolderedElectronics/Soldered-Inputronic-BRIDGE-Library/actions/workflows/make_docs.yml)
[![Arduino Library Manager Compatibility](https://github.com/SolderedElectronics/Soldered-Inputronic-BRIDGE-Library/actions/workflows/arduino_lint.yml/badge.svg?branch=dev)](https://github.com/SolderedElectronics/Soldered-Inputronic-BRIDGE-Library/actions/workflows/arduino_lint.yml)

| ![Product name](https://soldered.com/cdn/shop/files/333390_featured-photo_79e754_e227bf04-7da8-472b-98bb-fcad67e0f94f.png) |
| :------------------------------------------------------------------------------------: |
|                      [Soldered Inputronic BRIDGE](https://www.solde.red/333390)                      |

Inputronic BRIDGE is built around the ESP32-S3 and is designed for reading USB HID device data and forwarding it to your microcontroller. It's used to connect a USB keyboard, mouse, or MIDI controller to any project without implementing a USB host stack on your Arduino or ESP32 side. An onboard boost converter generates 5V for the USB-A port from the 3.3V supply, with overcurrent protection limiting the connected device to 260 mA.

It communicates over I2C, UART, or SPI, selectable via onboard jumpers. The default I2C address is 0x50. Two Qwiic connectors allow tool-free I2C connection, and a 14-pin header exposes SPI, UART, I2C, interrupt, and reset pins. Events can be read by polling or via interrupt. We have made this board open-source, so all hardware design files are available for you to inspect or adapt. Our technical support is also there if you need help getting started.

It works with the Arduino IDE using the Soldered Arduino library. At 26 x 63 mm, with two mounting holes for M3 screws, it fits easily into any build. Full setup and usage instructions are available in the Soldered documentation.

### Repository Contents

- **/src** - source files for the library (.h & .cpp)
- **/examples** - examples for using the library
- **_other_** - _keywords_ file highlights function words in your IDE, _library.properties_ enables implementation with Arduino Library Manager.

### Hardware design

You can find hardware design for this board in Soldered Inputronic BRIDGE hardware repository.

### Documentation

Access Arduino library documentation [here](https://docs.soldered.com/inputronic-bridge/overview/).

- Tutorial for using the NAZIV PROIZVODA board
- Installing an Arduino library

### Board compatibility

The library is compatible with board & microcontroller families shown in green below:

[![Compile Sketches](http://github-actions.40ants.com/SolderedElectronics/Soldered-Generic-Arduino-Library/matrix.svg?branch=dev&only=Compile%20Sketches)](https://github.com/SolderedElectronics/Soldered-Inputronic-BRIDGE-Library/actions/workflows/compile_test.yml)

### About Soldered

<img src="https://raw.githubusercontent.com/SolderedElectronics/Soldered-Generic-Arduino-Library/dev/extras/Soldered-logo-color.png" alt="soldered-logo" width="500"/>

At Soldered, we design and manufacture a wide selection of electronic products to help you turn your ideas into acts and bring you one step closer to your final project. Our products are intented for makers and crafted in-house by our experienced team in Osijek, Croatia. We believe that sharing is a crucial element for improvement and innovation, and we work hard to stay connected with all our makers regardless of their skill or experience level. Therefore, all our products are open-source. Finally, we always have your back. If you face any problem concerning either your shopping experience or your electronics project, our team will help you deal with it, offering efficient customer service and cost-free technical support anytime. Some of those might be useful for you:

- [Web Store](https://www.soldered.com/shop)
- [Tutorials & Projects](https://soldered.com/learn)
- [Community & Technical support](https://soldered.com/community)

### Open-source license

Soldered invests vast amounts of time into hardware & software for these products, which are all open-source. Please support future development by buying one of our products.

Check license details in the LICENSE file. Long story short, use these open-source files for any purpose you want to, as long as you apply the same open-source licence to it and disclose the original source. No warranty - all designs in this repository are distributed in the hope that they will be useful, but without any warranty. They are provided "AS IS", therefore without warranty of any kind, either expressed or implied. The entire quality and performance of what you do with the contents of this repository are your responsibility. In no event, Soldered (TAVU) will be liable for your damages, losses, including any general, special, incidental or consequential damage arising out of the use or inability to use the contents of this repository.

## Have fun!

And thank you from your fellow makers at Soldered Electronics.
