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

#include "kc1fsz-tools/threadsafequeue.h"

#include "Message.h"
#include "Runnable2.h"
#include "MessageConsumer.h"

namespace kc1fsz {

/**
 * A simple MessageBus for routing messages to one of two destinations.
 */
class MultiRouter : public MessageConsumer, public Runnable2 {
public:

    MultiRouter(threadsafequeue<Message>& queue);

    void addRoute(MessageConsumer* consumer, unsigned lineId);

    void consume(const Message& msg);
    bool run2();

    struct Dest {
        MessageConsumer* consumer;
        unsigned lineId;
    };

private:

    threadsafequeue<Message>& _queue;
    std::vector<Dest> _dests;
};

}
