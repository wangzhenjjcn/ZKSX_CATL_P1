#include "switches.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"
#include "usb_mux.h"

static const uint32_t kPollMs = 5;
static const uint32_t kDebounceMs = 25;
static const uint32_t kHeartbeatMs = 200;
static const uint32_t kStatusMs = 1000;

static uint8_t read_raw_bits() {
    uint8_t bits = 0;
    if (digitalRead(kPinSwitch1) == LOW) {
        bits |= 0x01;
    }
    if (digitalRead(kPinSwitch2) == LOW) {
        bits |= 0x02;
    }
    return bits;
}

void switches_begin() {
    pinMode(kPinSwitch1, INPUT_PULLUP);
    pinMode(kPinSwitch2, INPUT_PULLUP);
}

void io_task(void *arg) {
    (void)arg;
    uint8_t stable = read_raw_bits();
    uint8_t candidate = stable;
    uint32_t candidate_since = millis();
    uint32_t last_heartbeat = 0;
    uint32_t last_status = 0;

    mux_try_enqueue_switch(stable);

    for (;;) {
        const uint32_t now = millis();
        const uint8_t raw = read_raw_bits();

        if (raw != candidate) {
            candidate = raw;
            candidate_since = now;
        } else if (candidate != stable && (now - candidate_since) >= kDebounceMs) {
            stable = candidate;
            mux_try_enqueue_switch(stable);
            last_heartbeat = now;
        }

        if ((now - last_heartbeat) >= kHeartbeatMs) {
            mux_try_enqueue_switch(stable);
            last_heartbeat = now;
        }

        if ((now - last_status) >= kStatusMs) {
            mux_try_enqueue_status();
            last_status = now;
        }

        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}
