#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
import zlib
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("os303_backup.py")
SPEC = importlib.util.spec_from_file_location("os303_backup", MODULE_PATH)
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)


def sample_image() -> bytes:
    image = bytearray((index * 37 + 11) & 0xFF for index in range(backup.EEPROM_SIZE))
    image[:16] = b"OS-303-v0.8\0\0\0\0\0"
    image[backup.RESTORE_MARKER_A_ADDR] = 0xFF
    image[backup.RESTORE_MARKER_B_ADDR] = 0xFF
    return bytes(image)


def backup_stream(image: bytes) -> bytes:
    stream = bytearray()
    for addr in range(0, backup.EEPROM_SIZE, backup.CHUNK_SIZE):
        chunk = image[addr : addr + backup.CHUNK_SIZE]
        stream.extend(
            backup.sysex_frame(
                (
                    backup.MANUFACTURER_ID,
                    backup.CMD_EEPROM_DATA,
                    addr & 0x7F,
                    (addr >> 7) & 0x7F,
                    backup.CHUNK_SIZE,
                    *backup.pack_7bit(chunk),
                )
            )
        )
    return bytes(stream)


def write_temp(directory: str, name: str, data: bytes) -> Path:
    path = Path(directory, name)
    path.write_bytes(data)
    return path


class BackupToolTests(unittest.TestCase):
    def test_pack_round_trip_and_crc_reference(self):
        for length in range(65):
            raw = bytes((index * 37 + length) & 0xFF for index in range(length))
            self.assertEqual(backup.unpack_7bit(backup.pack_7bit(raw), length), raw)
        self.assertEqual(backup.crc16_ccitt(b"123456789"), 0x29B1)

    def test_unpack_rejects_truncated_and_trailing_data(self):
        packed = backup.pack_7bit(bytes(range(10)))
        with self.assertRaises(ValueError):
            backup.unpack_7bit(packed[:-1], 10)
        with self.assertRaises(ValueError):
            backup.unpack_7bit(packed + b"\0", 10)

    def test_load_backup_filters_noise_and_duplicate(self):
        image = sample_image()
        stream = backup.sysex_frame((0x01, 0x02, 0x03))
        stream += backup_stream(image)
        stream += backup_stream(image)[:44]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "capture.syx")
            path.write_bytes(stream)
            loaded, total, ignored, duplicates = backup.load_backup(path)
        self.assertEqual(loaded, image)
        self.assertEqual((total, ignored, duplicates), (130, 1, 1))

    def test_load_backup_rejects_missing_and_conflicting_chunks(self):
        image = sample_image()
        stream = backup_stream(image)
        with tempfile.TemporaryDirectory() as directory:
            missing = write_temp(directory, "missing.syx", stream[44:])
            with self.assertRaisesRegex(ValueError, "incomplete backup"):
                backup.load_backup(missing)

            conflict = bytearray(stream[:44])
            conflict[7] ^= 1
            conflicting = write_temp(
                directory, "conflicting.syx", stream + bytes(conflict)
            )
            with self.assertRaisesRegex(ValueError, "conflicting duplicate"):
                backup.load_backup(conflicting)

    def test_restore_stream_has_begin_data_and_end_frames(self):
        image = sample_image()
        stream = backup.make_restore_stream(image)
        messages = list(backup.iter_sysex(stream))
        self.assertEqual(len(messages), backup.CHUNK_COUNT + 2)
        self.assertEqual(messages[0][0:2], bytes((0x7D, backup.CMD_RESTORE_BEGIN)))
        self.assertTrue(all(msg[1] == backup.CMD_RESTORE_DATA for msg in messages[1:-1]))
        self.assertEqual(messages[-1][0:2], bytes((0x7D, backup.CMD_RESTORE_END)))
        self.assertEqual(len(stream), 6037)

        expected_crc = zlib.crc32(image) & 0xFFFFFFFF
        begin_crc = int.from_bytes(backup.unpack_7bit(messages[0][5:], 4), "little")
        self.assertEqual(begin_crc, expected_crc)
        restored = bytearray(backup.EEPROM_SIZE)
        for message in messages[1:-1]:
            addr = message[2] | (message[3] << 7)
            chunk = backup.unpack_7bit(message[5:42], backup.CHUNK_SIZE)
            chunk_crc = int.from_bytes(backup.unpack_7bit(message[42:45], 2), "little")
            self.assertEqual(chunk_crc, backup.crc16_ccitt(chunk))
            restored[addr : addr + backup.CHUNK_SIZE] = chunk
        end_crc = int.from_bytes(backup.unpack_7bit(messages[-1][2:], 4), "little")
        self.assertEqual(bytes(restored), image)
        self.assertEqual(end_crc, expected_crc)

        with tempfile.TemporaryDirectory() as directory:
            restore = write_temp(directory, "restore.syx", stream)
            self.assertEqual(backup.load_restore(restore), image)

    def test_restore_validator_rejects_corruption_and_missing_message(self):
        stream = backup.make_restore_stream(sample_image())
        messages = [backup.sysex_frame(message) for message in backup.iter_sysex(stream)]
        corrupted = bytearray(messages[5])
        corrupted[10] ^= 1
        messages[5] = bytes(corrupted)
        with tempfile.TemporaryDirectory() as directory:
            bad_crc = write_temp(directory, "bad-crc.syx", b"".join(messages))
            with self.assertRaisesRegex(ValueError, "CRC-16 mismatch"):
                backup.load_restore(bad_crc)

            missing = write_temp(directory, "missing.syx", b"".join(messages[:-2] + messages[-1:]))
            with self.assertRaisesRegex(ValueError, "expected 130"):
                backup.load_restore(missing)

    def test_prepare_rejects_recovery_marker(self):
        for addr, marker in (
            (backup.RESTORE_MARKER_A_ADDR, backup.RESTORE_MARKER_A),
            (backup.RESTORE_MARKER_B_ADDR, backup.RESTORE_MARKER_B),
            (backup.RESTORE_MARKER_A_ADDR, 0x00),
        ):
            image = bytearray(sample_image())
            image[addr] = marker
            with self.assertRaises(ValueError):
                backup.make_restore_stream(bytes(image))


if __name__ == "__main__":
    unittest.main()
