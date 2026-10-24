#!/usr/bin/env python3
"""Back up, verify, and prepare safe restore files for OS-303 EEPROM."""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
import zlib
from pathlib import Path

try:
    import mido
except ImportError:  # Offline verify/prepare-restore does not need MIDI support.
    mido = None


MANUFACTURER_ID = 0x7D
CMD_READ_EEPROM = 0x56
CMD_EEPROM_DATA = 0x57
CMD_RESTORE_BEGIN = 0x58
CMD_RESTORE_DATA = 0x59
CMD_RESTORE_END = 0x5A
RESTORE_PROTOCOL_VERSION = 1
EEPROM_SIZE = 4096
CHUNK_SIZE = 32
CHUNK_COUNT = EEPROM_SIZE // CHUNK_SIZE
RESTORE_MARKER_A_ADDR = EEPROM_SIZE - 2
RESTORE_MARKER_B_ADDR = EEPROM_SIZE - 1
RESTORE_MARKER_A = 0xA5
RESTORE_MARKER_B = 0x5A
RESTORE_MARKER_INACTIVE = 0xFF


def require_mido() -> None:
    if mido is None:
        raise SystemExit(
            "Missing MIDI support. Run: python3 -m pip install mido python-rtmidi"
        )


def pack_7bit(data: bytes) -> bytes:
    """Encode arbitrary bytes as bitmap-prefixed MIDI-safe groups."""
    packed = bytearray()
    for offset in range(0, len(data), 7):
        group = data[offset : offset + 7]
        high_bits = 0
        for bit, value in enumerate(group):
            high_bits |= ((value >> 7) & 1) << bit
        packed.append(high_bits)
        packed.extend(value & 0x7F for value in group)
    return bytes(packed)


def unpack_7bit(data: tuple[int, ...] | list[int] | bytes, expected: int) -> bytes:
    """Decode bitmap-prefixed groups of at most seven MIDI-safe bytes."""
    decoded = bytearray()
    pos = 0
    while len(decoded) < expected:
        if pos >= len(data):
            raise ValueError("truncated packed data")
        high_bits = data[pos]
        pos += 1
        group_len = min(7, expected - len(decoded))
        if pos + group_len > len(data):
            raise ValueError("truncated packed group")
        for bit in range(group_len):
            decoded.append(data[pos] | (((high_bits >> bit) & 1) << 7))
            pos += 1
    if pos != len(data):
        raise ValueError("unexpected trailing packed data")
    return bytes(decoded)


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def sysex_frame(data: bytes | tuple[int, ...]) -> bytes:
    if any(value & 0x80 for value in data):
        raise ValueError("SysEx payload contains a non-7-bit byte")
    return bytes((0xF0, *data, 0xF7))


def iter_sysex(wire: bytes):
    pos = 0
    while pos < len(wire):
        start = wire.find(b"\xF0", pos)
        if start < 0:
            return
        end = wire.find(b"\xF7", start + 1)
        if end < 0:
            raise ValueError("truncated SysEx message at end of file")
        yield wire[start + 1 : end]
        pos = end + 1


def parse_reply(message, expected_addr: int, expected_count: int) -> bytes | None:
    if message.type != "sysex":
        return None
    data = tuple(message.data)
    if len(data) < 5 or data[:2] != (MANUFACTURER_ID, CMD_EEPROM_DATA):
        return None
    addr = data[2] | (data[3] << 7)
    count = data[4]
    if addr != expected_addr or count != expected_count:
        return None
    return unpack_7bit(data[5:], count)


def choose_port(names: list[str], selector: str | None, kind: str) -> str:
    if selector:
        matches = [name for name in names if selector.casefold() in name.casefold()]
    else:
        matches = names
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise SystemExit(f"No {kind} MIDI port matches {selector!r}. Use 'list' to see ports.")
    formatted = "\n  ".join(matches)
    raise SystemExit(
        f"More than one {kind} MIDI port matches {selector!r}:\n  {formatted}\n"
        f"Pass a more specific --{kind} value."
    )


