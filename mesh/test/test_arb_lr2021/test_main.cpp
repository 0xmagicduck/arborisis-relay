// Copyright (C) 2026, Arborisis — GPL-3.0-or-later (see ../../../LICENSE)
//
// The arbiter tests of test_arb_arbiter, built for the LR2021. The stand-in
// radio then follows the LR2021 rule. It stays in receive mode after a
// packet and rejects commands until the arbiter puts it in standby.

#define USE_LR2021 1
#include "../test_arb_arbiter/test_main.cpp"
