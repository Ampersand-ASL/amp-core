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

#include <cassert>
#include <vector>

#include "Message.h"
#include "Runnable2.h"
#include "MessageConsumer.h"

namespace kc1fsz {

/**
 * A simple MessageBus for routing messages to registered destinations.
 */
class SimpleRouter : public MessageConsumer, public Runnable2 {
public:

    void addRoute(MessageConsumer* consumer, unsigned lineId) {
        if (_destCount < MAX_DEST)
            _dests[_destCount++] = { .consumer = consumer, .lineId = lineId };
    }

    void consume(const Message& msg) {
        for (unsigned i = 0; i < _destCount; i++)
            if (msg.getDestBusId() == _dests[i].lineId || 
                Message::BROADCAST == _dests[i].lineId)
                _dests[i].consumer->consume(msg);
    }

private:

    struct Dest {
        MessageConsumer* consumer;
        unsigned lineId;
    };

    static const unsigned MAX_DEST = 8;
    Dest _dests[MAX_DEST];
    unsigned _destCount = 0;
};

}
