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
 *
 *
 * This file provides the main entry point for the AMP Server. All of the 
 * major components are instantiated and hooked together in this file so
 * it should be a good place to start to navigate the rest of the application.
 */
#include <execinfo.h>
#include <signal.h>
#include <iostream>

// Non-AMP stuff from my C++ tools library
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/MTLog2.h"
#include "kc1fsz-tools/threadsafequeue2.h"

// All of this comes from AMP Core
#include "TraceLog.h"
#include "EventLoop.h"
#include "ThreadUtil.h"
#include "MultiRouter.h"
#include "LineIAX2.h"
#include "Bridge.h"
#include "BridgeCall.h"
#include "TimerTask.h"

// And a few things from AMP Server
#include "LocalRegistryStd.h"

using namespace std;
using namespace kc1fsz;

int main(int, const char**) {

    MTLog2 log;

    log.info("AMP Load Server 1");

    StdClock clock;

    // A queue used by other threads to pass messages into the main thread's
    // router.
    threadsafequeue2<Message> respQueue;
    // A wrapper that makes the response queue look like a MessageConsumer
    QueueConsumer respQueueConsumer(respQueue);

    // This is the router (aka "bus") that passes Message objects between the rest 
    // of the components in the system. You'll see that everything else below is
    // wired to the router one way or the other.
    MultiRouter router(respQueue);

    // This is the Line that makes the IAX2 network connection
    LocalRegistryStd locReg;
    LineIAX2 iax2Channel1(log, log, clock, 1, router, 0, 0, &locReg, 10, "radio");
    router.addRoute(&iax2Channel1, 1);

    int rc = iax2Channel1.open(AF_INET, 4569);
    if (rc < 0) {
        log.error("Failed to open IAX2 line %d", rc);
    }

    // Setup the EventLoop with all of the tasks that need to be run on this thread
    Runnable2* tasks[] = { &iax2Channel1 };
    EventLoop::run(log, clock, 0, 0, tasks, std::size(tasks), nullptr, false);

    // #### TODO: At the moment there is no clean way to get out of the loop

    std::exit(0);
}