// KeeLoq.cpp — Microchip HCS300/301 KeeLoq cipher + frame codec.
//
// Algorithm reference: Microchip "An Introduction to KeeLoq Code Hopping"
// (AN642). The cipher is public-domain; this is the canonical implementation
// used by every KeeLoq tool (RFCat, Flipper, Wagner, Eisenecker, ...).
//
// NLF table is the standard 0x3A5C742E.
//
// Bit ordering note: OTA bits are LSB-first. parseFrame() reads the input
// bit string left→right as the LSB-first stream and reconstructs MSB-first
// integers for use with encrypt/decrypt. buildFrame() does the inverse.

#include "KeeLoq.h"
#include <string.h>

namespace KeeLoq {

// NLF lookup — bit `i` of 0x3A5C742E is NLF(b4,b3,b2,b1,b0=i)
static inline uint8_t nlf(uint32_t x) {
    return (uint8_t)((0x3A5C742Eu >> (x & 0x1F)) & 1);
}

// Helper to fetch bit `b` of a 32-bit word (b=0 is LSB).
static inline uint8_t bit32(uint32_t v, uint8_t b) { return (uint8_t)((v >> b) & 1); }

// 528-round KeeLoq encryption.
// Per AN642: NLF index = b1 b9 b20 b26 b31 of state; XOR with state b0, b16,
// and one keystream bit, shift state right by 1, place result in MSB.
uint32_t encrypt(uint32_t plaintext, uint64_t key) {
    uint32_t x = plaintext;
    for (int i = 0; i < 528; ++i) {
        uint32_t nlf_in = (bit32(x, 1)  << 0) |
                          (bit32(x, 9)  << 1) |
                          (bit32(x, 20) << 2) |
                          (bit32(x, 26) << 3) |
                          (bit32(x, 31) << 4);
        uint8_t  k_bit  = (uint8_t)((key >> (i & 63)) & 1);
        uint8_t  msb    = (uint8_t)(bit32(x, 0) ^ bit32(x, 16) ^ nlf(nlf_in) ^ k_bit);
        x = (x >> 1) | ((uint32_t)msb << 31);
    }
    return x;
}

// 528-round KeeLoq decryption — inverse of encrypt(). Same NLF taps from the
// post-shift state, but rotates LEFT and consumes the key in reverse.
uint32_t decrypt(uint32_t ciphertext, uint64_t key) {
    uint32_t x = ciphertext;
    for (int i = 0; i < 528; ++i) {
        // Bit indices shift down by 1 vs. encrypt because we're undoing a
        // right-shift: read pre-image taps at positions (n-1) of encrypt.
        uint32_t nlf_in = (bit32(x, 0)  << 0) |
                          (bit32(x, 8)  << 1) |
                          (bit32(x, 19) << 2) |
                          (bit32(x, 25) << 3) |
                          (bit32(x, 30) << 4);
        // Key bits consumed in reverse: round i uses key bit (527 - i) mod 64.
        uint8_t  k_bit  = (uint8_t)((key >> ((527 - i) & 63)) & 1);
        uint8_t  lsb    = (uint8_t)(bit32(x, 31) ^ bit32(x, 15) ^ nlf(nlf_in) ^ k_bit);
        x = (x << 1) | (uint32_t)lsb;
    }
    return x;
}

// -------- frame bit-stream codec --------
//
// OTA wire ordering for KeeLoq is LSB-first per field. The 66-char input
// string `bits66` is read left→right as the temporal bit stream:
//   bits66[0..31]  = encrypted block, bit 0 first
//   bits66[32..59] = serial, bit 0 first  (28 bits)
//   bits66[60..63] = button, bit 0 first  (4 bits)
//   bits66[64..65] = status, bit 0 first  (2 bits)
//
// We reassemble each field as a normal MSB-first integer so the cipher
// and downstream display code can treat them as ordinary numbers.

static uint32_t bits_lsb_first(const char *p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        if (p[i] == '1') v |= (1u << i);
    }
    return v;
}

static void put_bits_lsb_first(char *p, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) {
        p[i] = ((v >> i) & 1) ? '1' : '0';
    }
}

bool parseFrame(const char *bits66, Frame &out) {
    if (!bits66) return false;
    // Validate length and characters.
    for (int i = 0; i < 66; ++i) {
        char c = bits66[i];
        if (c != '0' && c != '1') return false;
    }
    out.encrypted = bits_lsb_first(bits66 + 0,  32);
    out.serial    = bits_lsb_first(bits66 + 32, 28);
    out.button    = (uint8_t)bits_lsb_first(bits66 + 60, 4);
    out.status    = (uint8_t)bits_lsb_first(bits66 + 64, 2);
    return true;
}

void buildFrame(const Frame &in, char bits66[67]) {
    put_bits_lsb_first(bits66 + 0,  in.encrypted, 32);
    put_bits_lsb_first(bits66 + 32, in.serial & 0x0FFFFFFFu, 28);
    put_bits_lsb_first(bits66 + 60, in.button   & 0x0F,       4);
    put_bits_lsb_first(bits66 + 64, in.status   & 0x03,       2);
    bits66[66] = '\0';
}

// -------- plaintext field codec --------
Plain decodePlain(uint32_t encrypted_block, uint64_t key) {
    uint32_t pt = decrypt(encrypted_block, key);
    Plain p;
    p.counter        = (uint16_t)(pt & 0xFFFF);
    p.discrimination = (uint16_t)((pt >> 16) & 0x0FFF);
    p.button         = (uint8_t)((pt >> 28) & 0x0F);
    return p;
}

uint32_t encodePlain(const Plain &p, uint64_t key) {
    uint32_t pt = ((uint32_t)(p.button & 0x0F) << 28)
                | (((uint32_t)p.discrimination & 0x0FFF) << 16)
                | (uint32_t)p.counter;
    return encrypt(pt, key);
}

}  // namespace KeeLoq
