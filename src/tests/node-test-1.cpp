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
#include <iostream>
#include <cmath> 
#include <queue>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/LinuxPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/fixedqueue.h"

#include "Message.h"
#include "MessageBus.h"
#include "EventLoop.h"
#include "NodeParrot.h"

using namespace std;
using namespace kc1fsz;

class TestBus : public MessageConsumer {
public:

    virtual void consume(const Message& frame) {
    }
};

int main(int, const char**) {

    Log log;
    log.info("Start");   
    StdClock clock;

    TestBus bus0;
    NodeParrot parrot0(log, clock, bus0);

    // Trigger the start of a call
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 0, 0, 
        0, clock.timeMs());
    msg.setSource(0, 1);
    parrot0.consume(msg);

    // Main loop        
    Runnable* tasks[16] = { &parrot0 };
    unsigned taskCount = 1;

    EventLoop::run(log, clock, tasks, taskCount);
}