def request_chunk(inport, outport, addr: int, timeout: float) -> tuple[bytes, bytes]:
    count = min(CHUNK_SIZE, EEPROM_SIZE - addr)
    request = mido.Message(
        "sysex",
        data=(MANUFACTURER_ID, CMD_READ_EEPROM, addr & 0x7F, (addr >> 7) & 0x7F, count),
    )

    for attempt in range(3):
        for _ in inport.iter_pending():
            pass
        outport.send(request)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for message in inport.iter_pending():
                decoded = parse_reply(message, addr, count)
                if decoded is not None:
                    return decoded, bytes((0xF0, *message.data, 0xF7))
            time.sleep(0.002)
        print(f"No reply at EEPROM offset {addr}; retry {attempt + 1}/3", file=sys.stderr)
    raise SystemExit(
        "The OS-303 stopped replying. Stop playback and MIDI clock, check both MIDI "
        "directions, and confirm the SysEx-backup firmware is installed."
    )


def validate_image(image: bytes, *, for_restore: bool = False) -> None:
    if len(image) != EEPROM_SIZE:
        raise ValueError(f"expected {EEPROM_SIZE} bytes, got {len(image)}")
    signature = image[:16].split(b"\0", 1)[0]
    if not signature.startswith(b"OS-303-v0.8"):
        shown = signature.decode("ascii", errors="replace")
        raise ValueError(f"unexpected EEPROM signature {shown!r}")
    if for_restore and image[-2:] != bytes((RESTORE_MARKER_INACTIVE,) * 2):
        raise ValueError(
            "backup does not contain inactive restore-reserved bytes; it may "
            "have been captured from an interrupted restore"
        )


def load_backup(source: Path) -> tuple[bytes, int, int, int]:
    """Return image and (SysEx, ignored, duplicate) message counts."""
    chunks: dict[int, bytes] = {}
    total = ignored = duplicates = 0
    for data in iter_sysex(source.read_bytes()):
        total += 1
        if len(data) < 5 or data[:2] != bytes((MANUFACTURER_ID, CMD_EEPROM_DATA)):
            ignored += 1
            continue
        addr = data[2] | (data[3] << 7)
        count = data[4]
        if count != CHUNK_SIZE or addr % CHUNK_SIZE or addr + count > EEPROM_SIZE:
            raise ValueError(f"invalid OS-303 EEPROM chunk at address {addr}")
        chunk = unpack_7bit(data[5:], count)
        if addr in chunks:
            if chunks[addr] != chunk:
                raise ValueError(f"conflicting duplicate EEPROM chunk at address {addr}")
            duplicates += 1
        else:
            chunks[addr] = chunk

    expected = set(range(0, EEPROM_SIZE, CHUNK_SIZE))
    missing = sorted(expected - chunks.keys())
    if missing:
        raise ValueError(
            f"incomplete backup: {len(chunks)}/{CHUNK_COUNT} chunks; "
            f"first missing EEPROM address is {missing[0]}"
        )
    image = b"".join(chunks[addr] for addr in sorted(expected))
    validate_image(image)
    return image, total, ignored, duplicates


def backup(args) -> None:
    require_mido()
    input_name = choose_port(mido.get_input_names(), args.input or args.port, "input")
    output_name = choose_port(mido.get_output_names(), args.output or args.port, "output")
    destination = Path(args.file).expanduser()
    if destination.suffix.lower() != ".syx":
        destination = destination.with_suffix(".syx")
    binary_destination = destination.with_suffix(".bin")

    print(f"MIDI input:  {input_name}")
    print(f"MIDI output: {output_name}")
    print("Keep the OS-303 stopped until the backup completes.")

    image = bytearray()
    sysex = bytearray()
    with mido.open_input(input_name) as inport, mido.open_output(output_name) as outport:
        for addr in range(0, EEPROM_SIZE, CHUNK_SIZE):
            chunk, wire = request_chunk(inport, outport, addr, args.timeout)
            if not args.single_read:
                check, _ = request_chunk(inport, outport, addr, args.timeout)
                if check != chunk:
                    raise SystemExit(
                        f"Two reads disagreed at EEPROM offset {addr}; no backup was saved. "
                        "Check the MIDI connection and try again."
                    )
            image.extend(chunk)
            sysex.extend(wire)
            print(f"\rRead {len(image):4d}/{EEPROM_SIZE} bytes", end="", flush=True)
    print()

    validate_image(bytes(image))
    destination.write_bytes(sysex)
    binary_destination.write_bytes(image)
    digest = hashlib.sha256(image).hexdigest()
    print(f"Saved SysEx backup: {destination}")
    print(f"Saved raw EEPROM:   {binary_destination}")
    print(f"SHA-256: {digest}")


