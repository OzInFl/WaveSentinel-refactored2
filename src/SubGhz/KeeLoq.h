// KeeLoq.h — Microchip HCS300/301 KeeLoq cipher + frame codec
//
// Standard NLF 0x3A5C742E, 64-bit key, 32-bit block, 528 rounds.
// OTA frame layout (66 bits total, transmitted LSB-first):
//   bits[0..31]  — encrypted portion (counter + button + discrim + serial-low)
//   bits[32..59] — 28-bit serial number
//   bits[60..63] — 4-bit button code
//   bits[64..65] — 2 status bits (VLow / Repeat)
//
// All public API takes/returns canonical MSB-first 32-bit words; bit-stream
// conversion is handled in parseFrame/buildFrame so callers don't have to
// think about Flipper's LSB-first ordering.
#pragma once
#include <stdint.h>

namespace KeeLoq {

// ---- core cipher ---------------------------------------------------------
// 32-bit block, 64-bit key, 528-round NLF cipher.
uint32_t encrypt(uint32_t plaintext, uint64_t key);
uint32_t decrypt(uint32_t ciphertext, uint64_t key);

// ---- frame structure -----------------------------------------------------
struct Frame {
    uint32_t encrypted;    // 32-bit encrypted portion
    uint32_t serial;       // 28-bit serial number
    uint8_t  button;       // 4-bit button code
    uint8_t  status;       // 2-bit status (vlow|repeat)
};

// Parse a 66-char "01010..." bit string (LSB-first OTA order) into a Frame.
bool parseFrame(const char *bits66, Frame &out);

// Build a 66-char + null bit string (LSB-first OTA order) from a Frame.
void buildFrame(const Frame &in, char bits66[67]);

// ---- decrypted plaintext fields -----------------------------------------
// HCS300/301 plaintext layout (32 bits):
//   [31..28] button (4)
//   [27..16] discrimination (12) — typically low 12 bits of serial
//   [15..0]  counter (16)
struct Plain {
    uint16_t counter;
    uint16_t discrimination;
    uint8_t  button;
};

Plain    decodePlain(uint32_t encrypted_block, uint64_t key);
uint32_t encodePlain(const Plain &p, uint64_t key);

}  // namespace KeeLoq
