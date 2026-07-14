# Pattern Saving and MIDI Clock

This section describes when OS-303 stores pattern changes and how external MIDI clock affects playback and saving.

## Pattern memory

Patterns are edited in working memory while the unit is powered on. Changed pattern data is copied to non-volatile EEPROM only when a save event occurs. Removing power is not itself a save event.

A save writes the complete currently loaded pattern bank, including every changed pattern in that bank. Data that has not changed is not rewritten at the EEPROM byte level.

The following operations count as pattern changes:

- Entering or changing pitch and time steps
- Changing accent, slide, octave, or pattern length
- Clearing or pasting a pattern
- Generating random pitch or time data
- Bulk-clearing notes, rests, ties, ratchets, accents, or slides
- Editing or clearing a track chain

## When patterns are saved

In Write mode, the current bank is saved when any of these events occurs:

- The clock is stopped with the unit's RUN control
- MIDI Stop is received while MIDI clock receive is enabled
- The mode selector is changed from Write to Play while the clock is already stopped
- A different track or bank is loaded while Write mode is active

Saving is intentionally performed while the sequencer is stopped where possible, because an EEPROM operation during playback could disturb timing.

### Safe power-down procedure

After editing patterns:

1. Stop the sequencer, either with RUN or by sending MIDI Stop.
2. If desired, switch from Write to Play as an additional save point.
3. Wait briefly for the save operation to finish before disconnecting power.

Do not remove power while actively editing or running unless a save event has already occurred. The presence or absence of batteries does not change the save mechanism; retained patterns are stored in EEPROM.

## MIDI clock and transport

MIDI clock uses the standard 24 clock pulses per quarter note (24 PPQN).

When MIDI clock receive is enabled:

- **Clock** supplies sequencer timing.
- **Start** starts playback from the beginning and resets the sequencer position.
- **Continue** starts the sequencer using the same reset behavior as Start in the current firmware.
- **Stop** stops playback, lowers the gate, resets playback state, and saves pending pattern changes when the unit is in Write mode.

Once MIDI Clock messages are being received, they take priority over the analog/DIN clock input. After MIDI Stop, the firmware leaves MIDI-clock mode and the analog/DIN clock input can be used again.

When MIDI clock transmit is enabled:

- Incoming MIDI Clock, Start, Continue, and Stop messages are forwarded to MIDI output when MIDI clock receive is active.
- With no active MIDI clock input, analog/DIN clock pulses are converted to MIDI Clock messages.
- The unit's RUN control sends MIDI Start and MIDI Stop.

This allows the unit to follow an external MIDI master, forward that transport to another device, or act as a MIDI clock source when driven by its own RUN control and analog/DIN clock.

## Troubleshooting unsaved patterns

If a pattern is missing after power-up, check the following:

- Confirm the edit was made in Write mode.
- Stop playback before powering down.
- Confirm the external device sends a real MIDI Stop message, not merely an end to Clock messages.
- Confirm MIDI clock receive is enabled if relying on MIDI Stop as the save event.
- After extensive edits, switch Write to Play while stopped before removing power.

Merely ceasing to send MIDI Clock pulses is not equivalent to sending MIDI Stop. Without a Stop message or another save event, the unit cannot know that the external transport has stopped.
