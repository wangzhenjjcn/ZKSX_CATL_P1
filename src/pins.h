#pragma once

#include <Arduino.h>

// Pressure sensor UART: sensor TX -> D0, sensor RX -> D1.
static const int kPinSensorRx = D0;  // GPIO1
static const int kPinSensorTx = D1;  // GPIO2

// Sense expansion PDM microphone (do not use D11/D12 for switches).
static const int kPinPdmClk = 42;
static const int kPinPdmData = 41;

// Dry-contact switches to GND, internal pull-up. Closed = LOW.
static const int kPinSwitch1 = D3;  // GPIO4
static const int kPinSwitch2 = D4;  // GPIO5

// User LED (active low). Shared with SD CS if SD is enabled later.
static const int kPinUserLed = LED_BUILTIN;
