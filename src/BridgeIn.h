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
#include <queue>

#include "itu-g711-plc/Plc.h"

#include "amp/Resampler.h"
#include "amp/SequencingBufferStd.h"

#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Transcoder_G711_ULAW.h"
#include "Transcoder_SLIN_8K.h"
#include "Transcoder_SLIN_16K.h"
#include "Transcoder_SLIN_48K.h"
#include "KerchunkFilter.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

/**
 * A chain of functionality that needs to be handled on an input to a conference:
 * 
 * 1. De-jitter, identification of interpolation needs.
 * 2. PLC (if possible)
 * 3. Transcodes/resamples to SLIN_48K for internal processing.
 * 4. Kerchunk filtering
 */
class BridgeIn : public MessageConsumer {
public:

    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_16K = 160 * 2;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    BridgeIn();

    void init(Log* log, Log* traceLog, Clock* clock) { 
        _log = log; 
        _traceLog = traceLog;
        _clock = clock; 
        _jitBuf.setTraceLog(traceLog);
        _kerchunkFilter.init(log, clock);
    }

    void setSink(std::function<void(const Message& msg)> sink) { _sink = sink; }

    void setJitterBufferInitialMargin(unsigned ms);

    void setCodec(CODECType codecType);

    CODECType getCodec() const { return _codecType; }

    void setStartTime(uint32_t ms) { 
        _jitBuf.setStartMs(ms);
    }

    uint32_t getLastUnkeyMs() const { return _lastUnkeyMs; }
    
    void reset() { 
        _codecType = CODECType::IAX2_CODEC_UNKNOWN;
        _jitBuf.reset();
        _lastUnkeyMs = 0;
        _lastAudioMs = 0;
        _activeStatus = false;
        _lastActiveStatusChangedMs = 0;
        _transcoder0a.reset(); 
        _transcoder0c.reset(); 
        _transcoder0d.reset(); 
        _transcoder1.reset(); 
        _resampler.reset(); 
        _kerchunkFilter.reset();
    }

    // Statistics

    unsigned getClipCount() const;

    /**
     * @returns Peak power reading seen across trailing second in dBFS
     */
    float getPeakPower() const;

    /**
     * @returns Average power across trailing second in dBFS
     */
    float getAvgPower() const;

    /**
     * @returns The last time any audio was processed
     */
    //uint64_t getLastAudioMs() const { return _lastAudioMs; }

    /**
     * @returns True if audio is actively being received.
     */
    bool isActive() const { return _activeStatus; }

    /**
     * @returns True if audio has been received within the last few
     * seconds.
     */
    bool isActiveRecently() const;

    /** 
     * @returns The last time the active status transitioned (in either
     * direction).
     */
    uint64_t getActiveStatusChangedMs() const { return _lastActiveStatusChangedMs; }

    void setKerchunkFilterEnabled(bool b) {
        _kerchunkFilter.setEnabled(b);
    }

    void setKerchunkFilterEvaluationIntervalMs(unsigned ms) { 
        _kerchunkFilter.setEvaluationIntervalMs(ms); 
    }

    // ----- Runnable2 --------------------------------------------------------

    void audioRateTick(uint32_t tickMs);

    // ----- MessageConsumer ---------------------------------------------------

    /**
     * This is used when a message comes into the call, most likely
     * from one of the lines.
     */
    void consume(const Message& frame);

private:

    void _handleJitBufOut(const Message& msg);

    Log* _log; 
    Log* _traceLog; 
    Clock* _clock;
    std::function<void(const Message& msg)> _sink = nullptr;
    // This is the input CODEC of the user
    CODECType _codecType = CODECType::IAX2_CODEC_UNKNOWN;

    // Last time audio was processed 
    uint64_t _lastAudioMs = 0; 

    // Current status, used to detect transitions  
    bool _activeStatus = false;
    // The last time the status was changed in either direction.
    uint64_t _lastActiveStatusChangedMs = 0;

    // This is the Jitter Buffer used to address timing/sequencing
    // issues on the input side of the Bridge.
    amp::SequencingBufferStd<MessageCarrier> _jitBuf;

    uint32_t _lastUnkeyMs = 0;

    Transcoder_G711_ULAW _transcoder0a;
    Transcoder_SLIN_8K _transcoder0b;
    Transcoder_SLIN_16K _transcoder0c;
    Transcoder_SLIN_48K _transcoder0d;

    // This is used to satisfy interpolation requests from
    // the Jitter Buffer.
    Plc _plc;

    // This is used to convert up to 48K
    amp::Resampler _resampler;    
    
    // This is used at the end to convert to the "bus format"
    // that is passed around internally.
    Transcoder_SLIN_48K _transcoder1;

    KerchunkFilter _kerchunkFilter;
};

    }
}
