#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""Build release images of Arborisis Mesh into dist/, with their manifest.

    python3 tools/release.py arb_heltec_v3 arb_rak_4631 arb_picow
    python3 tools/release.py --all
    python3 tools/release.py --merge <dir-of-dist-dirs> <out>   # one manifest from CI artifacts

The manifest follows the one rns.arborisis.com already reads for the relay
(arborisis-reticulum, web/src/relay/manifest.ts): each build names its
board and chip family, and each file carries its size and SHA-256, which the
page checks before a byte reaches the board. What differs is the image a
family takes:

  ESP32 / S3 / C3 / C6  "update"  app only, at 0x10000 (settings survive)
                        "full"    bootloader + partitions + app, at 0x0
  nRF52840, RP2040      "uf2"     dropped on the bootloader's USB drive
  STM32WL               "hex"     STM32CubeProgrammer

The files are named arborisis_mesh_<board>.<ext>; the directory is served
as /firmware/arborisis-mesh/ next to /firmware/arborisis-relay/.
"""

import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
PIO = os.environ.get("PIO", shutil.which("pio") or str(ROOT.parent / ".venv" / "bin" / "pio"))

CHIP_FAMILY = {
    "esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3", "esp32c6": "ESP32-C6",
}
FAMILY_CHIP = {"nrf52": "nRF52840", "rp2040": "RP2040", "stm32": "STM32WL"}
RADIO = {
    "CustomSX1262": "SX1262", "CustomSX1268": "SX1268", "CustomSX1276": "SX1276", "CustomLLCC68": "LLCC68",
    "CustomLR1110": "LR1110", "CustomLR2021": "LR2021", "CustomSTM32WLx": "STM32WL",
}
# Names a person recognises, for the variants whose directory name is not one.
BOARD_NAMES = {
    "heltec_v2": "Heltec WiFi LoRa 32 V2", "heltec_v3": "Heltec WiFi LoRa 32 V3",
    "heltec_v4": "Heltec WiFi LoRa 32 V4", "heltec_v4_r8": "Heltec WiFi LoRa 32 V4 (R8)",
    "heltec_t114": "Heltec Mesh Node T114", "heltec_tracker": "Heltec Wireless Tracker",
    "heltec_tracker_v2": "Heltec Wireless Tracker V2", "heltec_wireless_paper": "Heltec Wireless Paper",
    "heltec_ct62": "Heltec HT-CT62", "heltec_mesh_solar": "Heltec MeshSolar", "heltec_rc32": "Heltec RC32",
    "heltec_e213": "Heltec Vision Master E213", "heltec_e290": "Heltec Vision Master E290",
    "heltec_t190": "Heltec Vision Master T190", "heltec_t096": "Heltec T096", "heltec_t1": "Heltec T1",
    "heltec_tower_v2": "Heltec Tower V2",
    "lilygo_tbeam_SX1262": "LilyGO T-Beam (SX1262)", "lilygo_tbeam_SX1276": "LilyGO T-Beam (SX1276)",
    "lilygo_tbeam_supreme_SX1262": "LilyGO T-Beam Supreme", "lilygo_tbeam_1w": "LilyGO T-Beam 1W",
    "lilygo_t3s3": "LilyGO T3-S3 (SX1262)", "lilygo_t3s3_sx1276": "LilyGO T3-S3 (SX1276)",
    "lilygo_tdeck": "LilyGO T-Deck", "lilygo_techo": "LilyGO T-Echo", "lilygo_techo_lite": "LilyGO T-Echo Lite",
    "lilygo_techo_card": "LilyGO T-Echo Card", "lilygo_tlora_v2_1": "LilyGO T-LoRa V2.1-1.6",
    "lilygo_tlora_c6": "LilyGO T-LoRa C6", "lilygo_teth_elite": "LilyGO T-ETH Elite",
    "lilygo_t_impulse_plus": "LilyGO T-Impulse Plus",
    "rak4631": "RAK WisBlock 4631", "rak3112": "RAK WisBlock 3112", "rak3401": "RAK WisBlock 3401",
    "rak11310": "RAK WisBlock 11310", "rak3x72": "RAK3172", "rak_wismesh_tag": "RAK WisMesh Tag",
    "xiao_s3": "Seeed XIAO ESP32S3", "xiao_s3_wio": "Seeed XIAO ESP32S3 + Wio-SX1262",
    "xiao_c3": "Seeed XIAO ESP32C3", "xiao_c6": "Seeed XIAO ESP32C6", "xiao_nrf52": "Seeed XIAO nRF52840",
    "xiao_rp2040": "Seeed XIAO RP2040", "wio-tracker-l1": "Seeed Wio Tracker L1",
    "wio-tracker-l1-eink": "Seeed Wio Tracker L1 E-ink", "t1000-e": "Seeed SenseCAP T1000-E",
    "sensecap_solar": "Seeed SenseCAP Solar", "wio-e5-dev": "Seeed Wio-E5 Dev", "wio-e5-mini": "Seeed Wio-E5 mini",
    "wio_wm1110": "Seeed Wio-WM1110", "station_g2": "B&Q Station G2", "station_g3_esp32": "B&Q Station G3",
    "nano_g2_ultra": "B&Q Nano G2 Ultra", "rpi_picow": "Raspberry Pi Pico W", "promicro": "Pro Micro nRF52840 (DIY)",
    "waveshare_rp2040_lora": "Waveshare RP2040-LoRa", "generic-e22": "ESP32 + E22 (DIY)",
    "ebyte_eora_s3": "EBYTE EoRa-S3",
}


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def file_entry(path, **extra):
    return {"path": path.name, "size": path.stat().st_size, "sha256": sha256(path), **extra}


def uf2_start(path):
    """Target address and family of a UF2's first block."""
    with open(path, "rb") as f:
        block = f.read(512)
    magic0, magic1, flags, addr = struct.unpack_from("<IIII", block, 0)
    family = struct.unpack_from("<I", block, 28)[0] if flags & 0x2000 else 0
    return addr, f"0x{family:08X}"


