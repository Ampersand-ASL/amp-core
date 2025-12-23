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

#include <atomic>

#include "kc1fsz-tools/threadsafequeue.h"
#include "kc1fsz-tools/copyableatomic.h"

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer; 

    namespace amp {

/**
 * A web server for a simple UI.  
 */
class WebUi : public Runnable2, public MessageConsumer {
public:

    WebUi(Log& log, Clock& clock, MessageConsumer& cons, unsigned listenPort,
        unsigned networkDestLineId, unsigned radioDestLineId);

    // ----- MessageConsumer --------------------------------------------

    void consume(const Message& msg);

    // ----- Runnable2 --------------------------------------------------

    bool run2();

private:

    struct Peer { 
        std::string remoteNumber;
        // #### TODO CHANGE TO 64
        uint32_t startMs = 0;
        uint64_t lastRxMs = 0;
        uint64_t lastTxMs = 0;
    };

    static void _uiThread(void*);
    void _thread();

    Log& _log;
    Clock& _clock;
    MessageConsumer& _consumer;
    const unsigned _listenPort;
    const unsigned _networkDestLineId;
    const unsigned _radioDestLineId;
    std::atomic<bool> _cos;
    bool _ptt = false;
    threadsafequeue<Message> _outQueue;
    copyableatomic<std::vector<Peer>> _status;
};

    }
}