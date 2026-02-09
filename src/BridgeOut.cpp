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

#include "kc1fsz-tools/Log.h"

#include "Message.h"
#include "BridgeOut.h"

// The definition of "recent"
#define RECENT_TIMEOUT_MS (2000)

using namespace std;

namespace kc1fsz {

void BridgeOut::setCodec(CODECType codecType) {
    _codecType = codecType;
    unsigned rate = codecSampleRate(_codecType);
    if (rate == 0)
        assert(false);
    _resampler.setRates(48000, rate);
}

bool BridgeOut::isActiveRecently() const {
    return _clock->isInWindow(_lastActivityMs, RECENT_TIMEOUT_MS);
}

void BridgeOut::consume(const Message& frame) {

    if (frame.getType() == Message::Type::AUDIO) {

        _lastActivityMs = _clock->timeMs();

        // This is the existing path
        if (frame.getFormat() == CODECType::IAX2_CODEC_SLIN_48K) {
            
            assert(frame.size() == BLOCK_SIZE_48K * 2);
    
            if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
                _codecType == CODECType::IAX2_CODEC_SLIN_16K ||
                _codecType == CODECType::IAX2_CODEC_SLIN_8K) {             
                
                Transcoder* t1 = 0;
                unsigned codeSize = maxVoiceFrameSize(_codecType);
                unsigned blockSize = codecBlockSize(_codecType);
                if (_codecType == CODECType::IAX2_CODEC_G711_ULAW) 
                    t1 = &_transcoder1a;
                else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K)
                    t1 = &_transcoder1c;
                else if (_codecType == CODECType::IAX2_CODEC_SLIN_8K)
                    t1 = &_transcoder1d;
                else 
                    assert(false);

                // Make PCM data
                int16_t pcm48k[BLOCK_SIZE_48K];
                _transcoder0.decode(frame.body(), frame.size(), pcm48k, BLOCK_SIZE_48K);

                // Resample PCM data 
                // NOTE: MAKE THIS LARGE ENOUGH
                int16_t pcm_low[BLOCK_SIZE_8K * 2];
                _resampler.resample(pcm48k, BLOCK_SIZE_48K, pcm_low, blockSize);

                // NOTE: Make this big enough for any format!
                uint8_t code[BLOCK_SIZE_8K * 4];
                t1->encode(pcm_low, blockSize, code, codeSize);
                
                // Times are passed right through
                MessageWrapper outFrame(Message::Type::AUDIO, _codecType,
                    codeSize, code, frame.getOrigMs(), frame.getRxMs());
                outFrame.setSource(frame.getSourceBusId(), frame.getSourceCallId());
                outFrame.setDest(frame.getDestBusId(), frame.getDestCallId());

                _sink(outFrame);
            }
            else if (_codecType == CODECType::IAX2_CODEC_SLIN_48K) {
                // No support for interpolation
                if (frame.getType() == Message::Type::AUDIO) {
                    // No conversion needed
                    _sink(frame);
                }
                else
                    assert(false);
            }
            else {
                assert(false);
            }
        }
        // NEW OPTIMIZED PATH
        else if (frame.getFormat() == CODECType::IAX2_CODEC_PCM_48K) {

            assert(frame.size() == BLOCK_SIZE_48K * 2);
    
            if (_codecType == CODECType::IAX2_CODEC_G711_ULAW ||
                _codecType == CODECType::IAX2_CODEC_SLIN_16K ||
                _codecType == CODECType::IAX2_CODEC_SLIN_8K) {             
                
                Transcoder* t1 = 0;
                unsigned codeSize = maxVoiceFrameSize(_codecType);
                unsigned blockSize = codecBlockSize(_codecType);
                if (_codecType == CODECType::IAX2_CODEC_G711_ULAW) 
                    t1 = &_transcoder1a;
                else if (_codecType == CODECType::IAX2_CODEC_SLIN_16K)
                    t1 = &_transcoder1c;
                else if (_codecType == CODECType::IAX2_CODEC_SLIN_8K)
                    t1 = &_transcoder1d;
                else 
                    assert(false);

                // Resample PCM data 
                // NOTE: MAKE THIS LARGE ENOUGH
                int16_t pcm_low[BLOCK_SIZE_8K * 2];
                _resampler.resample((const int16_t*)frame.body(), BLOCK_SIZE_48K, 
                    pcm_low, blockSize);

                // NOTE: Make this big enough for any format!
                uint8_t code[BLOCK_SIZE_8K * 4];
                t1->encode(pcm_low, blockSize, code, codeSize);
                
                // Times are passed right through
                MessageWrapper outFrame(Message::Type::AUDIO, _codecType,
                    codeSize, code, frame.getOrigMs(), frame.getRxMs());
                outFrame.setSource(frame.getSourceBusId(), frame.getSourceCallId());
                outFrame.setDest(frame.getDestBusId(), frame.getDestCallId());

                _sink(outFrame);
            }
            else if (_codecType == CODECType::IAX2_CODEC_SLIN_48K) {
                // No support for interpolation
                if (frame.getType() == Message::Type::AUDIO) {
                    // No conversion needed
                    _sink(frame);
                }
                else
                    assert(false);
            }
            else {
                assert(false);
            }
        }
        else {
            assert(false);
        }
    }
    else {
        // Non-audio frames passed through
        _sink(frame);
    }
}

}
