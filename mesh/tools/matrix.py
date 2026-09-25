#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""Write docs/MATERIEL.md: every board, its MCU family and radio, and what
the last builds (docs/build-results.json, from tools/build_matrix.py) said."""

import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FAMILY = {
    "esp32": "ESP32 / S3 / C3",
    "esp32c6": "ESP32-C6",
    "nrf52": "nRF52840",
    "rp2040": "RP2040",
    "stm32": "STM32WL",
}
RADIO = {
    "CustomSX1262": "SX1262",
    "CustomSX1268": "SX1268",
    "CustomSX1276": "SX1276",
    "CustomLLCC68": "LLCC68",
    "CustomLR1110": "LR1110",
    "CustomLR2021": "LR2021",
    "CustomSTM32WLx": "STM32WL",
}


def main():
    boards = json.loads((ROOT / "docs" / "boards.json").read_text())
    rpath = ROOT / "docs" / "build-results.json"
    results = json.loads(rpath.read_text()) if rpath.exists() else {}

    built = [b for b in boards if b["env"] in results]
    ok = [b for b in built if results[b["env"]]["ok"]]
    fams = Counter(b["family"] for b in boards)
    radios = Counter(RADIO.get(b["radio"], b["radio"]) for b in boards)

    out = [
        "# Cartes prises en charge",
        "",
        "*Généré par `tools/matrix.py` à partir de `docs/boards.json` (`tools/gen_envs.py`)",
        "et `docs/build-results.json` (`tools/build_matrix.py`). Ne pas éditer à la main.*",
        "",
        f"**{len(boards)} environnements** pour **{len({b['variant'] for b in boards})} cartes** "
        f"(variantes MeshCore). Compilés lors de la dernière passe : {len(built)}, "
        f"réussis : **{len(ok)}**.",
        "",
        "Familles : " + ", ".join(f"{FAMILY.get(k, k)} {v}" for k, v in sorted(fams.items())) + ".  ",
        "Radios : " + ", ".join(f"{k} {v}" for k, v in sorted(radios.items())) + ".",
        "",
        "Reticulum sur la carte : toutes les familles sauf STM32WL (64 Ko de RAM), où la carte",
        "reste un modem RNode pour un hôte et un répéteur MeshCore. « Écran » : la classe",
        "d'affichage de la variante MeshCore (vide = sans écran).",
        "",
        "| Environnement | Carte (variante) | MCU | Radio | Écran | Compilation | RAM | Flash |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for b in boards:
        r = results.get(b["env"])
        if r is None:
            status, ram, fl = "—", "", ""
        elif r["ok"]:
            status = "✅"
            ram = f"{r['ram_pct']} %" if r.get("ram_pct") is not None else ""
            fl = f"{r['flash_pct']} %" if r.get("flash_pct") is not None else ""
        else:
            status, ram, fl = "❌ " + (r.get("error") or "").replace("|", "/")[:80], "", ""
        disp = b.get("display") or ""
        out.append(f"| `{b['env']}` | {b['variant']} | {FAMILY.get(b['family'], b['family'])} | "
                   f"{RADIO.get(b['radio'], b['radio'])} | {disp} | {status} | {ram} | {fl} |")
    out.append("")
    (ROOT / "docs" / "MATERIEL.md").write_text("\n".join(out))
    print(f"docs/MATERIEL.md: {len(boards)} environments, {len(ok)}/{len(built)} built OK")


if __name__ == "__main__":
    main()
