/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <cassert>
#include <iostream>

#include "Message.h"
#include "BridgeIn.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

BridgeIn::BridgeIn() {
    // #### TODO: FIGURE OUT WHERE TO MOVE THIS
    _jitBuf.setInitialMargin(40);
}

void BridgeIn::setCodec(CODECType codecType) {
    _codecType = codecType;
    unsigned rate = codecSampleRate(_codecType);
    if (rate == 0)
        assert(false);
    _resampler.setRates(rate, 48000);
}

void BridgeIn::consume(const Message& frame) {    
    assert(frame.getType() == Message::Type::AUDIO ||
           (frame.getType() == Message::Type::SIGNAL && 
            frame.getFormat() == Message::SignalType::RADIO_UNKEY));
    // The first stop is the jitter buffer
    _jitBuf.consume(*_log, frame);
}

/** 
 * Adaptor that links the SequencingBuffer output to a sink function
 */
class JBOutAdaptor : public amp::SequencingBufferSink<Message> {
public:

    JBOutAdaptor(std::function<void(const Message& msg)> sink) 
    :   _sink(sink) {        
    }

    void playSignal(const Message& msg, uint32_t localTime) {   
        _sink(msg);
    }

    void playVoice(const Message& msg, uint32_t localTime) {        
        _sink(msg);
    }

    void interpolateVoice(uint32_t origMs, uint32_t localTime, uint32_t duration) {
        // Need to make a message to represent the interpolate event
        // #### TODO: NOTE: The source/destination aren't filled in. Does this matter?
        Message msg(Message::Type::AUDIO_INTERPOLATE, 0, 0, 0, origMs, localTime * 1000);
        _sink(msg);
    }

private:

    std::function<void(const Message& msg)> _sink = nullptr;
};

/**
 * Every tick we ask the Jitter Buffer to emit whatever it has into a temporary
 * adaptor that will handle forwarding.
 */
void BridgeIn::audioRateTick() {
    JBOutAdaptor adaptor([this](const Message& msg) { this->_handleJitBufOut(msg); });
    _jitBuf.playOut(*_log, _clock->time(), &adaptor);
}

/**
 * This function handles everything in the input flow *after* the jitter buffer. We will 
 * get a frame every tick and it will be either (a) normal voice (b) an interpolation 
 * request (c) an unkey signal.
 * 
 * At this stage in the flow the frame is still encoded as it was received, so this
 * function will take care of PLC, transcoding, resampling, etc.
 */
void BridgeIn::_handleJitBufOut(const Message& frame) {

    if (frame.getType() == Message::Type::AUDIO ||
        frame.getType() == Message::Type::AUDIO_INTERPOLATE) {
    
        if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
            _codecType == CODECType::IAX2_CODEC_SLIN ||
            _codecType == CODECType::IAX2_CODEC_SLIN_16K) { 
            
            Transcoder* t0 = 0;
            if (_codecType == CODECType::IAX2_CODEC_G711_ULAW)
                t0 = &_transcoder0a;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN) 
                t0 = &_transcoder0b;
            else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K) 
                t0 = &_transcoder0c;

            // Make PCM data
            // MAKE SURE THIS IS LARGE ENOUGH!
            int16_t pcm[BLOCK_SIZE_8K * 2];
            if (frame.getType() == Message::Type::AUDIO) {
                t0->decode(frame.body(), frame.size(), pcm, codecBlockSize(_codecType));
            } else {
                t0->decodeGap(pcm, codecBlockSize(_codecType));
            }

            // Resample PCM data up to 48K
            int16_t pcm48k[BLOCK_SIZE_48K];
            _resampler.resample(pcm, codecBlockSize(_codecType), pcm48k, BLOCK_SIZE_48K);
            
            // Transcode to SLIN_48K
            uint8_t slin48k[BLOCK_SIZE_48K * 2];
            _transcoder1.encode(pcm48k, BLOCK_SIZE_48K, slin48k, BLOCK_SIZE_48K * 2);
            Message outFrame(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
                BLOCK_SIZE_48K * 2, slin48k, frame.getOrigMs(), frame.getRxUs());
            outFrame.setSource(frame.getSourceBusId(), frame.getSourceCallId());
            outFrame.setDest(frame.getDestBusId(), frame.getDestCallId());

            if (_sink)
                _sink(outFrame);
        }
        else if (_codecType == CODECType::IAX2_CODEC_SLIN_48K) {                
            // No support for interpolation
            if (frame.getType() == Message::Type::AUDIO) {
                // No conversion needed
                if (_sink)
                    _sink(frame);
            } else
                assert(false);
        }
        else {
            assert(false);
        }
    }
    // Non-audio messages are key passed right through, transcoding
    // not relevant here.
    else {
        if (_sink)
            _sink(frame);
    }
}

}

}
