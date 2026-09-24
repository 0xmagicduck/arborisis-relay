#!/usr/bin/env python3
"""Assemble an Arborisis release from the PlatformIO build tree.

For each Arborisis environment that has been built, this produces in `dist/`:

    arborisis_relay_<variant>.bin          the application alone (0x10000):
                                           "update", keeps every setting
    arborisis_relay_<variant>_merged.bin   bootloader + partitions + boot_app0
                                           + app (0x0): "full install", erases
                                           the flash first
    arborisis_pocket_<variant>.uf2         the pocket (nRF52840): dropped on
                                           the USB drive the bootloader shows
                                           after a double press of RESET
    arborisis_pocket_<variant>_dfu.zip     the same, for `adafruit-nrfutil
                                           dfu serial` over the USB port
    manifest.json                          version, board, offsets, sizes,
                                           SHA-256 of each file

`manifest.json` is what rns.arborisis.com/relay reads. The page trusts it for
offsets and checks the SHA-256 of what it downloaded before writing a byte to
the board: a truncated download flashed at 0x0 is a board that does not boot,
and the person holding it is not the person who can debug it.

Why not flash.py: upstream's flasher is 67 KB of interactive CLI that fetches
from GitHub releases; this is the forty lines that the web page needs, and it
uses the esptool PlatformIO already downloaded to build, so the merge and the
build agree on flash mode, frequency and size by construction. The UF2 is
written here for the same reason: it is forty lines too (Microsoft's format,
512-byte blocks, the nRF52840 family ID), and the page has no use for a
converter it cannot check.

Usage:  .venv/bin/pio run -e arborisis_heltec_v3 -e arborisis_pocket_l1 && .venv/bin/python3 arborisis-release.py
"""

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "dist")
PIO_PACKAGES = os.path.expanduser("~/.platformio/packages")
ESPTOOL = os.path.join(PIO_PACKAGES, "tool-esptoolpy", "esptool.py")
# esptool imports pyserial, which the system python does not have; the venv
# that runs PlatformIO does. Prefer it when it is there.
PYTHON = os.path.join(ROOT, ".venv", "bin", "python3")
if not os.path.isfile(PYTHON):
    PYTHON = sys.executable
BOOT_APP0 = os.path.join(PIO_PACKAGES, "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")

# The same layout flash.py uses, and the IDF default for ESP32-S3.
BOOTLOADER_ADDR = 0x0000
PARTITIONS_ADDR = 0x8000
BOOT_APP0_ADDR = 0xE000
APP_ADDR = 0x10000

# variant -> (env, board label, flash size, flash mode). The mode is DIO for
# both on purpose — see platformio.ini: the bootloader upgrades to QIO itself
# when the chip allows it, and a QIO image header bricks a board that does not.
VARIANTS = {
    "heltec_v3": ("arborisis_heltec_v3", "Heltec WiFi LoRa 32 V3", "8MB", "dio"),
    "heltec_v4": ("arborisis_heltec_v4", "Heltec WiFi LoRa 32 V4", "16MB", "dio"),
}

# The nRF52840 pockets: variant -> (env, board label, application address).
# The address is the linker script's (variants/<variant>/nrf52840_s140_v7.ld):
# 0x27000 behind SoftDevice S140 7.x, which the Seeed bootloader carries.
POCKET_VARIANTS = {
    "wio_tracker_l1": ("arborisis_pocket_l1", "Seeed Wio Tracker L1 Pro", 0x27000),
}

# UF2, as the Adafruit-family bootloaders read it: 512-byte blocks of 256
# payload bytes, flagged with the family ID of the nRF52840.
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
UF2_FAMILY_NRF52840 = 0xADA52840
UF2_PAYLOAD = 256


