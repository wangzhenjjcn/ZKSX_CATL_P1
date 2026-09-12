#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// USB CDC multiplex frame:
//   [0xA5 0x5A][type u8][len u16-le][payload][crc16-ccitt u16-le]
// CRC is computed over type + len + payload (not the magic).
// CRC-16-CCITT-FALSE: poly 0x1021, init 0xFFFF, xorout 0x0000.

static const uint8_t kMuxMagic0 = 0xA5;
static const uint8_t kMuxMagic1 = 0x5A;

enum MuxType : uint8_t {
    kMuxSensor = 0x01,  // 70-byte raw sensor frame
    kMuxAudio = 0x02,   // int16 LE PCM, 48 kHz mono
    kMuxSwitch = 0x03,  // 1 byte: bit0=SW1, bit1=SW2, 1=closed
    kMuxStatus = 0x04,  // 4x uint32 LE counters
};

static const uint16_t kSensorFrameLen = 70;
static const uint8_t kSensorHead0 = 0xFF;
static const uint8_t kSensorHead1 = 0x84;

static const uint32_t kAudioSampleRate = 48000;
static const uint16_t kAudioSamplesPerChunk = 960;  // 20 ms
static const uint16_t kAudioChunkBytes = kAudioSamplesPerChunk * sizeof(int16_t);

static const uint16_t kStatusPayloadLen = 16;

static const size_t kMuxHeaderLen = 5;  // magic + type + len
static const size_t kMuxCrcLen = 2;
static const size_t kMuxMaxPayload = kAudioChunkBytes;
static const size_t kMuxMaxPacket = kMuxHeaderLen + kMuxMaxPayload + kMuxCrcLen;

inline uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

inline bool sensor_checksum_ok(const uint8_t *frame) {
    if (frame[0] != kSensorHead0 || frame[1] != kSensorHead1) {
        return false;
    }
    uint32_t sum = 0;
    for (int i = 2; i < 68; ++i) {
        sum += frame[i];
    }
    const uint16_t csum = static_cast<uint16_t>(sum);
    return frame[68] == static_cast<uint8_t>((csum >> 8) & 0xFF) &&
           frame[69] == static_cast<uint8_t>(csum & 0xFF);
}

inline uint16_t sensor_frame_counter(const uint8_t *frame) {
    return (static_cast<uint16_t>(frame[2]) << 8) | frame[3];
}

// Returns total packet size. out must hold at least 7 + payload_len bytes.
inline size_t mux_pack(uint8_t *out, uint8_t type, const uint8_t *payload, uint16_t payload_len) {
    out[0] = kMuxMagic0;
    out[1] = kMuxMagic1;
    out[2] = type;
    out[3] = static_cast<uint8_t>(payload_len & 0xFF);
    out[4] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
    if (payload_len > 0 && payload != nullptr) {
        memcpy(out + kMuxHeaderLen, payload, payload_len);
    }
    const uint16_t crc = crc16_ccitt(out + 2, 3 + payload_len);
    out[kMuxHeaderLen + payload_len] = static_cast<uint8_t>(crc & 0xFF);
    out[kMuxHeaderLen + payload_len + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return kMuxHeaderLen + payload_len + kMuxCrcLen;
}
