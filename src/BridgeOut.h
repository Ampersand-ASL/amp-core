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

#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Transcoder_G711_ULAW.h"
#include "Transcoder_SLIN_48K.h"
#include "Transcoder_SLIN_16K.h"
#include "Transcoder_SLIN_8K.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * Transcodes from internal SLIN_48K to whatever CODEC is needed for 
 * external communications.
 */
class BridgeOut : public MessageConsumer {
public:

    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    void setSink(std::function<void(const Message& msg)> sink) { _sink = sink; }

    void setCodec(CODECType codecType);

    void init(Log* log, Clock* clock) { 
        _log = log; 
        _clock = clock; 
    }

    void reset() { 
        _codecType = CODECType::IAX2_CODEC_UNKNOWN;
        _transcoder0.reset(); 
        _transcoder1a.reset(); 
        _transcoder1c.reset(); 
        _resampler.reset(); 
        _lastActivityMs = 0;
    }

    virtual void consume(const Message& frame);

    bool isActiveRecently() const;

private:

    Log* _log; 
    Clock* _clock;

    CODECType _codecType = CODECType::IAX2_CODEC_UNKNOWN;
    std::function<void(const Message& msg)> _sink = nullptr;
    Transcoder_SLIN_48K _transcoder0;
    Transcoder_G711_ULAW _transcoder1a;
    Transcoder_SLIN_16K _transcoder1c;
    Transcoder_SLIN_8K _transcoder1d;
    amp::Resampler _resampler;
    uint64_t _lastActivityMs;
};

}
