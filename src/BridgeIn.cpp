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

// How long after the last audio packet until we decide the link is 
// inactive.
#define ACTIVE_TIMEOUT_MS (100)
// The definition of "recent"
#define RECENT_TIMEOUT_MS (2000)

namespace kc1fsz {

    namespace amp {

BridgeIn::BridgeIn() {
    // Kerchunk filter just passes through
    _kerchunkFilter.setSink(
        [this](const Message& frame) { _sink(frame); }
    );
}

void BridgeIn::setCodec(CODECType codecType) {
    _codecType = codecType;
    unsigned rate = codecSampleRate(_codecType);
    if (rate == 0)
        assert(false);
    _resampler.setRates(rate, 48000);
    if (rate == 8000)
        _plc.setSampleRate(8000);
    else if (rate == 16000)
        _plc.setSampleRate(16000);
    else if (rate == 48000) {
        // #### TODO: SETUP THE PLC FOR THIS CASE
    }
    else assert(false);
}

void BridgeIn::setJitterBufferInitialMargin(unsigned ms) {
    _jitBuf.setInitialMargin(ms);
}

bool BridgeIn::isActiveRecently() const {
    return _clock->isInWindow(_lastAudioMs, RECENT_TIMEOUT_MS);
}

void BridgeIn::consume(const Message& frame) {    

    if (frame.getType() == Message::Type::AUDIO) {

        _lastAudioMs = _clock->timeMs();

        // Look for leading edge of active status
        if (!_activeStatus) {
            _activeStatus = true;
            _lastActiveStatusChangedMs = _lastAudioMs;
        }

        // The first stop in the pipeline is the jitter buffer
        _jitBuf.consume(*_log, frame);
    }
    else if (frame.getType() == Message::Type::SIGNAL) {
        if (frame.getFormat() == Message::SignalType::RADIO_UNKEY) {
            _lastUnkeyMs = _clock->time();
        }
        else {
            assert(false);
        }
    }
    else {
        assert(false);
    }
}

/** 
 * Adaptor that links the SequencingBuffer output to a sink function
 */
class JBOutAdaptor : public amp::SequencingBufferSink<Message> {
public:

    JBOutAdaptor(std::function<void(const Message& msg)> sink) 
    :   _sink(sink) { }

    void play(const Message& msg, uint32_t) {   
        _sink(msg);
    }

    void interpolate(uint32_t origMs, uint32_t localMs, uint32_t) {
        // Need to make a message to represent the interpolate event
        // #### TODO: NOTE: The source/destination aren't filled in. Does this matter?
        Message msg(Message::Type::AUDIO_INTERPOLATE, 0, 0, 0, origMs, localMs);
        _sink(msg);
    }

private:

    std::function<void(const Message& msg)> _sink = nullptr;
};

/**
 * Every tick we ask the Jitter Buffer to emit whatever it has into a temporary
 * adaptor that will handle forwarding.
 */
void BridgeIn::audioRateTick(uint32_t tickMs) {

    JBOutAdaptor adaptor([this](const Message& msg) { 
        this->_handleJitBufOut(msg); 
    });
    _jitBuf.playOut(*_log, _clock->time(), &adaptor);

    // Let the kerchunk filter contribute output if necessary
    _kerchunkFilter.audioRateTick(tickMs);

    // Look for trailing edge of active status
    if (_activeStatus && _clock->isPastWindow(_lastAudioMs, ACTIVE_TIMEOUT_MS)) {
        _activeStatus = false;
        _lastActiveStatusChangedMs = _clock->timeMs();
    }
}

/**
 * This function handles everything in the input flow *after* the jitter buffer. We will 
 * get a frame every tick and it will be either (a) normal voice (b) an interpolation 
 * request.
 * 
 * At this stage in the flow the frame is still encoded as it was received, so this
 * function will take care of PLC, transcoding, resampling, etc.
 */
void BridgeIn::_handleJitBufOut(const Message& frame) {

    if (frame.getType() == Message::Type::AUDIO ||
        frame.getType() == Message::Type::AUDIO_INTERPOLATE) {
    
        int16_t pcm48k[BLOCK_SIZE_48K];
        int16_t pcm2[BLOCK_SIZE_48K];

        // #### TODO: REORGANIZE TO ELIMINATE REPEATED CODE. EX:
        // #### ALLOW THE PLC TO DEAL WITH 48K SMOOTHLY TO ELIMINATE
        // #### THE SPECIAL CASE.

        if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
            _codecType == CODECType::IAX2_CODEC_SLIN_8K) {
            if (frame.getType() == Message::Type::AUDIO) {
                int16_t pcm1[BLOCK_SIZE_8K];
                Transcoder* tc = 0;
                if (_codecType == CODECType::IAX2_CODEC_G711_ULAW)
                    tc = &_transcoder0a;
                else if (_codecType == CODECType::IAX2_CODEC_SLIN_8K)
                    tc = &_transcoder0b;
                else 
                    assert(false);
                // Transcode
                tc->decode(frame.body(), frame.size(), pcm1, codecBlockSize(_codecType));            
                // Pass audio through the PLC mechanism. 
                // PLC operates on 10ms blocks so there are two calls
                _plc.goodFrame(pcm1, pcm2, BLOCK_SIZE_8K / 2);
                _plc.goodFrame(pcm1 + BLOCK_SIZE_8K / 2, pcm2 + BLOCK_SIZE_8K / 2, 
                    BLOCK_SIZE_8K / 2);
            } else {
                // Ask PLC to fill in the missing frame (in two 10ms sections).  
                // PLC operates on 10ms blocks so there are two calls
                _plc.badFrame(pcm2, BLOCK_SIZE_8K / 2);
                _plc.badFrame(pcm2 + BLOCK_SIZE_8K / 2, BLOCK_SIZE_8K / 2);
            }
        }
        else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K) {
            if (frame.getType() == Message::Type::AUDIO) {
                int16_t pcm1[BLOCK_SIZE_16K];
                // Transcode
                _transcoder0c.decode(frame.body(), frame.size(), 
                    pcm1, codecBlockSize(_codecType));            
                // Pass audio through the PLC mechanism
                // PLC operates on 10ms blocks so there are two calls
                _plc.goodFrame(pcm1, pcm2, BLOCK_SIZE_16K / 2);
                _plc.goodFrame(pcm1 + BLOCK_SIZE_16K / 2, pcm2 + BLOCK_SIZE_16K / 2, 
                    BLOCK_SIZE_16K / 2);
            } else {
                // Ask PLC to fill in the missing frame (in two 10ms sections).  
                // PLC operates on 10ms blocks so there are two calls
                _plc.badFrame(pcm2, BLOCK_SIZE_16K / 2);
                _plc.badFrame(pcm2 + BLOCK_SIZE_16K / 2, BLOCK_SIZE_16K / 2);
            }
        }
        else if (_codecType == CODECType::IAX2_CODEC_SLIN_48K) {
            if (frame.getType() == Message::Type::AUDIO) {
                // Transcode
                _transcoder0d.decode(frame.body(), frame.size(), 
                    pcm2, codecBlockSize(_codecType));            
            } else {
                // There is no PLC at the moment, so we just
                // create a frame of silence
                for (unsigned i = 0; i < BLOCK_SIZE_48K; i++)
                    pcm2[i] = 0;
            }
        }

        // Resample PCM data up to 48K
        _resampler.resample(pcm2, codecBlockSize(_codecType), 
            pcm48k, BLOCK_SIZE_48K);

        // Look for power and decide if this is silence 
        float rms = 0;
        for (unsigned i = 0; i < BLOCK_SIZE_48K; i++) {
            float a = pcm48k[i] / 32767.0;
            rms += (a * a);
        }
        // A completely silent frame is ignored. This can happen for stations
        // that sent continuous silent frames, like the ASL Telephone Portal.
        if (rms == 0)
            return;
       
        // Transcode to SLIN_48K
        uint8_t slin48k[BLOCK_SIZE_48K * 2];
        _transcoder1.encode(pcm48k, BLOCK_SIZE_48K, slin48k, BLOCK_SIZE_48K * 2);
        
        Message outFrame(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_48K,
            BLOCK_SIZE_48K * 2, slin48k, frame.getOrigMs(), frame.getRxMs());
        outFrame.setSource(frame.getSourceBusId(), frame.getSourceCallId());
        outFrame.setDest(frame.getDestBusId(), frame.getDestCallId());

        _kerchunkFilter.consume(outFrame);
        //_sink(outFrame);
    }
    else {
        assert(false);
    }
}

}

}
