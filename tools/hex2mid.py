#!/usr/bin/env python3

import sys
from intelhex import IntelHex
import mido

PAGE_SIZE = 256

MFR_ID = 0x7D
CMD_WRITE_PAGE = 0x01

PAGE_DELAY_MS = 5
TICKS_PER_BEAT = 480
TEMPO = 500000  # 120 BPM


def pack_7bit(data):
    out = []
    i = 0

    while i < len(data):
        chunk = data[i:i+7]
        msb = 0

        for bit, b in enumerate(chunk):
            if b & 0x80:
                msb |= (1 << bit)

        out.append(msb)

        for b in chunk:
            out.append(b & 0x7F)

        i += 7

    return out


def build_sysex(page, page_data):

    encoded = pack_7bit(page_data)

    payload = [
        MFR_ID,
        CMD_WRITE_PAGE,
        (page >> 8) & 0x7F,
        page & 0x7F,
        len(encoded) & 0x7F
    ]

    payload.extend(encoded)

    return payload


def ms_to_ticks(ms):
    beats = (ms / 1000.0) * (1000000 / TEMPO)
    return int(beats * TICKS_PER_BEAT)


def main():

    if len(sys.argv) < 3:
        print("usage: hex2mid firmware.hex firmware.mid")
        return

    ih = IntelHex(sys.argv[1])

    maxaddr = ih.maxaddr()
    pages = (maxaddr + PAGE_SIZE) // PAGE_SIZE

    midi = mido.MidiFile(ticks_per_beat=TICKS_PER_BEAT)
    track = mido.MidiTrack()
    midi.tracks.append(track)

    track.append(mido.MetaMessage("set_tempo", tempo=TEMPO, time=0))

    delay_ticks = ms_to_ticks(PAGE_DELAY_MS)

    for page in range(pages):

        addr = page * PAGE_SIZE
        data = ih.tobinarray(start=addr, size=PAGE_SIZE)

        payload = build_sysex(page, data)

        track.append(
            mido.Message(
                "sysex",
                data=payload,
                time=delay_ticks
            )
        )

    midi.save(sys.argv[2])

    print("Generated MIDI file with", pages, "pages")


if __name__ == "__main__":
    main()
