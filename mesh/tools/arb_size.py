# Copyright (C) 2026, Arborisis — GPL-3.0-or-later
#
# PlatformIO extra script for the nRF52840 images (tools/gen_envs.py adds it,
# as a pre: script so that the libraries are built with it too, to the arb_*
# environments of that family).
#
# The Adafruit nRF52 core builds everything with -Ofast. Most of the code
# does not care, but rweather's Curve25519 (the Crypto library, which
# microReticulum uses) unrolls into ~80 KB at that level, under 3 KB at
# -Os — the room the MeshCore companion needs next to the repeater and
# Reticulum in ~800 KB of application flash. Its speed stays well within
# what a LoRa link asks: one key exchange per Reticulum link or announce.

Import("env")

SIZE_OPTIMISED = ("Curve25519.cpp",)


def optimise_for_size(env, node):
    if not node.name.endswith(SIZE_OPTIMISED):
        return node
    flags = [f for f in env["CCFLAGS"] if not str(f).startswith("-O")] + ["-Os"]
    return env.Object(node, CCFLAGS=flags)


env.AddBuildMiddleware(optimise_for_size)
