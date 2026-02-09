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

#include <string>
#include <vector>

// 3rd party
#include <nlohmann/json.hpp>

// KC1FSZ tools
#include "kc1fsz-tools/fixedvector.h"
#include "kc1fsz-tools/threadsafequeue.h"

#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "BridgeCall.h"

using json = nlohmann::json;

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge : public MessageConsumer, public Runnable2 {
public:

    friend class BridgeCall;

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_16K = 160 * 2;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    /**
    * Takes text and adds a space between each letter. Relevant to 
    * text-to-speech.
    */
    static std::string addSpaces(const char* text);

    /**
     * @param netDestLineId The line ID of the IAX network connection.
     * @param callSpace The pre-allocated bank of BrideCalls. This is 
     * passed as a parameter to allow flexibility around the max number
     * of calls.
     * @param callSpaceLen The number of calls in the bank.
     */
    Bridge(Log& log, Log& traceLog, Clock& clock, MessageConsumer& bus, 
        BridgeCall::Mode defaultMode, 
        unsigned lineId, unsigned ttsLineId, unsigned netTestLineId,
        const char* netTestBindAddr, unsigned netDestLineId,
        BridgeCall* callSpace, unsigned callSpaceLen);

    void reset();

    void setLocalNodeNumber(const char* nodeNumber);

    void setKerchunkFilterNodes(std::vector<std::string> nodes);

    void setKerchunkFilterDelayMs(unsigned ms);

    /**
     * Sets optional text greeting that will be spoken to any new caller.
     */
    void setGreeting(const char* greeting);

    void setParrotLevelThresholds(std::vector<int>& thresholds);

    unsigned getCallCount() const;

    std::vector<std::string> getConnectedNodes() const;

    /**
     * @returns Effective time of the status document for this call. Used
     * to track changes.
     */
    uint64_t getStatusDocStampMs() const;

    /**
     * @returns The latest live status document in JSON format.
     */
    json getStatusDoc() const;

    /**
     * @returns The latest audio levels in JSON format. Generally used for 
     * UI display.
     */
    json getLevelsDoc() const;

    // ----- MessageConsumer --------------------------------------------------

    void consume(const Message& frame);

    // ----- Runnable2 --------------------------------------------------------
    
    void audioRateTick(uint32_t tickMs);
    void oneSecTick();
    void tenSecTick();

private:

    static constexpr auto RESET_VISITOR = [](BridgeCall& s) { s.reset(); return true; };
    static constexpr auto ACTIVE_PRED = [](const BridgeCall& c) { return c.isActive(); };

    void _visitActiveCalls(std::function<bool(BridgeCall&)> cb);
    void _visitActiveCalls(std::function<bool(const BridgeCall&)> cb) const;

    Log& _log;
    Log& _traceLog;
    Clock& _clock;
    MessageConsumer& _bus;
    const BridgeCall::Mode _defaultMode;
    unsigned _lineId;
    unsigned _ttsLineId;
    unsigned _netTestLineId;
    unsigned _networkDestLineId;
    std::string _nodeNumber;
    std::string _greetingText;
    std::vector<std::string> _kerchunkFilterNodes;
    unsigned _kerchunkFilterDelayMs;

    fixedvector<BridgeCall> _calls;

    // The last time a change was made to the call list (i.e. started or 
    // ended a call);
    uint64_t _lastCallListChangeMs;

    std::vector<int> _parrotLevelThresholds;

    std::string _statusMessageText;
    uint64_t _statusMessageUpdateMs = 0;
    unsigned _statusMessageLevel = 0;

    uint64_t _maxTickUs = 0;
};

// #### TODO: CAN WE CONSOLIDATE THE CONFIG POLLER WITH THIS?

/**
 * A utility that polls for changes to the status document and fires a lambda
 * callback whenever something new is available. Would probably be used to 
 * keep UIs up to date efficiently.
 */
class BridgeStatusDocPoller : public Runnable2 {
public:

    /**
     * @param maxIntervalMs The maximum interval that is allowed before a
     * event will be fired.
     */
    BridgeStatusDocPoller(Log& log, Clock& clock, const Bridge& bridge, 
        unsigned maxIntervalMs,
        std::function<void(const json& doc)> cb) 
    :   _log(log), _clock(clock), _bridge(bridge), _maxIntervalMs(maxIntervalMs), 
        _cb(cb) { }

    // ----- Runnable2 ----------------------------------------------------

    void quarterSecTick() {     
        uint64_t stampMs = _bridge.getStatusDocStampMs();
        if (stampMs > _lastUpdateMs) {
            _lastUpdateMs = stampMs;
            _cb(_bridge.getStatusDoc());
        }
    }

    void tenSecTick() {     
        uint64_t stampMs = _bridge.getStatusDocStampMs();
        _lastUpdateMs = stampMs;
        _cb(_bridge.getStatusDoc());
    }

private:

    Log& _log;
    Clock& _clock;
    const Bridge& _bridge;
    const unsigned _maxIntervalMs;
    const std::function<void(const json& doc)> _cb;

    uint64_t _lastUpdateMs = 0;
};

    }
}
