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

#include "kc1fsz-tools/threadsafequeue2.h"

#include "Message.h"

namespace kc1fsz {

class MessageConsumer {
public:

    virtual void consume(const Message& msg) = 0;
};

/**
 * A consumer that just pushes the message on a queue.
 */
class QueueConsumer : public MessageConsumer  {
public: 

    QueueConsumer(threadsafequeue2<MessageCarrier>& q) : _q(q) { }

    void consume(const Message& msg) { 
        // Here we take a copy of the message onto the queue
        _q.push(MessageCarrier(msg)); 
    }

private:

    threadsafequeue2<MessageCarrier>& _q;
};

}

