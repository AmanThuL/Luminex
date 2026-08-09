#!/usr/bin/env python3
"""Inventory a .gputrace bundle and hunt for a known byte pattern + labels.

Usage: python3 spike_inventory.py <bundle.gputrace> [--pattern-w 300 --pattern-h 299]
Prints: file tree with sizes; the 256-byte capture-blob header where one is present;
metadata plist keys; the spike pattern's contiguity verdict plus its prefix hits; every
printable "lmx." string with (file, offset).
Exit 0 always -- this is an inventory, not a check.

Produce the bundle this was written against with:
  MTL_CAPTURE_ENABLED=1 xmake test Tests/gpu   (leaves lmx-spike.gputrace in the run dir)
"""
import argparse, pathlib, plistlib, struct, sys

# Every texture-contents blob in a macOS 26 bundle opens with this 8-byte magic: the ASCII
# "capture\0" stored as a little-endian u64, so on disk it reads back-to-front. Buffer blobs
# have no header at all -- they are raw contents -- which is why absence is reported, not fatal.
BLOB_MAGIC = b"erutpac\x00"
# The header's u64 fields, by byte offset. Cross-checked against RGBA8Unorm/BGRA8Unorm/
# Depth32Float/BC1_RGBA_sRGB blobs from 256x256 to 2560x1440, including cube slices and mip
# chains observed in capture bundles.
BLOB_U64_FIELDS = [(16, "kind"), (24, "pixelFormat"), (32, "width"), (40, "height"),
                   (48, "depth"), (56, "bytesPerRow"), (64, "bytesPerImage")]


def pattern_bytes(w, h):
    """The spike test's pattern, byte-for-byte (Tests/CaptureTests.cpp spikePatternBytes)."""
    return bytes(((i * 31) + (i >> 8)) & 0xFF for i in range(w * h * 4))


def blob_header(data):
    """Decode the 256-byte capture-blob header, or None when this file has none."""
    if not data.startswith(BLOB_MAGIC) or len(data) < 256:
        return None
    version, header_size = struct.unpack_from("<II", data, 8)
    out = {"version": hex(version), "headerSize": header_size}
    for offset, name in BLOB_U64_FIELDS:
        out[name] = struct.unpack_from("<Q", data, offset)[0]
    return out


def scan_file(path, needles):
    data = path.read_bytes()
    hits = []
    for name, needle in needles:
        start = 0
        while (idx := data.find(needle, start)) != -1:
            hits.append((name, idx))
            start = idx + 1
    return len(data), hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bundle", type=pathlib.Path)
    ap.add_argument("--pattern-w", type=int, default=300)
    ap.add_argument("--pattern-h", type=int, default=299)
    args = ap.parse_args()
    pattern = pattern_bytes(args.pattern_w, args.pattern_h)
    # The 64-byte prefix is a weak needle on purpose-built patterns: this one repeats its own
    # prefix every 256-byte block whose offset satisfies 31r + q = 0 (mod 256), so a *linear*
    # blob still shows dozens of prefix hits. PATTERN-FULL below is the verdict; the prefix hits
    # are only a locator.
    needles = [("PATTERN", pattern[:64]), ("LMX-LABEL", b"lmx.")]
    for path in sorted(args.bundle.rglob("*")):
        if not path.is_file():
            continue
        size, hits = scan_file(path, needles)
        rel = path.relative_to(args.bundle)
        print(f"{rel}  ({size} bytes)")
        data = path.read_bytes()
        header = blob_header(data)
        if header is not None:
            print("    blob header:", " ".join(f"{k}={v}" for k, v in header.items()))
        full = data.find(pattern)
        if full != -1:
            print(f"    PATTERN-FULL @ {full}: all {len(pattern)} bytes contiguous"
                  f"{' (== headerSize, so the payload starts there)' if header and full == header['headerSize'] else ''}")
        elif any(name == "PATTERN" for name, _ in hits):
            print("    PATTERN-FULL: absent -- prefix hits only, so the bytes are not laid out "
                  "linearly here")
        for name, off in hits[:40]:
            if name == "LMX-LABEL":
                end = off
                while end < len(data) and 0x20 <= data[end] < 0x7F:
                    end += 1
                print(f"    {name} @ {off}: {data[off:end].decode(errors='replace')}")
            else:
                print(f"    {name} @ {off}")
        if len(hits) > 40:
            print(f"    ... {len(hits) - 40} further hit(s) suppressed")
        if path.name == "metadata":
            try:
                print("    plist keys:", sorted(plistlib.loads(data).keys()))
            except Exception as e:  # inventory tool: report, never die
                print("    plist parse failed:", e)


if __name__ == "__main__":
    sys.exit(main())