def verify(args) -> None:
    source = Path(args.file).expanduser()
    try:
        image, total, ignored, duplicates = load_backup(source)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    digest = hashlib.sha256(image).hexdigest()
    print(f"Valid OS-303 EEPROM backup ({EEPROM_SIZE} bytes in {CHUNK_COUNT} chunks)")
    print(f"Messages: {total} SysEx, {ignored} unrelated ignored, {duplicates} duplicates")
    print(f"SHA-256: {digest}")


def make_restore_stream(image: bytes) -> bytes:
    validate_image(image, for_restore=True)
    checksum = zlib.crc32(image) & 0xFFFFFFFF
    checksum_bytes = checksum.to_bytes(4, "little")
    stream = bytearray()
    stream.extend(
        sysex_frame(
            (
                MANUFACTURER_ID,
                CMD_RESTORE_BEGIN,
                RESTORE_PROTOCOL_VERSION,
                EEPROM_SIZE & 0x7F,
                (EEPROM_SIZE >> 7) & 0x7F,
                *pack_7bit(checksum_bytes),
            )
        )
    )
    for addr in range(0, EEPROM_SIZE, CHUNK_SIZE):
        chunk = image[addr : addr + CHUNK_SIZE]
        chunk_crc = crc16_ccitt(chunk).to_bytes(2, "little")
        stream.extend(
            sysex_frame(
                (
                    MANUFACTURER_ID,
                    CMD_RESTORE_DATA,
                    addr & 0x7F,
                    (addr >> 7) & 0x7F,
                    CHUNK_SIZE,
                    *pack_7bit(chunk),
                    *pack_7bit(chunk_crc),
                )
            )
        )
    stream.extend(
        sysex_frame((MANUFACTURER_ID, CMD_RESTORE_END, *pack_7bit(checksum_bytes)))
    )
    return bytes(stream)


def load_restore(source: Path) -> bytes:
    """Validate a canonical restore stream and return its EEPROM image."""
    messages = list(iter_sysex(source.read_bytes()))
    if len(messages) != CHUNK_COUNT + 2:
        raise ValueError(
            f"restore contains {len(messages)} messages; expected {CHUNK_COUNT + 2}"
        )

    begin = messages[0]
    if (
        len(begin) != 10
        or begin[:3] != bytes((MANUFACTURER_ID, CMD_RESTORE_BEGIN, RESTORE_PROTOCOL_VERSION))
        or (begin[3] | (begin[4] << 7)) != EEPROM_SIZE
    ):
        raise ValueError("invalid restore BEGIN message")
    expected_crc = int.from_bytes(unpack_7bit(begin[5:], 4), "little")

    chunks: dict[int, bytes] = {}
    for index, message in enumerate(messages[1:-1]):
        if (
            len(message) != 45
            or message[:2] != bytes((MANUFACTURER_ID, CMD_RESTORE_DATA))
            or message[4] != CHUNK_SIZE
        ):
            raise ValueError(f"invalid restore DATA message at index {index + 1}")
        addr = message[2] | (message[3] << 7)
        if addr % CHUNK_SIZE or addr + CHUNK_SIZE > EEPROM_SIZE or addr in chunks:
            raise ValueError(f"invalid or duplicate restore address {addr}")
        chunk = unpack_7bit(message[5:42], CHUNK_SIZE)
        supplied_crc = int.from_bytes(unpack_7bit(message[42:], 2), "little")
        if crc16_ccitt(chunk) != supplied_crc:
            raise ValueError(f"CRC-16 mismatch at restore address {addr}")
        chunks[addr] = chunk

    expected_addresses = range(0, EEPROM_SIZE, CHUNK_SIZE)
    missing = [addr for addr in expected_addresses if addr not in chunks]
    if missing:
        raise ValueError(f"restore is missing EEPROM address {missing[0]}")
    image = b"".join(chunks[addr] for addr in expected_addresses)
    validate_image(image, for_restore=True)

    end = messages[-1]
    if len(end) != 7 or end[:2] != bytes((MANUFACTURER_ID, CMD_RESTORE_END)):
        raise ValueError("invalid restore END message")
    end_crc = int.from_bytes(unpack_7bit(end[2:], 4), "little")
    actual_crc = zlib.crc32(image) & 0xFFFFFFFF
    if expected_crc != actual_crc or end_crc != actual_crc:
        raise ValueError("restore CRC-32 mismatch")
    return image


