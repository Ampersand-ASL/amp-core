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
#include "MultiRouter.h"

namespace kc1fsz {

MultiRouter::MultiRouter(threadsafequeue<Message>& queue) 
:   _queue(queue) { }

bool MultiRouter::run2() {
    if (_queue.empty())
        return false;
    while (!_queue.empty()) {
        Message msg;
        if (_queue.try_pop(msg)) {
            consume(msg);
        }
    }
    return true;
}

void MultiRouter::consume(const Message& msg) {
    for (auto it = _dests.begin(); it != _dests.end(); it++) {
        if (it->lineId == msg.getDestBusId()) {
            it->consumer->consume(msg);
        }
    }
}

void MultiRouter::addRoute(MessageConsumer* consumer, unsigned lineId) { 
    _dests.push_back({ .consumer=consumer, .lineId = lineId });
}

}