def version() -> str:
    with open(os.path.join(ROOT, "Arborisis.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+ARBORISIS_RELAY_VERSION\s+"([^"]+)"', f.read())
    if not m:
        sys.exit("ARBORISIS_RELAY_VERSION not found in Arborisis.h")
    return m.group(1)


def base_version() -> str:
    with open(os.path.join(ROOT, "Config.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+FW_RELEASE_TAG\s+"([^"]+)"', f.read())
    return m.group(1) if m else "unknown"


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git_commit() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception:
        return "unknown"


def intel_hex_to_bin(path: str) -> tuple[int, bytes]:
    """The contents of an Intel HEX file as one contiguous image, with the
    address of its first byte. Gaps are filled with 0xFF, the erased state
    of flash, which is what the DFU tool writes for them too."""
    chunks: dict[int, bytes] = {}
    base = 0
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            count, addr, kind, data = raw[0], (raw[1] << 8) | raw[2], raw[3], raw[4:-1]
            if sum(raw) & 0xFF:
                sys.exit(f"{path}: bad checksum on record {line}")
            if kind == 0x00:
                chunks[base + addr] = data[:count]
            elif kind == 0x01:
                break
            elif kind == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif kind == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
            # 0x03 / 0x05 (start addresses) carry nothing the flash needs.
    if not chunks:
        sys.exit(f"{path}: no data records")
    start = min(chunks)
    end = max(a + len(d) for a, d in chunks.items())
    image = bytearray(b"\xff" * (end - start))
    for a, d in chunks.items():
        image[a - start:a - start + len(d)] = d
    return start, bytes(image)


def write_uf2(image: bytes, address: int, out: str) -> None:
    blocks = (len(image) + UF2_PAYLOAD - 1) // UF2_PAYLOAD
    with open(out, "wb") as f:
        for n in range(blocks):
            payload = image[n * UF2_PAYLOAD:(n + 1) * UF2_PAYLOAD]
            header = struct.pack("<IIIIIIII", UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_FLAG_FAMILY_ID,
                                 address + n * UF2_PAYLOAD, UF2_PAYLOAD, n, blocks, UF2_FAMILY_NRF52840)
            f.write(header + payload.ljust(476, b"\x00") + struct.pack("<I", UF2_MAGIC_END))


def release_pockets(builds: list) -> None:
    for variant, (env, label, app_addr) in POCKET_VARIANTS.items():
        build_dir = os.path.join(ROOT, ".pio", "build", env)
        stem = f"arborisis_pocket_{variant}"
        hexfile = os.path.join(build_dir, f"{stem}.hex")
        dfu = os.path.join(build_dir, f"{stem}.zip")
        if not (os.path.isfile(hexfile) and os.path.isfile(dfu)):
            print(f"  {variant}: not built (pio run -e {env}), skipped")
            continue

        start, image = intel_hex_to_bin(hexfile)
        if start != app_addr:
            sys.exit(f"{variant}: the image starts at 0x{start:x}, the linker script says 0x{app_addr:x}")
        uf2_out = os.path.join(DIST, f"{stem}.uf2")
        dfu_out = os.path.join(DIST, f"{stem}_dfu.zip")
        write_uf2(image, start, uf2_out)
        shutil.copyfile(dfu, dfu_out)

        builds.append({
            "variant": variant,
            "board": label,
            "chipFamily": "nRF52840",
            "uf2": {"path": os.path.basename(uf2_out), "offset": start, "family": f"0x{UF2_FAMILY_NRF52840:08X}",
                    "size": os.path.getsize(uf2_out), "sha256": sha256(uf2_out)},
            "dfu": {"path": os.path.basename(dfu_out), "tool": "adafruit-nrfutil dfu serial",
                    "size": os.path.getsize(dfu_out), "sha256": sha256(dfu_out)},
        })
        print(f"  {variant}: {os.path.basename(uf2_out)} ({os.path.getsize(uf2_out):,} B), "
              f"{os.path.basename(dfu_out)} ({os.path.getsize(dfu_out):,} B)")


def main() -> int:
    os.makedirs(DIST, exist_ok=True)

    ver = version()
    builds = []
    esp32_built = any(
        os.path.isfile(os.path.join(ROOT, ".pio", "build", env, f"arborisis_relay_{variant}.bin"))
        for variant, (env, *_rest) in VARIANTS.items())
    if esp32_built:
        if not os.path.isfile(ESPTOOL):
            sys.exit(f"esptool not found at {ESPTOOL} — build once with pio first")
        if not os.path.isfile(BOOT_APP0):
            sys.exit(f"boot_app0.bin not found at {BOOT_APP0}")
    for variant, (env, label, flash_size, flash_mode) in VARIANTS.items():
        build_dir = os.path.join(ROOT, ".pio", "build", env)
        app = os.path.join(build_dir, f"arborisis_relay_{variant}.bin")
        bootloader = os.path.join(build_dir, "bootloader.bin")
        partitions = os.path.join(build_dir, "partitions.bin")
        if not all(os.path.isfile(p) for p in (app, bootloader, partitions)):
            print(f"  {variant}: not built (pio run -e {env}), skipped")
            continue

        app_out = os.path.join(DIST, f"arborisis_relay_{variant}.bin")
        merged_out = os.path.join(DIST, f"arborisis_relay_{variant}_merged.bin")
        with open(app, "rb") as src, open(app_out, "wb") as dst:
            dst.write(src.read())
        cmd = [
            PYTHON, ESPTOOL, "--chip", "esp32s3", "merge_bin",
            "--flash_mode", flash_mode, "--flash_freq", "80m", "--flash_size", flash_size,
            "-o", merged_out,
            f"0x{BOOTLOADER_ADDR:x}", bootloader,
            f"0x{PARTITIONS_ADDR:x}", partitions,
            f"0x{BOOT_APP0_ADDR:x}", BOOT_APP0,
            f"0x{APP_ADDR:x}", app,
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

        builds.append({
            "variant": variant,
            "board": label,
            "chipFamily": "ESP32-S3",
            "flashSize": flash_size,
            "update": {"path": os.path.basename(app_out), "offset": APP_ADDR,
                       "size": os.path.getsize(app_out), "sha256": sha256(app_out)},
            "full": {"path": os.path.basename(merged_out), "offset": 0,
                     "size": os.path.getsize(merged_out), "sha256": sha256(merged_out)},
        })
        print(f"  {variant}: {os.path.basename(app_out)} ({os.path.getsize(app_out):,} B), "
              f"{os.path.basename(merged_out)} ({os.path.getsize(merged_out):,} B)")

    release_pockets(builds)

    if not builds:
        sys.exit("nothing to release: no Arborisis environment is built")

    manifest = {
        "name": "Arborisis Relay",
        "version": ver,
        "images": {"relay": "ESP32-S3, flashed over Web Serial (esptool)",
                   "pocket": "nRF52840, flashed as a UF2 or over DFU"},
        "base": f"RTNode {base_version()}",
        "commit": git_commit(),
        "built": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "channel": {"frequency_hz": 869525000, "bandwidth_hz": 125000, "spreading_factor": 8,
                    "coding_rate": 5, "txpower_dbm": 22},
        "builds": builds,
    }
    # The channel above is repeated from Arborisis.h so the page can check
    # the firmware it serves against the channel it displays; a mismatch is
    # a build error, and the check below turns it into one.
    with open(os.path.join(ROOT, "Arborisis.h"), encoding="utf-8") as f:
        header = f.read()
    for macro, key in (("ARBORISIS_LORA_FREQ_HZ", "frequency_hz"), ("ARBORISIS_LORA_BW_HZ", "bandwidth_hz"),
                       ("ARBORISIS_LORA_SF", "spreading_factor"), ("ARBORISIS_LORA_CR", "coding_rate"),
                       ("ARBORISIS_LORA_TXP_DBM", "txpower_dbm")):
        m = re.search(rf"#define\s+{macro}\s+(\d+)", header)
        if not m or int(m.group(1)) != manifest["channel"][key]:
            sys.exit(f"{macro} in Arborisis.h does not match the channel in this script")

    with open(os.path.join(DIST, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"  manifest.json: Arborisis Relay {ver} ({manifest['commit']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
