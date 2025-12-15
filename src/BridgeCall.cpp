#include <cassert>
#include <iostream>

#include "Message.h"
#include "BridgeCall.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

void BridgeCall::consume(const Message& frame) {
    if (_bypassAdaptor) {
        _stageIn = frame;
    }
}

void BridgeCall::audioRateTick() {
}

void BridgeCall::contributeInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale) const {
    if (_stageIn.getType() == Message::Type::AUDIO) {
        assert(blockSize * 2 == _stageIn.size());
        int16_t* pcm = (int16_t*)_stageIn.body();
        for (unsigned i = 0; i < blockSize; i++)
            pcmBlock[i] += scale * (float)pcm[i];
    }
}

void BridgeCall::setOutputAudio(const int16_t* pcmBlock, unsigned blockSize) {
    if (_bypassAdaptor) {
        // Make a message with the new audio
        // #### TODO: WORK ON TIMESTAMPS
        Message audioOut(Message::Type::SIGNAL, CODECType::IAX2_CODEC_SLIN_48K, 
            blockSize * 2, (const uint8_t*)pcmBlock, 0, 0);
        audioOut.setSource(10, 1);
        audioOut.setDest(_lineId, _callId);
        _sink->consume(audioOut);
    }
}

    }
}
