#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""Generate one Arborisis Mesh build environment per LoRa board MeshCore knows.

MeshCore describes each board in variants/<board>/platformio.ini: a base
section with the pin map and radio class, and a few environments on top of
it (repeater, companion, room server…). The repeater environment is the one
closest to what Arborisis Mesh is — a headless infrastructure node with the
board's display and button when it has them — so each Arborisis
environment *extends that repeater environment* and swaps its application
for ours:

    [env:arb_heltec_v3]
    extends          = env:Heltec_v3_repeater
    build_flags      = ${env:Heltec_v3_repeater.build_flags} ${arborisis.build_flags} ...
    build_src_filter = ${env:Heltec_v3_repeater.build_src_filter} -<../examples/simple_repeater> ${arborisis.build_src_filter}

A board MeshCore ships without a repeater gets an environment on its base
section instead (no display class: headless). Boards without a LoRa radio
(the ESP-NOW ones) are skipped.

Writes arborisis_envs.ini (read by platformio.ini through extra_configs)
and docs/boards.json (the support matrix the docs and the release script
read). Run it again after tools/sync_meshcore.sh.
"""

import configparser
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAMILIES = {
    "esp32_base": "esp32",
    "esp32c6_base": "esp32c6",
    "nrf52_base": "nrf52",
    "rp2040_base": "rp2040",
    "stm32_base": "stm32",
}
SKIP_WORDS = ("bridge", "logging", "ethernet")
# Flash size of the PlatformIO boards the variants use that are not in
# boards/ (they come with the platform, which may not be installed where
# this runs — CI checks that the generated files are up to date).
PLATFORM_BOARD_FLASH_MB = {
    "esp32-c3-devkitm-1": 4,
    "esp32-c6-devkitm-1": 4,
    "esp32-s3-devkitc-1": 8,
    "esp32doit-devkit-v1": 4,
    "esp32s3box": 16,
    "heltec_wifi_lora_32_V2": 8,
    "seeed_xiao_esp32c3": 4,
    "seeed_xiao_esp32s3": 8,
    "ttgo-lora32-v1": 4,
    "ttgo-t-beam": 4,
}
# The partition table those same boards name (build.arduino.partitions),
# for the ones with more than 4 MB of flash; the others get huge_app.csv.
PLATFORM_BOARD_PARTITIONS = {
    "esp32-s3-devkitc-1": "default_8MB.csv",
    "esp32s3box": "default_16MB.csv",
    "heltec_wifi_lora_32_V2": "default_8MB.csv",
    "seeed_xiao_esp32s3": "default_8MB.csv",
}
# Tables whose application slot (1.25 / 1.9 MB) cannot hold MeshCore and
# Reticulum together (about 2 MB on an ESP32-S3).
SMALL_PARTITION_TABLES = ("default.csv", "min_spiffs.csv")
REPEATER_RE = re.compile(r"_?[Rr]epeater_?$")


def read_ini(path):
    cp = configparser.RawConfigParser(strict=False, interpolation=None, comment_prefixes=(";", "#"),
                                      inline_comment_prefixes=(";",))
    cp.optionxform = str
    cp.read(path)
    return cp


def slug(name):
    s = REPEATER_RE.sub("", name)
    s = re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").lower()
    return "arb_" + s


def main():
    main_ini = read_ini(ROOT / "platformio.ini")
    sections = {s: dict(main_ini.items(s)) for s in main_ini.sections()}
    per_variant = {}
    for ini in sorted((ROOT / "variants").glob("*/platformio.ini")):
        cp = read_ini(ini)
        names = cp.sections()
        per_variant[ini.parent.name] = names
        for s in names:
            sections[s] = dict(cp.items(s))

    def extends_chain(name):
        chain = []
        while name and name in sections and name not in chain:
            chain.append(name)
            name = sections[name].get("extends", "").strip()
        if name and name not in chain:
            chain.append(name)
        return chain

    def family(name):
        for n in extends_chain(name):
            if n in FAMILIES:
                return FAMILIES[n]
        return None

    def flags(name, depth=0):
        """build_flags with ${x.build_flags} references expanded."""
        if depth > 12 or name not in sections:
            return ""
        raw = sections[name].get("build_flags")
        if raw is None:
            parent = sections[name].get("extends", "").strip()
            return flags(parent, depth + 1) if parent else ""

        def sub(m):
            return flags(m.group(1), depth + 1)
        return re.sub(r"\$\{([^}.]+)\.build_flags\}", sub, raw)

    def option(name, key, depth=0):
        """An option's value through the extends chain."""
        if depth > 12 or name not in sections:
            return None
        if key in sections[name]:
            return sections[name][key].split(";")[0].strip()
        parent = sections[name].get("extends", "").strip()
        return option(parent, key, depth + 1) if parent else None

    def board_flash_mb(name):
        """Flash size of the env's board, from the env or the board file."""
        v = option(name, "board_upload.flash_size")
        board = option(name, "board")
        if not v and (ROOT / "boards" / f"{board}.json").exists():
            v = json.loads((ROOT / "boards" / f"{board}.json").read_text()).get("upload", {}).get("flash_size")
        if not v and board in PLATFORM_BOARD_FLASH_MB:
            return PLATFORM_BOARD_FLASH_MB[board]
        if not v:
            for c in sorted(Path.home().glob(f".platformio/platforms/*/boards/{board}.json")):
                if c.exists():
                    v = json.loads(c.read_text()).get("upload", {}).get("flash_size")
                    break
        m = re.match(r"(\d+)\s*MB", v or "")
        return int(m.group(1)) if m else None

    def partition_table(name):
        """The partition table the env builds with: its own, its board's, or the core's default."""
        own = option(name, "board_build.partitions")
        if own:
            return own
        board = option(name, "board")
        f = ROOT / "boards" / f"{board}.json"
        if f.exists():
            return json.loads(f.read_text()).get("build", {}).get("arduino", {}).get("partitions") or "default.csv"
        return PLATFORM_BOARD_PARTITIONS.get(board, "default.csv")

    def define(flagstr, key):
        m = re.search(r"-D\s*" + key + r"=([^\s]+)", flagstr)
        return m.group(1).strip("'\"") if m else None

    # What the MeshCore side reports (`ver`, the app): MeshCore's version with
    # ours after a dash — MeshCore's own tools cut at the dash.
    arb_ver = re.search(r"ARB_VERSION='\"([^\"]+)\"'", sections["arborisis"]["build_flags"]).group(1)
    mc_h = (ROOT / "app" / "mc" / "MyMesh.h").read_text()
    mc_ver = re.search(r'#define FIRMWARE_VERSION\s+"([^"]+)"', mc_h).group(1)
    fw_ver = f"{mc_ver}-arb{arb_ver}"

    envs = []
    for variant, names in per_variant.items():
        target = (ROOT / "variants" / variant / "target.cpp")
        if target.exists() and "radio_driver.init()" in target.read_text(errors="ignore"):
            continue  # ESP-NOW "radio": no LoRa to speak either protocol on
        chosen = [n for n in names if n.startswith("env:") and REPEATER_RE.search(n[4:])
                  and not any(w in n.lower() for w in SKIP_WORDS)]
        headless = False
        if not chosen:
            bases = [n for n in names if not n.startswith("env:") and family(n)]
            if not bases:
                continue
            chosen = [bases[0]]
            headless = True
        for src in chosen:
            fam = family(src)
            if fam is None:
                continue
            f = flags(src)
            # ESP32 boards with 4 MB of flash: MeshCore's two OTA slots leave
            # 1.25 MB (1.9 MB with min_spiffs) for the application, less than
            # MeshCore and Reticulum together. One 3 MB slot instead (no OTA
            # over WiFi; these boards are flashed over USB anyway). A larger
            # flash left on one of those tables gets the core's table for its
            # size, two OTA slots of 3.25 or 6.25 MB.
            partitions = None
            if fam in ("esp32", "esp32c6"):
                mb = board_flash_mb(src)
                if mb == 4:
                    partitions = "huge_app.csv"
                elif partition_table(src) in SMALL_PARTITION_TABLES:
                    partitions = f"default_{mb}MB.csv" if mb in (8, 16) else "huge_app.csv"
            # nRF52 layouts that give the application's flash to a second
            # file system (…_extrafs.ld, for the companion's contacts) go back
            # to the standard one: the repeater never uses that file system,
            # and MeshCore with Reticulum needs the room.
            ldscript = None
            ld = option(src, "board_build.ldscript") or ""
            if fam == "nrf52" and ld.endswith("_extrafs.ld") and (ROOT / ld.replace("_extrafs.ld", ".ld")).exists():
                ldscript = ld.replace("_extrafs.ld", ".ld")
            envs.append({
                "env": slug(src[4:] if src.startswith("env:") else src),
                "extends": src,
                "variant": variant,
                "family": fam,
                "radio": define(f, "RADIO_CLASS") or "?",
                "display": define(f, "DISPLAY_CLASS"),
                "headless": headless,
                "partitions": partitions,
                "ldscript": ldscript,
            })

    seen = {}
    for e in envs:
        if e["env"] in seen:
            e["env"] += "_" + str(seen[e["env"]])
        seen[e["env"]] = seen.get(e["env"], 1) + 1
    envs.sort(key=lambda e: e["env"])

    out = [
        "; GENERATED by tools/gen_envs.py from variants/*/platformio.ini — do not edit.",
        "; One Arborisis Mesh environment per LoRa board MeshCore describes.",
        "",
    ]
    for e in envs:
        src = e["extends"]
        ref = src if src.startswith("env:") else src
        fam = "arborisis_" + ("esp32" if e["family"] == "esp32c6" else e["family"])
        out += [
            f"[env:{e['env']}]",
            f"; {e['variant']} — {e['family']}, {e['radio']}" + (", headless" if e["headless"] else ""),
            f"extends = {ref}",
            f"custom_arb_family = {e['family']}",
            f"custom_arb_variant = {e['variant']}",
            *([f"board_build.partitions = {e['partitions']}"] if e.get("partitions") else []),
            *([f"board_build.ldscript = {e['ldscript']}"] if e.get("ldscript") else []),
            # nRF52840: Curve25519 at -Os (tools/arb_size.py), after the variant's own scripts.
            *([f"extra_scripts = ${{{ref}.extra_scripts}}", "  pre:tools/arb_size.py"] if e["family"] == "nrf52" else []),
            "build_flags =",
            f"  ${{{ref}.build_flags}}",
            "  ${arborisis.build_flags}",
            f"  ${{{fam}.build_flags}}",
            f"  -D FIRMWARE_VERSION='\"{fw_ver}\"'",
            f"build_unflags = ${{{fam}.build_unflags}}",
            "build_src_filter =",
            f"  ${{{ref}.build_src_filter}}",
            "  -<../examples/simple_repeater>",
            "  ${arborisis.build_src_filter}",
            f"  ${{{fam}.build_src_filter}}",
            "lib_deps =",
            f"  ${{{ref}.lib_deps}}",
            "  ${arborisis.lib_deps}",
            f"  ${{{fam}.lib_deps}}",
            # arduino-pico's BLE library, which the LDF finds through app/BleLink.cpp's
            # ESP32 includes and cannot build without Bluetooth (MeshCore's own
            # RP2040 environments ignore it the same way).
            *([f"lib_ignore = ${{{fam}.lib_ignore}}"] if e["family"] == "rp2040" else []),
            "",
        ]
    (ROOT / "arborisis_envs.ini").write_text("\n".join(out))
    (ROOT / "docs").mkdir(exist_ok=True)
    (ROOT / "docs" / "boards.json").write_text(json.dumps(envs, indent=1) + "\n")
    fams = {}
    for e in envs:
        fams[e["family"]] = fams.get(e["family"], 0) + 1
    print(f"firmware version {fw_ver}")
    print(f"{len(envs)} environments: " + ", ".join(f"{k} {v}" for k, v in sorted(fams.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
