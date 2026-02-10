#include "IRTransmit.h"

IRTransmit IR_TX;

void IRTransmit::init() {
    irsend = new IRsend(IR_LED);
    irsend->begin();
}

void IRTransmit::sendRaw(const uint16_t rawData[], uint16_t rawLen, uint32_t freqHz) {
    irsend->sendRaw(rawData, rawLen, freqHz / 1000);  // sendRaw takes kHz
}

void IRTransmit::sendSignal(const FlipperIRSignal &sig) {
    if (sig.isRaw) {
        sendRaw(sig.rawData, sig.rawLen, sig.frequency);
    } else {
        uint64_t data = 0;
        uint16_t nbits = 0;

        switch (sig.protocol) {
            case decode_type_t::NEC:
                data = irsend->encodeNEC(sig.address & 0xFFFF, sig.command & 0xFFFF);
                nbits = 32;
                break;
            case decode_type_t::SAMSUNG:
                data = (sig.address << 16) | (sig.command & 0xFFFF);
                nbits = 32;
                break;
            case decode_type_t::SONY:
                data = sig.command | (sig.address << 7);
                nbits = (sig.address > 0xFF) ? 20 : ((sig.address > 0x1F) ? 15 : 12);
                break;
            case decode_type_t::RC5:
                data = (sig.address << 6) | (sig.command & 0x3F);
                nbits = 13;
                break;
            case decode_type_t::RC6:
                data = (sig.address << 8) | (sig.command & 0xFF);
                nbits = 20;
                break;
            case decode_type_t::PANASONIC:
                irsend->sendPanasonic64(((uint64_t)sig.address << 32) | sig.command);
                return;
            case decode_type_t::LG:
                data = (sig.address << 16) | (sig.command & 0xFFFF);
                nbits = 28;
                break;
            case decode_type_t::JVC:
                data = (sig.address << 8) | (sig.command & 0xFF);
                nbits = 16;
                break;
            case decode_type_t::SHARP:
                data = (sig.address << 8) | (sig.command & 0xFF);
                nbits = 15;
                break;
            default:
                data = sig.command;
                nbits = 32;
                break;
        }

        irsend->send(sig.protocol, data, nbits);
    }
}
