#pragma once

#include <stdint.h>

void bat_runtime_init(void);

/** Read VBUS, handle plug/unplug, persist while on battery. */
void bat_runtime_poll(void);

/** Seconds since USB was unplugged, or 0 if VBUS is present. */
uint32_t bat_runtime_on_bat_s(void);
