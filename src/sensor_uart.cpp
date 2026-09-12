#include "sensor_uart.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "pins.h"
#include "protocol.h"
#include "stats.h"
#include "usb_mux.h"

static const uint32_t kSensorBaud = 460800;
static const size_t kAccCap = 160;

static uint8_t s_acc[kAccCap];
static size_t s_acc_len = 0;
static bool s_led_on = false;

static void led_toggle() {
    s_led_on = !s_led_on;
    digitalWrite(kPinUserLed, s_led_on ? LOW : HIGH);
}

static int find_header(const uint8_t *buf, size_t len) {
    if (len < 2) {
        return -1;
    }
    for (size_t i = 0; i + 1 < len; ++i) {
        if (buf[i] == kSensorHead0 && buf[i + 1] == kSensorHead1) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static void drain_accumulator() {
    while (true) {
        const int header = find_header(s_acc, s_acc_len);
        if (header < 0) {
            if (s_acc_len > 0 && s_acc[s_acc_len - 1] == kSensorHead0) {
                s_acc[0] = kSensorHead0;
                s_acc_len = 1;
            } else {
                s_acc_len = 0;
            }
            return;
        }
        if (header > 0) {
            memmove(s_acc, s_acc + header, s_acc_len - static_cast<size_t>(header));
            s_acc_len -= static_cast<size_t>(header);
        }
        if (s_acc_len < kSensorFrameLen) {
            return;
        }
        if (sensor_checksum_ok(s_acc)) {
            if (mux_try_enqueue_sensor(s_acc)) {
                g_stats.sensor_ok++;
                led_toggle();
            }
            memmove(s_acc, s_acc + kSensorFrameLen, s_acc_len - kSensorFrameLen);
            s_acc_len -= kSensorFrameLen;
        } else {
            g_stats.sensor_checksum_fail++;
            memmove(s_acc, s_acc + 1, s_acc_len - 1);
            s_acc_len -= 1;
        }
    }
}

static void ingest_uart() {
    while (Serial1.available() > 0) {
        if (s_acc_len >= kAccCap) {
            drain_accumulator();
            if (s_acc_len >= kAccCap) {
                s_acc_len = 0;
            }
        }
        const int b = Serial1.read();
        if (b < 0) {
            break;
        }
        s_acc[s_acc_len++] = static_cast<uint8_t>(b);
    }
    drain_accumulator();
}

void sensor_begin() {
    Serial1.setRxBufferSize(1024);
    Serial1.begin(kSensorBaud, SERIAL_8N1, kPinSensorRx, kPinSensorTx);
}

void sensor_task(void *arg) {
    (void)arg;
    for (;;) {
        ingest_uart();
        vTaskDelay(1);
    }
}
