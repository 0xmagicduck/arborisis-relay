#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""Build Arborisis Mesh environments one after the other and record the result.

    python3 tools/build_matrix.py                 # every arb_* environment
    python3 tools/build_matrix.py arb_heltec_v3 arb_rak_4631
    python3 tools/build_matrix.py --failed        # only those that failed last time
    python3 tools/build_matrix.py --clean         # remove each build and libdeps dir afterwards (disk)
    python3 tools/build_matrix.py --reclassify    # re-read the logs of the failures, build nothing

Builds run strictly in sequence: PlatformIO cleans every build directory
when platformio.ini changes and shares one package store, so two builds at
once, or an edit during a build, give failures that are not the code's.

Results go to docs/build-results.json (merged with what is there), then
tools/matrix.py rewrites docs/MATERIEL.md.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# ARB_RESULTS: somewhere else while a long pass runs (the file is in git).
RESULTS = Path(os.environ.get("ARB_RESULTS", ROOT / "docs" / "build-results.json"))
PIO = os.environ.get("PIO", shutil.which("pio") or str(ROOT.parent / ".venv" / "bin" / "pio"))


def refused_download(text):
    """A dependency that could not be downloaded is not the code's failure
    (a sandbox, an offline machine): the URL, or None."""
    d = re.search(r"PackageException: Got the unrecognized status code '(\d+)' when downloaded (\S+)", text)
    return f"download refused ({d.group(1)}): {d.group(2)}" if d else None


def reclassify(results, logdir):
    for env, r in results.items():
        log = logdir / f"{env}.log"
        if r.get("ok") or not log.exists():
            continue
        why = refused_download(log.read_text(errors="ignore"))
        if why:
            r["blocked"], r["error"] = True, why
    RESULTS.write_text(json.dumps(results, indent=1, sort_keys=True) + "\n")


def main(argv):
    clean = "--clean" in argv
    failed_only = "--failed" in argv
    names = [a for a in argv if not a.startswith("--")]
    boards = json.loads((ROOT / "docs" / "boards.json").read_text())
    results = json.loads(RESULTS.read_text()) if RESULTS.exists() else {}
    if not names:
        names = [b["env"] for b in boards]
        if failed_only:
            names = [n for n in names if results.get(n, {}).get("ok") is not True]
    logdir = ROOT / ".pio" / "matrix-logs"
    logdir.mkdir(parents=True, exist_ok=True)
    if "--reclassify" in argv:
        reclassify(results, logdir)
        subprocess.call([sys.executable, str(ROOT / "tools" / "matrix.py")], cwd=ROOT)
        return 0

    for i, env in enumerate(names, 1):
        t0 = time.time()
        log = logdir / f"{env}.log"
        with open(log, "w") as f:
            rc = subprocess.call([PIO, "run", "-e", env], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
        text = log.read_text(errors="ignore")
        ram = re.findall(r"RAM:\s+\[.*?\]\s+([\d.]+)% \(used (\d+) bytes from (\d+) bytes\)", text)
        fl = re.findall(r"Flash:\s+\[.*?\]\s+([\d.]+)% \(used (\d+) bytes from (\d+) bytes\)", text)
        err = ""
        blocked = False
        if rc != 0:
            m = re.search(r"^.*(error:|overflowed by|Error \d).*$", text, re.M)
            err = m.group(0).strip()[:300] if m else "failed (see log)"
            why = refused_download(text)
            if why:
                blocked, err = True, why
        results[env] = {
            "ok": rc == 0,
            "ram_pct": float(ram[-1][0]) if ram else None,
            "ram_used": int(ram[-1][1]) if ram else None,
            "ram_total": int(ram[-1][2]) if ram else None,
            "flash_pct": float(fl[-1][0]) if fl else None,
            "flash_used": int(fl[-1][1]) if fl else None,
            "flash_total": int(fl[-1][2]) if fl else None,
            "error": err,
            "blocked": blocked,
            "seconds": round(time.time() - t0),
        }
        print(f"[{i}/{len(names)}] {env}: {'OK' if rc == 0 else 'FAILED'}"
              + (f"  RAM {results[env]['ram_pct']}%  flash {results[env]['flash_pct']}%" if rc == 0 else f"  {err}"),
              flush=True)
        RESULTS.write_text(json.dumps(results, indent=1, sort_keys=True) + "\n")
        if clean:   # ~150 MB of libraries per board: 94 boards do not fit on a small disk
            shutil.rmtree(ROOT / ".pio" / "build" / env, ignore_errors=True)
            shutil.rmtree(ROOT / ".pio" / "libdeps" / env, ignore_errors=True)

    subprocess.call([sys.executable, str(ROOT / "tools" / "matrix.py")], cwd=ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
