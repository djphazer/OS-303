# OS-303 Processor
This is the firmware for a TB-303 CPU replacement, targeting Teensy++ 2.0 using PlatformIO. It uses the Arduino-compatible Teensy framework, so using Arduino IDE to build it also works - just open the `src/src.ino` file. Make sure you have the Teensy libraries installed in Boards Manager, select "Teensy++ 2.0" and set CPU Speed to 4 MHz.

## Hardware
It should run on any AT90USB1286 microcontroller board with compatible pinouts, like the [MSW V0X DEI](https://michigansynthworks.com/products/os-303). The `src/pins.h` file contains the vital mappings between the Teensy pins and the TB-303 circuit pins; this could be edited/replaced for porting to a different chip. The driver code uses direct port manipulation for efficiency; more portable code using `digitalWriteFast()` is left commented out.

## Engine
A basic sequencer implementation has been hacked together on top of the core drivers, with patterns saved to EEPROM. It tries to be a faithful imitation of the original, but without as many limitations, plus some extra features. With this foundation in place, there is an opportunity to remake the 303 sequencer as you see fit...

## Update via MIDI SysEx
A basic bootloader enables updates via MIDI. Hold the "Write/Next/Tap" button on startup to enter the bootloader; 4 LEDs will light up solid - PITCH, TIME, FUNCTION, and A#. Use a program like Sysex Librarian or MIDI-OX to send the "update.syx" file from a PC, taking care to throttle the send speed... (if it fails, you might need to send it slower!)

## Operation

See [Pattern Saving and MIDI Clock](docs/pattern-saving-and-midi-clock.md) for when edits are written to memory, safe power-down practice, and MIDI clock transport behavior.

## Development
Building the firmware primarily uses the [PlatformIO](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html) toolchain. Run `pio run` from the repo root; the build process produces a `*.syx` file for use with the MIDI bootloader. You might have to run `pip install intelhex` once locally.

If you fork the repo and _enable GitHub Actions workflows_ on your fork, the server will build it for you when code changes are pushed. Anyone logged in to GH can [download the artifact files](https://github.com/djphazer/OS-303/actions/workflows/firmware.yml).

More info for developers will be added to [the Wiki](https://github.com/djphazer/OS-303/wiki).

## Credits
Authored by Nicholas J. Michalek (Phazerville) in partnership with [Michigan Synth Works](https://michigansynthworks.com/).
