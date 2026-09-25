// Host stand-in for the few Arduino calls app/RadioArbiter.cpp makes. Time
// is virtual: the simulation advances it.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern uint32_t g_sim_now_ms;
inline uint32_t millis() { return g_sim_now_ms; }
inline uint32_t micros() { return g_sim_now_ms * 1000; }
inline long random(long lo, long hi) { return lo + (rand() % (hi - lo)); }
void sim_yield();                 // advances virtual time inside busy waits
inline void yield() { sim_yield(); }
