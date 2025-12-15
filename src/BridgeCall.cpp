#include <cassert>
#include <iostream>

#include "Message.h"
#include "BridgeCall.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

BridgeCall::BridgeCall() {
    // The last stage of the BridgeIn pipeline drops the message 
    // into the input staging area.
    _bridgeIn.setSink([this](const Message& msg) {
        this->_stageIn = msg;
    });
    // The last stage of the BridgeOut pipeline passes the message
    // to the sink message bus.
    _bridgeOut.setSink([this](const Message& msg) {
        this->_sink->consume(msg);
    });
}

void BridgeCall::reset() {
    _active = false;
    _lineId = 0;  
    _callId = 0; 
    _bridgeIn.reset();
    _bridgeOut.reset();
}

void BridgeCall::setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec) {
    _active = true;
    _lineId = lineId;  
    _callId = callId; 
    _bridgeIn.setCodec(codec);
    _bridgeIn.setStartTime(startMs);
    _bridgeOut.setCodec(codec);
}

void BridgeCall::consume(const Message& frame) {
    if (_bypassAdaptor) {
        _stageIn = frame;
    } else {
        _bridgeIn.consume(frame);
    }
}

void BridgeCall::audioRateTick() {
    if (_bypassAdaptor)
        return;
    _bridgeIn.audioRateTick();
}

void BridgeCall::contributeInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale) const {
    if (_stageIn.getType() == Message::Type::AUDIO) {
        assert(_stageIn.size() == BLOCK_SIZE_48K * 2);
        assert(_stageIn.getFormat() == CODECType::IAX2_CODEC_SLIN_48K);
        const uint8_t* p = _stageIn.body();
        for (unsigned i = 0; i < blockSize; i++, p += 2)
            pcmBlock[i] += scale * (float)unpack_int16_le(p);
    }
}

void BridgeCall::setOutputAudio(const int16_t* source, unsigned blockSize) {
    // Make a message with the new audio
    assert(blockSize == BLOCK_SIZE_48K);
    uint8_t encoded[BLOCK_SIZE_48K * 2];
    uint8_t* p = encoded;
    for (unsigned i = 0; i < blockSize; i++, p += 2)
        pack_int16_le(source[i], p);
    Message audioOut(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K, 
        BLOCK_SIZE_48K * 2, encoded, 0, 0);
    audioOut.setSource(10, 1);
    audioOut.setDest(_lineId, _callId);
    if (_bypassAdaptor) {
        _sink->consume(audioOut);
    } else {
        _bridgeOut.consume(audioOut);
    }
}

    }
}
