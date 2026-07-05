#!/usr/bin/env python3
"""Merge the per-emotion esp_mmap_assets packs into one combined pack.

The five source packs in spiffs_image/*.bin each hold a single .eaf animation
plus its own index.json. Mounting one partition per emotion and re-parsing on
every switch is slow; the firmware wants ONE mmap pack flashed to the emote_gen
partition so switching an emotion is a pure mmap pointer flip.

This tool is self-contained (no PIL, no network, no assets_gen.py): it parses
the mmap "MMAP" container format directly, merges, repacks, and round-trip
verifies (re-parse + byte-compare every member + checksum). Any mismatch exits
non-zero so a broken pack never reaches the board.

Container format (little-endian), reverse-engineered from and matched against
esp_mmap_assets pack_assets()/reader:
  header (32B): magic "MMAP" | version=0x00010000 | name_len | file_count |
                checksum | payload_len | reserved(8)
  payload = table ++ blobs
  table entry (name_len+12): name[name_len] | size(4) | offset(4) | w(2) | h(2)
  blobs: per file  b"\\x5a\\x5a" ++ raw-bytes ; `offset` points at the 0x5a5a
"""
import argparse
import json
import os
import struct
import sys

MAGIC = b"MMAP"
VERSION = 0x00010000
HEADER_LEN = 32


def _checksum(payload: bytes) -> int:
    return sum(payload) & 0xFFFF


def unpack(blob: bytes) -> dict:
    """Return {name: bytes} for every member of an mmap pack."""
    if blob[:4] != MAGIC:
        raise ValueError("not an MMAP pack (bad magic)")
    _magic, version, name_len, file_count, checksum, payload_len = struct.unpack(
        "<4sIIIII", blob[:24]
    )
    payload = blob[HEADER_LEN:HEADER_LEN + payload_len]
    if _checksum(payload) != (checksum & 0xFFFF):
        raise ValueError("checksum mismatch on source pack")
    stride = name_len + 12
    table_len = file_count * stride
    out = {}
    for i in range(file_count):
        base = i * stride
        raw_name = payload[base:base + name_len]
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        size, offset, _w, _h = struct.unpack("<IIHH", payload[base + name_len:base + stride])
        start = table_len + offset + 2  # skip the 0x5a5a marker
        out[name] = bytes(payload[start:start + size])
    return out


def pack(files: "list[tuple[str, bytes]]") -> bytes:
    """Build an mmap pack from (name, bytes) pairs (mirrors pack_assets)."""
    files = sorted(files, key=lambda f: (os.path.splitext(f[0])[1], os.path.splitext(f[0])[0]))
    max_name = max(len(n) for n, _ in files)
    name_len = (max_name + 1 + 3) & ~3

    merged = bytearray()
    table = bytearray()
    for name, data in files:
        offset = len(merged)
        merged += b"\x5a\x5a"
        merged += data
        table += name.encode("utf-8").ljust(name_len, b"\0")
        table += struct.pack("<IIHH", len(data), offset, 0, 0)

    payload = bytes(table) + bytes(merged)
    header = struct.pack(
        "<4sIIIII8s", MAGIC, VERSION, name_len, len(files),
        _checksum(payload), len(payload), b"\0" * 8,
    )
    return header + payload


def repack(src_bins, out_path, assets_dir=None):
    members = {}          # final name -> bytes
    merged_index = []     # combined index.json entries

    for path in src_bins:
        with open(path, "rb") as f:
            parts = unpack(f.read())
        if "index.json" not in parts:
            raise ValueError(f"{path}: no index.json")
        entries = json.loads(parts["index.json"].decode("utf-8"))
        for e in entries:
            merged_index.append(e)
            fname = e["file"]
            if fname not in parts:
                raise ValueError(f"{path}: index references missing member {fname}")
            members[fname] = parts[fname]
        print(f"  + {os.path.basename(path):16s} -> {[e['name'] for e in entries]}")

    index_bytes = json.dumps(merged_index, ensure_ascii=False, indent=2).encode("utf-8")
    files = [("index.json", index_bytes)] + [(n, b) for n, b in members.items()]

    packed = pack(files)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(packed)

    if assets_dir:
        os.makedirs(assets_dir, exist_ok=True)
        with open(os.path.join(assets_dir, "index.json"), "wb") as f:
            f.write(index_bytes)
        for name, data in members.items():
            with open(os.path.join(assets_dir, name), "wb") as f:
                f.write(data)

    # --- round-trip self check ---
    reparsed = unpack(packed)
    expect = {"index.json": index_bytes, **members}
    if set(reparsed) != set(expect):
        raise SystemExit(f"FAIL: member set mismatch {set(reparsed)} != {set(expect)}")
    for name, data in expect.items():
        if reparsed[name] != data:
            raise SystemExit(f"FAIL: byte mismatch in {name}")

    total = len(packed)
    print(f"OK: {len(files)} members, {len(merged_index)} emotions, "
          f"{total} bytes -> {out_path}  (round-trip verified)")
    print(f"    emotions: {[e['name'] for e in merged_index]}")


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description="Merge per-emotion emote packs into one.")
    ap.add_argument("bins", nargs="*",
                    help="source single-emotion .bin packs (default: spiffs_image/*.bin)")
    ap.add_argument("--out", default=os.path.join(repo, "build", "nanosoul_emote.bin"),
                    help="combined pack output path")
    ap.add_argument("--assets-out", default=None,
                    help="also dump extracted index.json + .eaf into this dir")
    args = ap.parse_args()

    bins = args.bins
    if not bins:
        src = os.path.join(repo, "spiffs_image")
        bins = sorted(os.path.join(src, f) for f in os.listdir(src) if f.endswith(".bin"))
    if not bins:
        sys.exit("no source packs found")

    print(f"repacking {len(bins)} pack(s):")
    repack(bins, args.out, args.assets_out)


if __name__ == "__main__":
    main()
