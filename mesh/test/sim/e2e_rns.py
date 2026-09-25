#!/usr/bin/env python3
# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
"""End-to-end check of the RNode host protocol against real Reticulum.

Two Reticulum instances (the `rns` Python package, its own RNodeInterface)
each open one end of rnode_air, which joins them through the firmware's
RNodeHost, rnodeSplit and RNodeReassembler. The test passes when:

  • both interfaces come up — detection, firmware version, and the echo of
    every radio parameter satisfied RNS;
  • the sink's announce crosses the air and gives the source a path;
  • a small and a large (split in two LoRa frames) packet reach the sink,
    and their delivery proofs come back.

    pip install rns
    python3 test/sim/e2e_rns.py

No hardware involved: this is the protocol and the framing, not the radio.
"""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent.parent / "app"

PEER = r'''
import os, sys, time, hashlib
import RNS
role, cfgdir, port, shared = sys.argv[1:5]
os.makedirs(cfgdir, exist_ok=True)
open(os.path.join(cfgdir, "config"), "w").write(f"""
[reticulum]
  enable_transport = False
  share_instance = No
  panic_on_interface_error = No
[logging]
  loglevel = 3
[interfaces]
  [[Arborisis RNode]]
    type = RNodeInterface
    enabled = yes
    port = {port}
    frequency = 869525000
    bandwidth = 125000
    txpower = 14
    spreadingfactor = 8
    codingrate = 5
""")
r = RNS.Reticulum(cfgdir)
iface = [i for i in RNS.Transport.interfaces if "RNode" in str(i)][0]
t0 = time.time()
while not iface.online and time.time() - t0 < 30: time.sleep(0.2)
open(os.path.join(shared, role + ".online"), "w").write("1" if iface.online else "0")
if not iface.online: sys.exit(3)

if role == "sink":
    ident = RNS.Identity()
    dest = RNS.Destination(ident, RNS.Destination.IN, RNS.Destination.SINGLE, "arbtest", "sink")
    dest.set_proof_strategy(RNS.Destination.PROVE_ALL)
    got = []
    def on_packet(data, packet):
        got.append(len(data))
        open(os.path.join(shared, "sink.got"), "a").write(f"{len(data)} {hashlib.sha256(data).hexdigest()}\n")
    dest.set_packet_callback(on_packet)
    open(os.path.join(shared, "sink.hash"), "w").write(dest.hash.hex())
    end = time.time() + 90
    while time.time() < end:
        dest.announce()
        for _ in range(50):
            if os.path.exists(os.path.join(shared, "source.done")): sys.exit(0)
            time.sleep(0.1)
    sys.exit(0)

else:
    hp = os.path.join(shared, "sink.hash")
    while not os.path.exists(hp): time.sleep(0.2)
    h = bytes.fromhex(open(hp).read().strip())
    t0 = time.time()
    while not RNS.Transport.has_path(h) and time.time() - t0 < 60: time.sleep(0.2)
    if not RNS.Transport.has_path(h):
        open(os.path.join(shared, "source.result"), "w").write("no path\n"); sys.exit(4)
    ident = RNS.Identity.recall(h)
    dest = RNS.Destination(ident, RNS.Destination.OUT, RNS.Destination.SINGLE, "arbtest", "sink")
    results = []
    for size in (20, 380):
        data = bytes((i * 7 + size) & 0xFF for i in range(size))
        receipt = RNS.Packet(dest, data).send()
        t1 = time.time()
        while receipt.status == RNS.PacketReceipt.SENT and time.time() - t1 < 30: time.sleep(0.1)
        results.append(f"{size} {hashlib.sha256(data).hexdigest()} {'delivered' if receipt.status == RNS.PacketReceipt.DELIVERED else 'status=' + str(receipt.status)}")
    open(os.path.join(shared, "source.result"), "w").write("\n".join(results) + "\n")
    open(os.path.join(shared, "source.done"), "w").write("1")
    sys.exit(0)
'''


def main():
    work = Path(tempfile.mkdtemp(prefix="arb-e2e-"))
    sim = work / "rnode_air"
    subprocess.check_call(["g++", "-std=c++17", "-O1", "-I", str(APP), str(HERE / "rnode_air.cpp"), "-o", str(sim)])
    (work / "peer.py").write_text(PEER)
    shared = work / "shared"
    shared.mkdir()
    air = subprocess.Popen([str(sim), str(work / "ttyA"), str(work / "ttyB"), "150"], stdout=subprocess.PIPE, text=True)
    assert air.stdout.readline().startswith("ready")
    py = sys.executable
    sink = subprocess.Popen([py, str(work / "peer.py"), "sink", str(work / "rnsB"), str(work / "ttyB"), str(shared)])
    source = subprocess.Popen([py, str(work / "peer.py"), "source", str(work / "rnsA"), str(work / "ttyA"), str(shared)])
    rc_source = source.wait(timeout=180)
    rc_sink = sink.wait(timeout=120)
    air.terminate()
    tail = air.communicate(timeout=10)[0]

    ok = True
    for role in ("sink", "source"):
        f = shared / f"{role}.online"
        online = f.exists() and f.read_text() == "1"
        print(f"{role}: interface {'online' if online else 'NOT online'}")
        ok &= online
    result = (shared / "source.result").read_text() if (shared / "source.result").exists() else ""
    got = (shared / "sink.got").read_text() if (shared / "sink.got").exists() else ""
    print("source:", result.strip().replace("\n", " | "))
    print("sink got:", got.strip().replace("\n", " | "))
    print("air:", tail.strip())
    for line in result.strip().splitlines():
        size, digest, status = line.split(" ", 2)
        ok &= status == "delivered" and digest in got
    ok &= len(result.strip().splitlines()) == 2
    # The 380-byte packet went over the air as two LoRa frames.
    split = int(tail.split("split_packets=")[1].split()[0]) if "split_packets=" in tail else 0
    ok &= split >= 1
    print("E2E", "PASS" if ok and rc_source == 0 else f"FAIL (source rc={rc_source}, sink rc={rc_sink})")
    return 0 if ok and rc_source == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