def board_name(variant, env):
    if variant in BOARD_NAMES:
        name = BOARD_NAMES[variant]
    else:
        name = " ".join(w.upper() if len(w) <= 3 and w.isalpha() and w not in ("mesh", "kit", "pro")
                        else w.capitalize() for w in re.split(r"[_\-]+", variant))
    # A variant with several environments (display / no display, PA power…)
    vs, e = variant.replace("-", "_").lower(), env[4:]
    suffix = e[len(vs):].strip("_") if e.startswith(vs) else ""
    if suffix and suffix not in ("sx1262", "sx1276", "repeater"):
        name += f" ({suffix.replace('_', ' ')})"
    return name


_config = None


def env_options(env):
    global _config
    if _config is None:
        out = subprocess.run([PIO, "project", "config", "--json-output"], cwd=ROOT, capture_output=True, text=True).stdout
        _config = {section: dict(options) for section, options in json.loads(out)}
    return _config.get(f"env:{env}", {})


def board_file(board):
    for f in [ROOT / "boards" / f"{board}.json", *Path.home().glob(f".platformio/platforms/*/boards/{board}.json")]:
        if f.exists():
            return json.loads(f.read_text())
    return {}


def build_one(env, info, version):
    fam = info["family"]
    targets = ["mergebin"] if fam.startswith("esp32") else (["create_uf2"] if fam == "nrf52" else [])
    if subprocess.call([PIO, "run", "-e", env], cwd=ROOT) != 0:
        return None
    for t in targets:   # custom targets after the firmware exists
        if subprocess.call([PIO, "run", "-e", env, "-t", t], cwd=ROOT) != 0:
            return None
    build = ROOT / ".pio" / "build" / env
    stem = f"arborisis_mesh_{env[4:]}"
    opts = env_options(env)
    flags = " ".join(opts.get("build_flags", [])) if isinstance(opts.get("build_flags"), list) else str(opts.get("build_flags", ""))
    b = {
        "variant": env[4:],
        "env": env,
        "board": board_name(info["variant"], env),
        "meshcore_variant": info["variant"],
        "radio": RADIO.get(info["radio"], info["radio"]),
        "display": bool(info.get("display")),
        "reticulum": "ARB_WITH_RNS=1" in flags,
        "ble": "ARB_WITH_BLE=1" in flags,
    }
    if fam.startswith("esp32"):
        bf = board_file(opts.get("board", ""))
        b["chipFamily"] = CHIP_FAMILY.get(bf.get("build", {}).get("mcu", ""), "ESP32")
        b["flashSize"] = opts.get("board_upload.flash_size") or bf.get("upload", {}).get("flash_size", "")
        shutil.copy2(build / "firmware.bin", DIST / f"{stem}.bin")
        shutil.copy2(build / "firmware-merged.bin", DIST / f"{stem}_merged.bin")
        b["update"] = file_entry(DIST / f"{stem}.bin", offset=0x10000)
        b["full"] = file_entry(DIST / f"{stem}_merged.bin", offset=0)
    elif fam in ("nrf52", "rp2040"):
        b["chipFamily"] = FAMILY_CHIP[fam]
        shutil.copy2(build / "firmware.uf2", DIST / f"{stem}.uf2")
        addr, family = uf2_start(DIST / f"{stem}.uf2")
        b["uf2"] = file_entry(DIST / f"{stem}.uf2", offset=addr, family=family)
    else:
        b["chipFamily"] = FAMILY_CHIP[fam]
        shutil.copy2(build / "firmware.hex", DIST / f"{stem}.hex")
        b["hex"] = file_entry(DIST / f"{stem}.hex")
    return b


