#pragma once

#include <stdint.h>

struct MuxStats {
    volatile uint32_t sensor_ok;
    volatile uint32_t sensor_checksum_fail;
    volatile uint32_t audio_dropped;
    volatile uint32_t usb_incomplete;
};

extern MuxStats g_stats;
