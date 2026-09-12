#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

void mux_begin();
void mux_task(void *arg);

// Never blocks. Sensor/switch/status keep latest if the queue is full.
// Audio is dropped when the queue is full (backpressure).
bool mux_try_enqueue_sensor(const uint8_t frame[kSensorFrameLen]);
bool mux_try_enqueue_audio(const uint8_t *pcm, uint16_t nbytes);
void mux_try_enqueue_switch(uint8_t bits);
void mux_try_enqueue_status();
