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
    * @param netLineId The line ID of the IAX network connection.
     */
    Bridge(Log& log, Log& traceLog, Clock& clock, MessageConsumer& bus, 
        BridgeCall::Mode defaultMode, 
        unsigned lineId, unsigned ttsLineId, unsigned netTestLineId,
        const char* netTestBindAddr,
        unsigned networkDestLineId);

    unsigned getCallCount() const;

    void reset();

    void setLocalNodeNumber(const char* nodeNumber);

    /**
     * Sets optional text greeting that will be spoken to any new caller.
     */
    void setGreeting(const char* greeting);

    void setParrotLevelThresholds(std::vector<int>& thresholds);

    std::vector<std::string> getConnectedNodes() const;

    /**
     * @returns True if the internal status document (JSON) has been 
     * updated and a newer one should be distributed.
     */
    bool isStatusDocUpdated(uint64_t lastUpdateMs) const;

    /**
     * @returns The latest live status document in JSON format.
     */
    json getStatusDoc() const;

    // ----- MessageConsumer --------------------------------------------------

    void consume(const Message& frame);

    // ----- Runnable2 --------------------------------------------------------
    
    bool run2();
    void audioRateTick(uint32_t tickMs);
    void oneSecTick();
    void tenSecTick();

private:

    static constexpr auto RESET_VISITOR = [](BridgeCall& s) { s.reset(); return true; };

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

    static const unsigned MAX_CALLS = 8;
    BridgeCall _callSpace[MAX_CALLS];
    fixedvector<BridgeCall> _calls;

    std::vector<int> _parrotLevelThresholds;
};

/**
 * A utility that polls for changes to the status document and fires a lambda
 * callback whenever something new is available. Would probably be used to 
 * keep UIs up to date efficiently.
 */
class BridgeStatusDocPoller : public Runnable2 {
public:

    BridgeStatusDocPoller(Clock& clock, const Bridge& bridge, 
        std::function<void(const json& doc)> cb) 
    :   _clock(clock), _bridge(bridge), _cb(cb) { }

    // ----- Runnable2 ----------------------------------------------------

    void oneSecTick() { 
        uint64_t nowMs = _clock.timeUs() / 1000;
        if (_bridge.isStatusDocUpdated(nowMs)) {
            _cb(_bridge.getStatusDoc());
            _lastUpdateMs = nowMs;
        }
    }

    bool run2() { return false; }

private:

    Clock& _clock;
    const Bridge& _bridge;
    std::function<void(const json& doc)> _cb;
    uint64_t _lastUpdateMs = 0;
};

    }
}
