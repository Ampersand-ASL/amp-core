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
#include "Message.h"

// And a few things from AMP Server
#include "LocalRegistryStd.h"

using namespace std;
using namespace kc1fsz;

static const unsigned lineCount = 128;
static LineIAX2::Call iax2CallSpace[lineCount];

int main(int, const char**) {

    MTLog2 log;

    log.info("AMP Load Client 1");

    StdClock clock;

    // A queue used by other threads to pass messages into the main thread's
    // router.
    threadsafequeue2<MessageCarrier> respQueue;
    // A wrapper that makes the response queue look like a MessageConsumer
    QueueConsumer respQueueConsumer(respQueue);

    // This is the router (aka "bus") that passes Message objects between the rest 
    // of the components in the system. You'll see that everything else below is
    // wired to the router one way or the other.
    MultiRouter router(respQueue);

    // This is the Line that makes the IAX2 network connection
    LocalRegistryStd locReg;

    Runnable2* tasks[lineCount + 1];

    // Make a bunch of lines
    LineIAX2* lines[lineCount];
    for (unsigned i = 0; i < lineCount; i++) {
        // Each line only gets one call
        LineIAX2* line = new LineIAX2(log, log, clock, 100 + i, router, 0, 0, &locReg, 10, "radio",
            iax2CallSpace + i, 1);
        lines[i] = line;
        tasks[i + 1] = line;
        router.addRoute(line, 100 + i);
        int rc = line->open(AF_INET, 4570 + i);
        if (rc < 0) {
            log.error("Failed to open IAX2 line %d", rc);
        }
        // Place a test call
        char localNode[32];
        snprintf(localNode, sizeof(localNode), "%d", 1000 + i);
        //line->call(localNode, "radio@127.0.0.1:4569/2000,NONE");
        line->call(localNode, "672731", CODECType::IAX2_CODEC_G711_ULAW);
    }

    int count = 0;

    TimerTask timer0(log, clock, 10, [&clock, lines, &count]() {
        cout << "TICK" << endl;
        /*
        if (count == 20) {
            cout << "Sending audio" << endl;
            // Attempt to send an audio packet on all lines 

            uint8_t audio[640];
            float sampleRate = 16000;
            float omega = 2.0f * 3.1415926f * 400.0f / sampleRate;
            float phi = 0;
            uint8_t* p = audio;
            for (unsigned i = 0; i < 320; i++, p += 2) {
                int16_t sample = std::cos(phi) * 32767.0;
                pack_int16_le(sample, p);
                phi += omega;
            }

            uint64_t startUs = clock.timeUs();
            for (unsigned i = 0; i < lineCount; i++) {
                Message voice(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_16K,
                    sizeof(audio), audio, 1000, 1000);
                voice.setSource(1, 1);
                voice.setDest(100 + i, 21);
                lines[i]->consume(voice);
            }

            uint64_t endUs = clock.timeUs();
            cout << "Time " << (endUs - startUs) << endl;
        }
        */
        count++;
    });
    tasks[0] = &timer0;

    // Setup the EventLoop with all of the tasks that need to be run on this thread
    EventLoop::run(log, clock, 0, 0, tasks, std::size(tasks), nullptr, false);

    // #### TODO: At the moment there is no clean way to get out of the loop

    std::exit(0);
}