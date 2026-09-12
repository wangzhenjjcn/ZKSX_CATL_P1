#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "audio_i2s.h"
#include "pins.h"
#include "sensor_uart.h"
#include "switches.h"
#include "usb_mux.h"

void setup() {
    pinMode(kPinUserLed, OUTPUT);
    digitalWrite(kPinUserLed, HIGH);

    Serial.begin(115200);
    const uint32_t wait_start = millis();
    while (!Serial && (millis() - wait_start) < 3000) {
        delay(10);
    }

    mux_begin();
    sensor_begin();
    audio_begin();
    switches_begin();

    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(io_task, "io", 3072, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(mux_task, "usb", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
