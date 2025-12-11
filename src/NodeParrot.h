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

#include <queue>
#include <list>
#include <cstdint>

#include "kc1fsz-tools/fixedvector.h"

#include "AdaptorIn.h"
#include "AdaptorOut.h"
#include "MessageConsumer.h"
#include "Runnable2.h"
#include "PCM16Frame.h"

namespace kc1fsz {

class Log;
class Clock;

class NodeParrot : public MessageConsumer, public Runnable2 {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;
    
    static const unsigned SESSION_TIMEOUT_MS = 120 * 1000;

    NodeParrot(Log&, Clock&, MessageConsumer&);

    void reset();
    void setToneEnabled(unsigned lineId, unsigned callId, bool en);

    // ----- MessageConsume Interface ------------------------------------------
    
    virtual void consume(const Message& frame);

    // ----- Runnable Interface ------------------------------------------------

    int getPolls(pollfd* fds, unsigned fdsCapacity) { return 0; }
    bool run2() { return false; }
    virtual void audioRateTick();
    virtual void tenSecTick();

private:

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;

    enum State {
        NONE,
        ACTIVE,
        CONNECTED,
        PLAYING_PROMPT_GREETING,
        WAITING_FOR_RECORD,
        RECORDING,
        PAUSE_AFTER_RECORD,
        PLAYING,
        TIMEDOUT
    };

    class Session {
    public:

        bool active = false;
        unsigned lineId = 0;
        unsigned callId = 0;
        State state = State::NONE;
        uint32_t callStartTime = 0;
        uint32_t lastAudioTime = 0;
        uint32_t stateStartTime = 0;
        unsigned playQueueDepth = 0;

        // These two are used to convert from/to the caller's desired format
        // into the "common bus format" used internally.
        AdaptorIn adaptorIn;
        AdaptorOut adaptorOut;

        // The audio waiting to be sent to the caller in PCM16 48K format.
        std::queue<PCM16Frame> playQueue;    

        bool toneActive = false;
        float toneOmega;
        float tonePhi;
        float toneLevel;
        
        void reset();
        bool belongsTo(const Message& msg) const;
        void audioRateTick(Log& log, Clock& clock, NodeParrot& node);
    };

    static constexpr auto RESET_VISITOR = [](NodeParrot::Session& s) { s.reset(); return true; };

    static const unsigned MAX_SESSIONS = 16;
    Session _sessionsStore[MAX_SESSIONS];
    fixedvector<Session> _sessions;

    void _consumeAudioInSession(Session&, const Message& frame);
    void _produceAudioInSession(Session&, const Message& frame);

public:

    void _loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const;
    void _loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const;
    Message _makeMessage(const PCM16Frame& frame, unsigned destBusId, unsigned destCallId) const;
};

}