def prepare_restore(args) -> None:
    source = Path(args.backup).expanduser()
    destination = Path(args.restore).expanduser()
    if destination.suffix.lower() != ".syx":
        destination = destination.with_suffix(".syx")
    binary_destination = destination.with_suffix(".bin")
    try:
        image, total, ignored, duplicates = load_backup(source)
        stream = make_restore_stream(image)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    destination.write_bytes(stream)
    binary_destination.write_bytes(image)
    checksum = zlib.crc32(image) & 0xFFFFFFFF
    print(f"Validated {CHUNK_COUNT} chunks from {total} SysEx messages")
    print(f"Ignored {ignored} unrelated messages; removed {duplicates} identical duplicates")
    print(f"Saved safe restore: {destination} ({CHUNK_COUNT + 2} messages, {len(stream)} bytes)")
    print(f"Saved raw EEPROM:  {binary_destination}")
    print(f"CRC-32: {checksum:08x}")
    print("In SysEx Librarian, set Pause between played messages to at least 500 ms.")


def verify_restore(args) -> None:
    source = Path(args.file).expanduser()
    try:
        image = load_restore(source)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    print(f"Valid OS-303 restore ({CHUNK_COUNT + 2} messages, {len(image)} EEPROM bytes)")
    print(f"CRC-32: {zlib.crc32(image) & 0xFFFFFFFF:08x}")
    print(f"SHA-256: {hashlib.sha256(image).hexdigest()}")


def list_ports(_args) -> None:
    require_mido()
    print("Inputs:")
    for name in mido.get_input_names():
        print(f"  {name}")
    print("Outputs:")
    for name in mido.get_output_names():
        print(f"  {name}")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("list", help="list MIDI input and output ports")

    backup_parser = commands.add_parser("backup", help="read and save the complete EEPROM")
    backup_parser.add_argument("file", help="destination .syx file")
    backup_parser.add_argument("--port", help="name fragment used for both MIDI ports")
    backup_parser.add_argument("--input", help="input port name fragment")
    backup_parser.add_argument("--output", help="output port name fragment")
    backup_parser.add_argument("--timeout", type=float, default=1.0, help="seconds per request")
    backup_parser.add_argument(
        "--single-read",
        action="store_true",
        help="read each chunk once instead of verifying it with a second read",
    )

    verify_parser = commands.add_parser("verify", help="validate a saved .syx backup")
    verify_parser.add_argument("file", help="backup .syx file")

    restore_parser = commands.add_parser(
        "prepare-restore", help="validate a backup and create a safe restore .syx"
    )
    restore_parser.add_argument("backup", help="recorded backup .syx file")
    restore_parser.add_argument("restore", help="destination restore .syx file")

    verify_restore_parser = commands.add_parser(
        "verify-restore", help="validate a prepared restore .syx"
    )
    verify_restore_parser.add_argument("file", help="prepared restore .syx file")
    return parser


def main() -> None:
    parser = make_parser()
    args = parser.parse_args()
    if args.command == "list":
        list_ports(args)
    elif args.command == "backup":
        backup(args)
    elif args.command == "verify":
        verify(args)
    elif args.command == "prepare-restore":
        prepare_restore(args)
    elif args.command == "verify-restore":
        verify_restore(args)


if __name__ == "__main__":
    main()