def header(version):
    ini = (ROOT / "platformio.ini").read_text()
    mc = re.search(r'#define FIRMWARE_VERSION\s+"([^"]+)"', (ROOT / "app" / "mc" / "MyMesh.h").read_text()).group(1)
    mc_commit = (ROOT / "MESHCORE_VERSION").read_text().strip()[:7]
    try:
        commit = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, capture_output=True,
                                text=True).stdout.strip()
    except OSError:
        commit = ""
    prof = (ROOT / "app" / "ArbProfile.h").read_text()

    def num(name):
        return int(re.search(rf"#define {name}\s+(\d+)", prof).group(1))

    return {
        "name": "Arborisis Mesh",
        "version": version,
        "base": f"MeshCore {mc} ({mc_commit}) + microReticulum",
        "commit": commit,
        "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        # The Reticulum channel a fresh board joins (app/ArbProfile.h).
        "channel": {
            "frequency_hz": num("ARB_RNS_FREQ_HZ"), "bandwidth_hz": num("ARB_RNS_BW_HZ"),
            "spreading_factor": num("ARB_RNS_SF"), "coding_rate": num("ARB_RNS_CR"),
            "txpower_dbm": num("ARB_RNS_TXP_DBM"),
        },
        # MeshCore's default for Europe (platformio.ini [arduino_base]).
        "meshcore": {
            "frequency_hz": int(round(float(re.search(r"LORA_FREQ=([\d.]+)", ini).group(1)) * 1e6)),
            "bandwidth_hz": int(round(float(re.search(r"LORA_BW=([\d.]+)", ini).group(1)) * 1e3)),
            "spreading_factor": int(re.search(r"LORA_SF=(\d+)", ini).group(1)),
        },
        "builds": [],
    }


def merge(src, out):
    """One manifest and every file, from the dist/ directories of CI jobs."""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    manifest = None
    for m in sorted(Path(src).glob("*/manifest.json")):
        d = json.loads(m.read_text())
        if manifest is None:
            manifest = {k: v for k, v in d.items() if k != "builds"} | {"builds": []}
        manifest["builds"] += d["builds"]
        for f in m.parent.iterdir():
            if f.name != "manifest.json":
                shutil.copy2(f, out / f.name)
    if manifest is None:
        print("nothing to merge")
        return 1
    manifest["builds"].sort(key=lambda b: b["board"].lower())
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"{out}/manifest.json: {len(manifest['builds'])} boards")
    return 0


def main(argv):
    if argv[:1] == ["--merge"] and len(argv) == 3:
        return merge(argv[1], argv[2])
    boards = {b["env"]: b for b in json.loads((ROOT / "docs" / "boards.json").read_text())}
    envs = list(boards) if "--all" in argv else [a for a in argv if not a.startswith("--")]
    if not envs or any(e not in boards for e in envs):
        print(__doc__)
        return 2
    version = re.search(r"ARB_VERSION='\"([^\"]+)\"'", (ROOT / "platformio.ini").read_text()).group(1)
    DIST.mkdir(exist_ok=True)
    manifest = header(version)
    old = DIST / "manifest.json"
    if old.exists():   # keep the boards an earlier run of the same version built
        prev = json.loads(old.read_text())
        if prev.get("version") == version:
            manifest["builds"] = [b for b in prev.get("builds", []) if b["env"] not in envs]
    failed = []
    for env in envs:
        b = build_one(env, boards[env], version)
        if b is None:
            failed.append(env)
        else:
            manifest["builds"].append(b)
            print(f"{env}: {b['board']} ({b['chipFamily']})")
    manifest["builds"].sort(key=lambda b: b["board"].lower())
    (DIST / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    if failed:
        print("failed: " + " ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
