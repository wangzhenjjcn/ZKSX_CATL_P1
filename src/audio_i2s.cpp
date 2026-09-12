#include "audio_i2s.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined __has_include && __has_include(<ESP_I2S.h>)
#include <ESP_I2S.h>
#define AUDIO_USE_ESP_I2S 1
#else
#include <driver/i2s.h>
#define AUDIO_USE_ESP_I2S 0
#endif

#include "pins.h"
#include "protocol.h"
#include "usb_mux.h"

static bool s_ready = false;

#if AUDIO_USE_ESP_I2S
static I2SClass s_i2s;
#endif

bool audio_ready() {
    return s_ready;
}

void audio_begin() {
#if AUDIO_USE_ESP_I2S
    s_i2s.setPinsPdmRx(kPinPdmClk, kPinPdmData);
    s_ready = s_i2s.begin(I2S_MODE_PDM_RX, kAudioSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    if (s_ready) {
        s_i2s.setTimeout(50);
    }
#else
    const i2s_config_t cfg = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate = static_cast<int>(kAudioSampleRate),
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = kAudioSamplesPerChunk,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    const i2s_pin_config_t pins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = kPinPdmClk,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = kPinPdmData,
    };
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        s_ready = false;
        return;
    }
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        s_ready = false;
        return;
    }
    i2s_set_clk(I2S_NUM_0, kAudioSampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    s_ready = true;
#endif
}

void audio_task(void *arg) {
    (void)arg;
    uint8_t chunk[kAudioChunkBytes];

    if (!s_ready) {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    for (;;) {
        size_t filled = 0;
        while (filled < kAudioChunkBytes) {
#if AUDIO_USE_ESP_I2S
            const size_t got = s_i2s.readBytes(
                reinterpret_cast<char *>(chunk + filled), kAudioChunkBytes - filled);
#else
            size_t got = 0;
            i2s_read(I2S_NUM_0, chunk + filled, kAudioChunkBytes - filled, &got, pdMS_TO_TICKS(50));
#endif
            if (got == 0) {
                vTaskDelay(1);
                continue;
            }
            filled += got;
        }
        mux_try_enqueue_audio(chunk, kAudioChunkBytes);
    }
}
