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

#include "kc1fsz-tools/fixedvector.h"
#include "kc1fsz-tools/threadsafequeue.h"

#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "BridgeCall.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge : public MessageConsumer, public Runnable2 {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_16K = 160 * 2;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    Bridge(Log& log, Log& traceLog, Clock& clock, MessageConsumer& bus, 
        BridgeCall::Mode defaultMode, unsigned ttsLineId, unsigned netTestLineId);

    unsigned getCallCount() const;

    void reset();

    /**
     * Only call this if the text-to-speech server is needed. This may not be 
     * avaiable on all platforms.
     */
    void startTTSThread();

    // ----- MessageConsumer --------------------------------------------------

    void consume(const Message& frame);

    // ----- Runnable2 --------------------------------------------------------
    
    bool run2();
    void audioRateTick(uint32_t tickMs);

private:

    static constexpr auto RESET_VISITOR = [](BridgeCall& s) { s.reset(); return true; };

    Log& _log;
    Log& _traceLog;
    Clock& _clock;
    MessageConsumer& _bus;
    const BridgeCall::Mode _defaultMode;
    unsigned _ttsLineId;
    unsigned _netTestLineId;
    
    static const unsigned MAX_CALLS = 8;
    BridgeCall _callSpace[MAX_CALLS];
    fixedvector<BridgeCall> _calls;
};

    }
}
