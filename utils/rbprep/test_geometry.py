#!/usr/bin/env python3
"""Host-side regression tests for RBPrep's 4096 <-> 512 translations."""

import struct
import unittest


MULTIPLIER = 8
FAT_START_4096 = 49_278
FAT_COUNT_4096 = 244_141_367


def get16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def get32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def put16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def translate_mbr(data, multiplier=MULTIPLIER):
    result = bytearray(data)
    fat_entry = None
    for index in range(4):
        offset = 446 + index * 16
        if result[offset + 4] in (0x0B, 0x0C):
            start = get32(result, offset + 8)
            count = get32(result, offset + 12)
            if start and count:
                fat_entry = bytearray(result[offset:offset + 16])
                put32(fat_entry, 8, start * multiplier)
                put32(fat_entry, 12, count * multiplier)
                break
    if fat_entry is not None:
        result[446:510] = bytes(64)
        result[446:462] = fat_entry
    return result


def translate_bpb_out(data, multiplier=MULTIPLIER):
    result = bytearray(data)
    put16(result, 11, 512)
    result[13] *= multiplier
    for offset in (14, 48, 50):
        value = get16(result, offset)
        if offset == 14 or value not in (0, 0xFFFF):
            put16(result, offset, value * multiplier)
    for offset in (32, 36):
        put32(result, offset, get32(result, offset) * multiplier)
    return result


def translate_bpb_in(data, multiplier=MULTIPLIER):
    result = bytearray(data)
    values16 = {offset: get16(result, offset) for offset in (14, 48, 50)}
    values32 = {offset: get32(result, offset) for offset in (32, 36)}
    if result[13] == 0 or result[13] % multiplier:
        raise ValueError("invalid sectors-per-cluster")
    if values16[14] % multiplier or any(v % multiplier for v in values32.values()):
        raise ValueError("non-integral translated BPB geometry")
    for offset in (48, 50):
        if values16[offset] not in (0, 0xFFFF) and values16[offset] % multiplier:
            raise ValueError("non-integral translated BPB pointer")
    put16(result, 11, 4096)
    result[13] //= multiplier
    put16(result, 14, values16[14] // multiplier)
    for offset, value in values32.items():
        put32(result, offset, value // multiplier)
    for offset in (48, 50):
        if values16[offset] not in (0, 0xFFFF):
            put16(result, offset, values16[offset] // multiplier)
    return result


def sample_mbr():
    data = bytearray(512)
    data[446 + 4] = 0xEE
    put32(data, 446 + 8, 1)
    put32(data, 446 + 12, 49_277)
    data[462 + 4] = 0x0C
    put32(data, 462 + 8, FAT_START_4096)
    put32(data, 462 + 12, FAT_COUNT_4096)
    data[510:512] = b"\x55\xaa"
    return data


def sample_bpb():
    data = bytearray(512)
    put16(data, 11, 4096)
    data[13] = 8
    put16(data, 14, 32)
    put32(data, 28, 63)
    put32(data, 32, FAT_COUNT_4096)
    put32(data, 36, 29_792)
    put16(data, 48, 1)
    put16(data, 50, 6)
    data[82:90] = b"FAT32   "
    data[510:512] = b"\x55\xaa"
    return data


class RBPrepGeometryTests(unittest.TestCase):
    def test_observed_rizzpod_partition_geometry(self):
        translated = translate_mbr(sample_mbr())
        self.assertEqual(get32(translated, 454), 394_224)
        self.assertEqual(get32(translated, 458), 1_953_130_936)
        self.assertEqual(translated[462:510], bytes(48))

    def test_bpb_round_trip_is_lossless(self):
        original = sample_bpb()
        translated = translate_bpb_out(original)
        self.assertEqual(get16(translated, 11), 512)
        self.assertEqual(translated[13], 64)
        self.assertEqual(get16(translated, 14), 256)
        self.assertEqual(get32(translated, 28), 63)
        self.assertEqual(get16(translated, 48), 8)
        self.assertEqual(get16(translated, 50), 48)
        self.assertEqual(translate_bpb_in(translated), original)

    def test_rejects_fractional_reverse_translation(self):
        translated = translate_bpb_out(sample_bpb())
        translated[13] = 63
        with self.assertRaises(ValueError):
            translate_bpb_in(translated)

    def test_partition_bounds(self):
        start = FAT_START_4096 * MULTIPLIER
        end = start + FAT_COUNT_4096 * MULTIPLIER
        self.assertTrue(start <= start < end and 1 <= end - start)
        self.assertTrue(start <= end - 1 < end and 1 <= end - (end - 1))
        self.assertFalse(start <= start - 1 < end)
        self.assertFalse(start <= end < end)


if __name__ == "__main__":
    unittest.main(verbosity=2)
