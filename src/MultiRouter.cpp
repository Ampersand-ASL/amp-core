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

MultiRouter::MultiRouter(threadsafequeue2<Message>& auxQueue)
:   _auxQueue(auxQueue) {     
}

bool MultiRouter::run2() {
    bool worked = false;
    while (!_auxQueue.empty()) {
        Message m;
        // Goal is to avoid blocking and return back to the event loop 
        // as quickly as possible.
        if (_auxQueue.try_pop(m, 0)) {
            consume(m);
            worked = true;
        }
    }
    return worked;
}

void MultiRouter::consume(const Message& msg) {
    // Dispatch the message to its intended destination
    for (auto it = _dests.begin(); it != _dests.end(); it++) {
        if (it->lineId == msg.getDestBusId() || it->lineId == BROADCAST) {
            it->consumer->consume(msg);
        }
    }
}

void MultiRouter::addRoute(MessageConsumer* consumer, unsigned lineId) { 
    _dests.push_back({ .consumer=consumer, .lineId = lineId });
}

}
