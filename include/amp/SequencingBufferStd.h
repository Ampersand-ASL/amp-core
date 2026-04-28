/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ, All Rights Reserved
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

#include <cmath>
#include <concepts>

#include "kc1fsz-tools/fixedsortedlist.h"
#include "kc1fsz-tools/Log.h"

#include "amp/SequencingBuffer.h"

namespace kc1fsz {   
    namespace amp {

/**
 * Adaptive Jitter Buffer.
 * 
 * The method I've settled on at the moment is called "Ramjee Algorithm 1" after a 
 * paper by Ramjee, Kurose, Towsley, and Schulzrinne called "Adaptive Playout 
 * Mechanisms for Packetized Audio Applications in Wide-Area Networks." This
 * is an IEEE paper behind the paywell.
 */
template <class T> class SequencingBufferStd : public SequencingBuffer<T> {
public:

    SequencingBufferStd()
    :   _buffer(_slotSpace, _ptrSpace, MAX_BUFFER_SIZE, 
        // This is the function that establishes the sort order of the frames. 
        [](const T& a, const T& b) {
            if (a.getOrigMs() < b.getOrigMs())
                return -1;
            else if (a.getOrigMs() > b.getOrigMs())
                return 1;
            else 
                return 0;
        }) 
    { 
        reset();
    }

    void setTraceLog(Log* l) { _traceLog = l; }

    void lockDelay() {
        _delayLocked = true;
    }

    void unlockDelay() {
        _delayLocked = false;
    }   

    void setInitialMargin(int32_t ms) {
        _initialMargin = ms;
        // Seed the adaptive buffer
        _di = _di_1 = ms;
        _vi = _vi_1 = 0;
    }

    void setTalkspurtTimeoutInterval(uint32_t ms) {
        _talkspurtTimeoutInteval = ms;
    }

    // #### TODO: MAKE SURE WE HAVE EVERYTHING
    virtual void reset() {
        _buffer.clear();
        // Diagnostics
        _maxBufferDepth = 0;
        _overflowCount = 0;
        _lateVoiceFrameCount = 0;
        _interpolatedVoiceFrameCount = 0;
        // Tracking
        _lastPlayedLocal = 0;
        _lastPlayedOrigMs = 0;
        _originCursor = 0;
        _originCursorValid = false;
        _inTalkspurt = false;
        _talkSpurtCount = 0;
        _talkspurtFrameCount = 0;
        _talkspurtFirstOrigin = 0;
        _voicePlayoutCount = 0;
        _voiceConsumedCount = 0;
        _di = 0;
        _di_1 = 0;
        _vi = 0;
        _vi_1 = 0;
        _idealDelay = 0;
        _worstMargin = INT32_MAX;
        _totalMargin = 0;   
        _lastPlayoutTime = 0;
        _startMs = 0;
        _newestOrigMs = 0;
     }

    bool empty() const { return _buffer.empty(); }
    unsigned size() const { return _buffer.size(); }
    unsigned maxSize() const { return MAX_BUFFER_SIZE; }
    void setStartMs(uint32_t ms) { _startMs = ms; }
    
    void debug() const {
        if (!empty()) {
            printf("----- SequencingBufferStd --------------------\n");
            printf("lastplayout %d, cursor %d, idealdelay %f\n", _lastPlayoutTime, _originCursor, _idealDelay);
            _buffer.visitAll([this](const T& frame) {
                printf("  orig=%6d rx=%6d margin=%ld\n", frame.getOrigMs(), frame.getRxMs(),
                (long)((int64_t)_lastPlayoutTime - (int64_t)frame.getRxMs()));
                return true;
            });
        }
    }

    /**
     * Used to "extend" a 16-bit time (from a voice mini-frame) to a full
     * 32-bit representation if necessary. Assumes that both times are 
     * within the same general vicinity.
     */
    static uint32_t extendTime(uint32_t remoteTime, uint32_t localTime) {
        if ((remoteTime & 0xffff0000) == 0) {

            uint32_t r2 = remoteTime & 0x0000ffff;
            uint32_t l1 = localTime  & 0xffff0000;
            uint32_t l2 = localTime  & 0x0000ffff;

            if (l2 >= 0x8000) {
                uint32_t boundary = (l2 - 0x8000) & 0xffff;
                if (r2 < boundary) {
                    return (l1 + 0x00010000) | r2;
                } else {
                    return l1 | r2;
                }
            } else {
                uint32_t boundary = (l2 + 0x8000) & 0xffff;
                if (r2 > boundary) {
                    return (l1 - 0x00010000) | r2;
                } else {
                    return l1 | r2;
                }
            }
        }
        else {
            return remoteTime;
        }
    }

    static int32_t roundToTick(int32_t v, int32_t tick) {
        float a = round((float)v / (float)tick);
        return a * (float)tick;
    }

    static uint32_t roundDownToTick(uint32_t v, uint32_t tick) {
        return (v / tick) * tick;
    }
    
    // ----- Diagnostics -----------------------------------------------

    unsigned getLateVoiceFrameCount() const { return _lateVoiceFrameCount; }
    unsigned getInterpolatedVoiceFrameCount() const { return _interpolatedVoiceFrameCount; }
    unsigned getOverflowCount() const { return _overflowCount; }
    unsigned getMaxBufferDepth() const { return _maxBufferDepth; }

    // ----- SequencingBuffer -------------------------------------------------

    virtual bool consume(Log& log, const T& payload) {

        if (!_buffer.hasCapacity()) {
            _overflowCount++;
            log.info("OF orig=%6d cursor=%6d", payload.getOrigMs(), _originCursor);
            if (_overflowCount % 25 == 0) {
                debug();
            }
            return false;
        }

        _buffer.insert(payload);     

        if (_traceLog)
            _traceLog->info("RXV, %u, %u", payload.getOrigMs(), _originCursor);

        if (_newestOrigMs < payload.getOrigMs())
            _newestOrigMs = payload.getOrigMs();

        // Use the frame information to keep the delay estimate up to date. We do 
        // this as early as possible (on consume) so that the estimate is as 
        // up-to-date as possible.
        bool startOfCall = _voiceConsumedCount == 0;
        _voiceConsumedCount++;
        _updateDelayTarget(log, startOfCall, payload.getRxMs(), payload.getOrigMs());

        return true;
    }

    /**
     * @param localMs The local time.
     */
    virtual void playOut(Log& log, uint32_t localMs, SequencingBuffer<T>::playCb playSink, 
        SequencingBuffer<T>::interpCb interpSink) {     

        _lastPlayoutTime = localMs;

        bool framePlayed = false;
        uint32_t framePlayedOriginMs = 0;
        uint32_t framePlayedRxMs = 0;

        // For diagnostic purposes
        _maxBufferDepth = std::max(_maxBufferDepth, _buffer.size());

        // General Notes
        // -------------
        // We can only play one frame per playout cycle. There are a few possibilities
        // of what can happen.
        //
        // 1. The buffer is empty, or all of the entries on the buffer are expired. In
        //    this case nothing is played in the cycle.
        // 2. The buffer has a frame that is playable right now. In this case we play the frame
        //    in the cycle.
        // 3. The buffer has a frame that isn't playable yet. In this case we interpolate 
        //    (if a talkspurt is going) and leave the frame for a future cycle.
        //
        // Implicit in these cases are the concepts of "expired" and "playable." These
        // determinations are made using a cursor that always points to the next audio
        // frame to be played. The initial position of this cursor is established at the 
        // start of a talkspurt.
        //
        // Any frames that are earlier than the cursor are considered to be expired and
        // get discarded. 
        //

        // Look for the special case that is treated as a bypass of the 
        // sequencing buffer.
        if (_initialMargin == 0) {
            if (!_buffer.empty()) {
                const T& frame = _buffer.first();
                // Just play the oldest frame, no questions asked
                playSink(frame, localMs);
                framePlayed = true;
                framePlayedOriginMs = frame.getOrigMs();
                framePlayedRxMs = frame.getRxMs();
                _voicePlayoutCount++;
                _buffer.pop();
            }
        }
        // Otherwise, the playout is controlled by a cursor
        else {
            // Has the cursor been setup yet? If not, establish based on the timestamp
            // of the first frame on the buffer.
            if (!_originCursorValid) {
                if (!_buffer.empty()) {

                    const T& frame = _buffer.first();

                    // The playout cursor gets established based on the origin timestamp
                    // of the first frame in the stream.
                    _originCursor = roundToTick(SUB_MOD32(frame.getOrigMs(), _initialMargin),
                        _voiceTickSize);
                    _originCursorValid = true;
                }
            }

            if (_originCursorValid) {

                // #### TODO: CONSIDER WHETHER A CURSOR MOVE IS NEEDED?
                // Clean anything that is expired
                while (!_buffer.empty() && 
                    LT_MOD32(_buffer.first().getOrigMs(), _originCursor)) {
                        log.info("Discarding %u < %u", _buffer.first().getOrigMs(), _originCursor);
                        _buffer.pop();
                }

                // If we've got a frame that is inside of the current tick then play it.
                // NOTE: *Most* of the time the voice ticks will be aligned on audio 
                // tick boundaries, but occasionally we may get one that is is not, most
                // likely because the timestamp for the tick was consumed by another 
                // message.
                //
                // (Bruce observed non-rounded voice frames from ECR on 27-Dec-2025)
                const T& frame = _buffer.first();
                if (LE_MOD32(_originCursor, frame.getOrigMs()) &&
                    LT_MOD32(frame.getOrigMs(), _originCursor + _voiceTickSize)) {
                    playSink(frame, localMs);
                    framePlayed = true;
                    framePlayedOriginMs = frame.getOrigMs();
                    framePlayedRxMs = frame.getRxMs();
                    _voicePlayoutCount++;
                    _buffer.pop();
                }
            }
        }

        // All playout complete, now deal with tracking/stats
        if (framePlayed) {
            
            _lastPlayedLocal = localMs;

            // Is this the leading edge of a talkspurt?
            if (!_inTalkspurt) {
                _inTalkspurt = true;
                _talkspurtFrameCount = 0;
                _talkspurtFirstOrigin = framePlayedOriginMs;
                _worstMargin = INT32_MAX;
                _totalMargin = 0;
                log.info("Start TS %u", _talkspurtFirstOrigin);
            }

            // Keep margin tracking up to date
            int32_t margin = (int32_t)localMs - (int32_t)framePlayedRxMs;
            if (margin < _worstMargin)
                _worstMargin = margin;
            _totalMargin += margin;
            _talkspurtFrameCount++;
        }
        else {
            // Is there a gap in the talkspurt? If so, interpolate it.
            if (_inTalkspurt) {

                interpSink(_originCursor, localMs, _voiceTickSize);
                _interpolatedVoiceFrameCount++;
                
                // Is this the trailing edge of a talkspurt?
                if (localMs >= (_lastPlayedLocal + _talkspurtTimeoutInteval)) {
                    _inTalkspurt = false;
                    _talkSpurtCount++;
                    int32_t avgMargin = (_talkspurtFrameCount != 0) ? 
                        _totalMargin / _talkspurtFrameCount : 0;
                    log.info("End TS, avgM: %d, shortM: %d, OC: %u, size: %d, interp: %u", 
                        avgMargin, 
                        _worstMargin,
                        _originCursor, 
                        size(),
                        _interpolatedVoiceFrameCount); 
                    // Unlink the cursor so we can re-establish 
                    _originCursorValid = false;
                }
            }
        }

        // Always move the expectation forward one click to keep in sync with 
        // the clock moving forward on the remote side.
        _originCursor += _voiceTickSize;
    }
    
    virtual bool inTalkspurt() const {
        return _inTalkspurt;
    }

private:

    // This should be called on each voice frame arrival so that we have the 
    // most timely information about the network conditions.
    void _updateDelayTarget(Log& log, bool startOfCall, 
        uint32_t frameRxMs, uint32_t frameOrigMs) {

        // Calculate the flight time of this frame
        float ni = ((float)frameRxMs - (float)frameOrigMs);

        // If this is the very first voice received for the first talkspurt
        // then use it to make an initial estimate of the delay. This can float
        // during the rest of the talkspurt.
        if (startOfCall) {
            _di = ni;
            _di_1 = ni;
            // Assume no variance at the beginning
            _vi = 0;
            _vi_1 = _vi;
        }
        // Re-estimate the variance statistics on each frame
        else {
            //
            // Please see "Adaptive Playout Mechanisms for Packetized Audio Applications
            // in Wide-Area Networks" by Ramachandran Ramjee, et. al.
            //
            // This is the classic "Algorithm 1" method
            _di = _alpha * _di_1 + (1 - _alpha) * ni;
            _di_1 = _di;
            _vi = _alpha * _vi_1 + (1 - _alpha) * fabs(_di - ni);
            _vi_1 = _vi;
        }

        // This is the current estimate of the ideal delay
        _idealDelay = _di + _beta * _vi;
    }

    // ------ Configuration Constants ----------------------------------------

    // The size of an audio tick in milliseconds
    const uint32_t _voiceTickSize = 20;
    // This is the most the playback cursor can be adjusted to pick up a 
    // late frame inside of a talkspurt
    const int32_t _midTsAdjustMax = 500;
    // Constants for Ramjee Algorithm 1
    const float _alpha = 0.998002f;
    const float _beta = 5.0f;
    // The number of ms of silence before we delcare a talkspurt ended.
    uint32_t _talkspurtTimeoutInteval = 60;   

    Log* _traceLog = 0;

    // A 64-entry buffer provides room to track 1 second of audio
    // plus some extra for control frames that may be interspersed.
    const static unsigned MAX_BUFFER_SIZE = 64;
    T _slotSpace[MAX_BUFFER_SIZE];
    unsigned _ptrSpace[MAX_BUFFER_SIZE];
    fixedsortedlist<T> _buffer;

    uint32_t _startMs = 0;

    // This is the important variable. This always points to the next
    // origin time to be played. This will always be on a 20ms boundary 
    // (i.e. 0, 20, 40, 60, 80, 100, ....)
    int32_t _originCursor = 0;
    bool _originCursorValid = false;
    uint32_t _talkspurtFirstOrigin = 0;
    // The orig timestamp of the newest frame to be put into the buffer,
    // used to prevent an overflow.
    uint32_t _newestOrigMs = 0;
    uint32_t _lastPlayedOrigMs = 0;
    // Used for detecting the end of a talkspurt
    uint32_t _lastPlayedLocal = 0;

    bool _inTalkspurt = false;
    unsigned _talkspurtFrameCount = 0;

    bool _delayLocked = false;

    // Used to estimate delay and delay variance
    float _di_1 = 0;
    float _di = 0;
    float _vi = 0;
    float _vi_1 = 0; 
    float _idealDelay = 0;
    // Starting estimate of margin
    // MUST BE A MULTIPLE OF _voiceTickSize
    unsigned _initialMargin = _voiceTickSize * 5;

    // ----- Diagnostic/Metrics Stuff ----------------------------------------

    unsigned _overflowCount = 0;
    unsigned _lateVoiceFrameCount = 0;
    unsigned _interpolatedVoiceFrameCount = 0;
    unsigned _voicePlayoutCount = 0;
    unsigned _voiceConsumedCount = 0;
    int32_t _worstMargin = 0;
    int32_t _totalMargin = 0;
    uint32_t _lastPlayoutTime = 0;

    // The number of talkspurts since reset. This is incremented at the end of 
    // each talkspurt. Importantly, it will be zero for the duration of the 
    // first talkspurt.
    unsigned _talkSpurtCount = 0;
    unsigned _maxBufferDepth = 0;
};    

    }
}
