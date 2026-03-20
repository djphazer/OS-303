#!/usr/bin/env python3

import sys
from intelhex import IntelHex

PAGE_SIZE = 256
SYSEX_START = 0xF0
SYSEX_END = 0xF7
MFR_ID = 0x7D

CMD_WRITE_PAGE = 0x01
CMD_EXECUTE = 0x02

def pack_7bit(data):
    """
    Convert 8-bit data into MIDI 7-bit packed format.
    7 bytes → 8 bytes
    """
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
    length = len(encoded)
    checksum = 0
    for b in page_data:
        checksum ^= b

    msg = [
        SYSEX_START,
        MFR_ID,
        CMD_WRITE_PAGE,
        (page >> 7) & 0x7F,
        page & 0x7F,
        (length >> 7) & 0x7F,
        length & 0x7F,
        (checksum >> 4) & 0x0F,
        checksum & 0x0F
    ]

    msg.extend(encoded)
    msg.append(SYSEX_END)

    return bytes(msg)


def main():

    if len(sys.argv) < 2:
        print("usage: hex2sysex firmware.hex")
        return

    ih = IntelHex(sys.argv[1])

    maxaddr = ih.maxaddr()

    pages = (maxaddr + PAGE_SIZE) // PAGE_SIZE

    for page in range(pages):

        addr = page * PAGE_SIZE
        data = ih.tobinarray(start=addr, size=PAGE_SIZE)

        msg = build_sysex(page, data)

        sys.stdout.buffer.write(msg)

    msg = [
        SYSEX_START,
        MFR_ID,
        CMD_EXECUTE,
        SYSEX_END
    ]
    sys.stdout.buffer.write(bytes(msg))

if __name__ == "__main__":
    main()
