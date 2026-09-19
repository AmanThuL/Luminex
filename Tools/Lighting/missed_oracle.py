#!/usr/bin/env python3
"""Count exact RGB missed-light diagnostics; report overflow separately from missing lights.

Use --input frame.png or a native capture directory with manifest.json. Missing, malformed,
nonopaque or non-oracle-colour evidence is a harness failure; red pixels are a renderer failure.
Yellow pixels fail overflow-free runs and are reported without failure with --allow-yellow.
Pillow accelerates decoding when installed; the renderer's 8-bit RGB/RGBA PNG subset also has
an independent stdlib decoder so the self-tests and the tool work in a fresh Python environment.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def decode_png(data):
    if data[:8] != SIGNATURE:
        raise ValueError("expected PNG signature")
    cursor = 8
    header = None
    compressed = bytearray()
    ended = False
    while cursor < len(data):
        if cursor + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        length = int.from_bytes(data[cursor:cursor + 4], "big")
        kind = data[cursor + 4:cursor + 8]
        payload = data[cursor + 8:cursor + 8 + length]
        end = cursor + length + 12
        if end > len(data) or zlib.crc32(kind + payload) != int.from_bytes(data[end - 4:end], "big"):
            raise ValueError("truncated PNG or chunk CRC mismatch")
        if kind == b"IHDR":
            if header is not None or cursor != 8 or length != 13:
                raise ValueError("invalid PNG header")
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            if header is None:
                raise ValueError("IDAT precedes header")
            compressed.extend(payload)
        elif kind == b"IEND":
            if length or end != len(data):
                raise ValueError("invalid PNG end")
            ended = True
            break
        elif not kind[0] & 32:
            raise ValueError("unsupported critical PNG chunk")
        cursor = end
    if not ended or header is None:
        raise ValueError("missing PNG header/end")
    width, height, bits, colour, compression, filtering, interlace = header
    if not (0 < width <= 32768 and 0 < height <= 32768 and bits == 8 and colour in (2, 6)
            and compression == filtering == interlace == 0):
        raise ValueError("expected noninterlaced 8-bit RGB/RGBA PNG")
    channels = 3 if colour == 2 else 4
    stride = width * channels
    expected = (stride + 1) * height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("PNG decompressed size or stream mismatch")
    pixels = bytearray()
    previous = bytes(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_kind = raw[start]
        row = bytearray(raw[start + 1:start + stride + 1])
        if filter_kind > 4:
            raise ValueError("invalid PNG row filter")
        if filter_kind:
            for x in range(stride):
                left = row[x - channels] if x >= channels else 0
                up = previous[x]
                corner = previous[x - channels] if x >= channels else 0
                if filter_kind == 1:
                    predictor = left
                elif filter_kind == 2:
                    predictor = up
                elif filter_kind == 3:
                    predictor = (left + up) // 2
                else:
                    base = left + up - corner
                    distances = (abs(base - left), abs(base - up), abs(base - corner))
                    predictor = (left, up, corner)[distances.index(min(distances))]
                row[x] = (row[x] + predictor) & 255
        pixels.extend(row)
        previous = row
    if channels == 4 and any(value != 255 for value in pixels[3::4]):
        raise ValueError("nonopaque oracle image")
    return width, height, channels, pixels


def count_pixels(path):
    data = path.read_bytes()
    try:
        from PIL import Image
    except ImportError:
        width, height, channels, raw = decode_png(data)
        counts = Counter(zip(raw[0::channels], raw[1::channels], raw[2::channels]))
    else:
        with Image.open(path) as image:
            if image.format != "PNG" or image.mode not in ("RGB", "RGBA"):
                raise ValueError("expected 8-bit RGB/RGBA PNG")
            if image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255):
                raise ValueError("nonopaque oracle image")
            width, height = image.size
            rgb = image.convert("RGB")
            colours = rgb.getcolors(maxcolors=4)
            counts = Counter({colour: count for count, colour in colours}) if colours is not None else Counter(rgb.getdata())
    black = counts.pop((0, 0, 0), 0)
    red = counts.pop((255, 0, 0), 0)
    yellow = counts.pop((255, 255, 0), 0)
    return dict(width=width, height=height, pixels=width * height, black=black, red=red,
                yellow=yellow, other=sum(counts.values()), sha256=hashlib.sha256(data).hexdigest())


def audit(path, allow_yellow=False):
    report = dict(schemaVersion=1, allowYellow=allow_yellow, frames=[], rendererFailures=[], harnessFailures=[])
    try:
        if path.is_dir():
            manifest = json.loads((path / "manifest.json").read_text())
            frames = manifest.get("frames", [])
            if (manifest.get("complete") is not True or manifest.get("failure") or not frames
                    or manifest.get("frameCount") != len(frames) or manifest.get("lightDebugView") != "missed"):
                raise ValueError("incomplete/non-missed native capture manifest")
            files = []
            previous = None
            for frame in frames:
                filename = frame.get("file", "")
                if Path(filename).name != filename or not filename or filename in files:
                    raise ValueError("duplicate or nonlocal frame filename")
                current = frame.get("simulationFrame")
                if type(current) is not int or current < 0 or (previous is not None and current != previous + 1):
                    raise ValueError("missing or unordered simulation frame")
                files.append(filename)
                previous = current
            paths = [path / file for file in files]
        else:
            paths = [path]
        for frame in paths:
            try:
                result = count_pixels(frame)
                report["frames"].append(dict(file=str(frame), **result))
                if result["other"]:
                    report["harnessFailures"].append(dict(file=str(frame), reason="non-oracle pixels", count=result["other"]))
                if result["red"]:
                    report["rendererFailures"].append(dict(file=str(frame), reason="missed lights", count=result["red"]))
                if result["yellow"] and not allow_yellow:
                    report["rendererFailures"].append(dict(file=str(frame), reason="overflow in lossless run", count=result["yellow"]))
            except (OSError, ValueError, zlib.error) as exc:
                report["harnessFailures"].append(dict(file=str(frame), reason=str(exc)))
    except (OSError, ValueError, TypeError, KeyError) as exc:
        report["harnessFailures"].append(dict(file=str(path), reason=str(exc)))
    report["complete"] = not report["harnessFailures"]
    report["passed"] = report["complete"] and not report["rendererFailures"]
    return report


class OracleTests(unittest.TestCase):
    @staticmethod
    def png(colours, channels=3, filter_kind=0):
        def chunk(kind, payload):
            return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
        pixels = bytes(value for colour in colours for value in colour)
        row = bytearray(pixels)
        for x, value in enumerate(pixels):
            left = pixels[x - channels] if x >= channels else 0
            predictor = left if filter_kind in (1, 4) else left // 2 if filter_kind == 3 else 0
            row[x] = (value - predictor) & 255
        return (SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", len(colours), 1, 8, 2 if channels == 3 else 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes([filter_kind]) + row)) + chunk(b"IEND", b""))

    def test_all_filters_and_exact_counts(self):
        colours = [(0, 0, 0), (255, 0, 0), (255, 255, 0), (254, 0, 0)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oracle.png"
            for filter_kind in range(5):
                path.write_bytes(self.png(colours, filter_kind=filter_kind))
                self.assertEqual(decode_png(path.read_bytes())[3], bytes(value for colour in colours for value in colour))
                result = count_pixels(path)
                self.assertEqual([result[key] for key in ("black", "red", "yellow", "other")], [1, 1, 1, 1])

    def test_overflow_never_erases_red_or_unknown_pixels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oracle.png"
            path.write_bytes(self.png([(255, 255, 0)]))
            self.assertFalse(audit(path)["passed"])
            self.assertTrue(audit(path, True)["passed"])
            path.write_bytes(self.png([(255, 0, 0), (2, 0, 0)]))
            result = audit(path, True)
            self.assertTrue(result["rendererFailures"])
            self.assertTrue(result["harnessFailures"])

    def test_malformed_and_alpha(self):
        for data in (b"", self.png([(0, 0, 0)])[:-1], self.png([(0, 0, 0, 0)], 4)):
            with self.assertRaises(ValueError):
                decode_png(data)
        data = bytearray(self.png([(0, 0, 0)])); data[-1] ^= 1
        with self.assertRaises(ValueError):
            decode_png(data)

    def test_incomplete_sequence_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "manifest.json").write_text('{"complete":true,"frameCount":1,"frames":[]}')
            self.assertFalse(audit(root)["complete"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--allow-yellow", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(OracleTests))
        return 0 if result.wasSuccessful() else 1
    if args.input is None or args.out is None:
        parser.error("--input and --out are required")
    if args.out.exists():
        parser.error("output exists; preserve every attempt")
    report = audit(args.input, args.allow_yellow)
    with args.out.open("x") as stream:
        json.dump(report, stream, indent=2); stream.write("\n")
    print(json.dumps({key: report[key] for key in ("complete", "passed", "rendererFailures", "harnessFailures")}))
    return 2 if not report["complete"] else 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
