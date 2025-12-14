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
#pragma once

#include <functional>

#include "amp/Resampler.h"
#include "amp/SequencingBufferStd.h"

#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Transcoder_G711_ULAW.h"
#include "Transcoder_SLIN_48K.h"
#include "Transcoder_SLIN.h"
#include "Transcoder_SLIN_16K.h"

namespace kc1fsz {

/**
 * A chain of functionality that needs to be handled on an input to a conference:
 * 
 * 1. De-jitter, identification of interpolation needs.
 * 2. PLC (if possible)
 * 3. Transcodes/resamples to SLIN_48K for internal processing.
 */
class AdaptorIn : public MessageConsumer {
public:

    //static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    void setCodec(CODECType codecType);
    void setStartTime(uint32_t t) { _startTime = t; }
    void setSink(std::function<void(const Message& msg)> sink) { _sink = sink; }
    
    void reset() { 
        _codecType = CODECType::IAX2_CODEC_UNKNOWN;
        _startTime = 0;
        _sink = nullptr;
        _transcoder0a.reset(); 
        _transcoder0b.reset(); 
        _transcoder0c.reset(); 
        _transcoder1.reset(); 
        _resampler.reset(); 
    }

    virtual void audioRateTick() { }

    virtual void consume(const Message& frame);

private:

    std::function<void(const Message& msg)> _sink = nullptr;

    amp::SequencingBufferStd<Message> jitBuf;

    // This is the input CODEC of the user
    CODECType _codecType = CODECType::IAX2_CODEC_UNKNOWN;
    // In ms
    uint32_t _startTime = 0;
    Transcoder_G711_ULAW _transcoder0a;
    Transcoder_SLIN _transcoder0b;
    Transcoder_SLIN_16K _transcoder0c;
    Transcoder_SLIN_48K _transcoder1;
    amp::Resampler _resampler;

};

}
