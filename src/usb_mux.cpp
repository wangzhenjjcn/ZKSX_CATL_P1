#include "usb_mux.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "stats.h"

MuxStats g_stats = {};

static const uint8_t kSensorQueueDepth = 8;
static const uint8_t kAudioQueueDepth = 3;

static QueueHandle_t s_sensor_q = nullptr;
static QueueHandle_t s_audio_q = nullptr;

static volatile uint8_t s_switch_bits = 0;
static volatile bool s_switch_pending = false;
static volatile bool s_status_pending = false;

void mux_begin() {
    s_sensor_q = xQueueCreate(kSensorQueueDepth, kSensorFrameLen);
    s_audio_q = xQueueCreate(kAudioQueueDepth, kAudioChunkBytes);
}

bool mux_try_enqueue_sensor(const uint8_t frame[kSensorFrameLen]) {
    if (s_sensor_q == nullptr) {
        return false;
    }
    if (xQueueSend(s_sensor_q, frame, 0) == pdTRUE) {
        return true;
    }
    uint8_t discarded[kSensorFrameLen];
    xQueueReceive(s_sensor_q, discarded, 0);
    return xQueueSend(s_sensor_q, frame, 0) == pdTRUE;
}

bool mux_try_enqueue_audio(const uint8_t *pcm, uint16_t nbytes) {
    if (s_audio_q == nullptr || pcm == nullptr || nbytes != kAudioChunkBytes) {
        return false;
    }
    if (xQueueSend(s_audio_q, pcm, 0) != pdTRUE) {
        g_stats.audio_dropped++;
        return false;
    }
    return true;
}

void mux_try_enqueue_switch(uint8_t bits) {
    s_switch_bits = bits;
    s_switch_pending = true;
}

void mux_try_enqueue_status() {
    s_status_pending = true;
}

static bool usb_write_all(const uint8_t *data, size_t n, uint32_t timeout_ms) {
    if (!Serial) {
        return false;
    }
    const uint32_t start = millis();
    size_t off = 0;
    while (off < n) {
        if (!Serial) {
            return false;
        }
        const int avail = Serial.availableForWrite();
        if (avail <= 0) {
            if ((millis() - start) > timeout_ms) {
                return false;
            }
            vTaskDelay(1);
            continue;
        }
        const size_t chunk = static_cast<size_t>(avail) < (n - off) ? static_cast<size_t>(avail) : (n - off);
        const size_t written = Serial.write(data + off, chunk);
        if (written == 0) {
            if ((millis() - start) > timeout_ms) {
                return false;
            }
            vTaskDelay(1);
            continue;
        }
        off += written;
    }
    return true;
}

static bool send_packet(uint8_t type, const uint8_t *payload, uint16_t len, uint32_t timeout_ms) {
    uint8_t packet[kMuxMaxPacket];
    const size_t n = mux_pack(packet, type, payload, len);
    if (!usb_write_all(packet, n, timeout_ms)) {
        g_stats.usb_incomplete++;
        return false;
    }
    return true;
}

static void pack_status(uint8_t out[kStatusPayloadLen]) {
    const uint32_t values[4] = {
        g_stats.sensor_ok,
        g_stats.sensor_checksum_fail,
        g_stats.audio_dropped,
        g_stats.usb_incomplete,
    };
    for (int i = 0; i < 4; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>(values[i] & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((values[i] >> 16) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>((values[i] >> 24) & 0xFF);
    }
}

void mux_task(void *arg) {
    (void)arg;
    uint8_t sensor[kSensorFrameLen];
    uint8_t audio[kAudioChunkBytes];
    uint8_t status[kStatusPayloadLen];

    for (;;) {
        if (s_sensor_q != nullptr && xQueueReceive(s_sensor_q, sensor, 0) == pdTRUE) {
            send_packet(kMuxSensor, sensor, kSensorFrameLen, 20);
            continue;
        }

        if (s_switch_pending) {
            const uint8_t bits = s_switch_bits;
            s_switch_pending = false;
            send_packet(kMuxSwitch, &bits, 1, 10);
            continue;
        }

        if (s_status_pending) {
            s_status_pending = false;
            pack_status(status);
            send_packet(kMuxStatus, status, kStatusPayloadLen, 10);
            continue;
        }

        if (s_audio_q != nullptr && xQueueReceive(s_audio_q, audio, 0) == pdTRUE) {
            if (!send_packet(kMuxAudio, audio, kAudioChunkBytes, 2)) {
                g_stats.audio_dropped++;
            }
            continue;
        }

        vTaskDelay(1);
    }
}
