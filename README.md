# OS-303 Processor
This is the firmware for a TB-303 CPU replacement, targeting Teensy 2.0++ using PlatformIO. It uses the Arduino-compatible Teensy framework, so using Arduino IDE to build it also works - just open the `src/src.ino` file.

## Hardware
It should run on any AT90USB1286 microcontroller board with compatible pinouts. The `src/pins.h` file contains the vital mappings between the Teensy pins and the TB-303 circuit pins; this could be edited/replaced for porting to a different chip. The driver code uses direct port manipulation for efficiency; more portable code using `digitalWriteFast()` is left commented out.

## Engine
A very basic sequencer implementation has been hacked together on top of the core drivers, with patterns saved to EEPROM. It is not a complete imitation of the original (yet, WIP) but serves as a good starting point and PoC. With basic familiar functions in place, there is an opportunity to remake the 303 sequencer as you see fit...

## Credits
Authored by Nicholas J. Michalek (Phazerville) in partnership with [Michigan Synth Works](https://michigansynthworks.com/).
