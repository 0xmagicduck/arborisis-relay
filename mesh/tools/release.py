#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""Build release images of Arborisis Mesh into dist/, with a manifest.

    python3 tools/release.py arb_heltec_v3 arb_rak_4631 arb_picow
    python3 tools/release.py --all

Per family, the image a user flashes:
  ESP32 / C3 / C6 / S3  <env>_merged.bin  bootloader + partitions + app, at 0x0
                        <env>.bin         app only (update over an existing install)
  nRF52840              <env>.uf2         drag and drop on the bootloader drive
  RP2040                <env>.uf2         drag and drop on RPI-RP2
  STM32WL               <env>.hex         STM32CubeProgrammer

dist/manifest.json lists each file with its SHA-256, size, board and family.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
PIO = os.environ.get("PIO", shutil.which("pio") or str(ROOT.parent / ".venv" / "bin" / "pio"))


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    boards = {b["env"]: b for b in json.loads((ROOT / "docs" / "boards.json").read_text())}
    envs = list(boards) if "--all" in argv else [a for a in argv if not a.startswith("--")]
    if not envs:
        print(__doc__)
        return 2
    ini = (ROOT / "platformio.ini").read_text()
    version = re.search(r"ARB_VERSION='\"([^\"]+)\"'", ini).group(1)
    DIST.mkdir(exist_ok=True)
    manifest = {"firmware": "Arborisis Mesh", "version": version, "files": []}
    failed = []
    for env in envs:
        fam = boards[env]["family"]
        targets = ["mergebin"] if fam.startswith("esp32") else (["create_uf2"] if fam == "nrf52" else [])
        cmd = [PIO, "run", "-e", env] + sum((["-t", t] for t in targets), [])
        # A custom target alone does not build the firmware: build first.
        if subprocess.call([PIO, "run", "-e", env], cwd=ROOT) != 0 or \
           (targets and subprocess.call(cmd, cwd=ROOT) != 0):
            failed.append(env)
            continue
        build = ROOT / ".pio" / "build" / env
        picks = {
            "esp32": [("firmware-merged.bin", "_merged.bin"), ("firmware.bin", ".bin")],
            "esp32c6": [("firmware-merged.bin", "_merged.bin"), ("firmware.bin", ".bin")],
            "nrf52": [("firmware.uf2", ".uf2"), ("firmware.zip", "_dfu.zip")],
            "rp2040": [("firmware.uf2", ".uf2")],
            "stm32": [("firmware.hex", ".hex"), ("firmware.bin", ".bin")],
        }[fam]
        for src, suffix in picks:
            p = build / src
            if not p.exists():
                continue
            name = f"arborisis_mesh_{env[4:]}{suffix}"
            shutil.copy2(p, DIST / name)
            manifest["files"].append({
                "file": name, "env": env, "variant": boards[env]["variant"], "family": fam,
                "radio": boards[env]["radio"], "size": (DIST / name).stat().st_size, "sha256": sha256(DIST / name),
            })
            print(f"dist/{name}")
    (DIST / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    if failed:
        print("failed: " + " ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
