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

#include "Message.h"
#include "MessageConsumer.h"

namespace kc1fsz {

/**
 * A simple MessageBus for routing messages to one of two destinations.
 */
class TwoLineRouter : public MessageConsumer {
public:

    TwoLineRouter(MessageConsumer& con0, unsigned line0, MessageConsumer& con1, unsigned line1)
    :   _con0(con0), _line0(line0), _con1(con1), _line1(line1) { }

    void consume(const Message& msg) {
        if (msg.getDestBusId() == _line0) 
            _con0.consume(msg);
        else if (msg.getDestBusId() == _line1) 
            _con1.consume(msg);
        else 
            assert(false);
    }

private:

    MessageConsumer& _con0;
    unsigned _line0;
    MessageConsumer& _con1;
    unsigned _line1;
};

}
