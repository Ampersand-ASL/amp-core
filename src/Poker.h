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
#include "Message.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * Node diagnostic function.
 */
class Poker {
public:

    struct Request {
        char nodeNumber[16] = { 0 };
        unsigned timeoutMs = 250;
    };

    struct Result {

        /**
         * The result code
         *  0:  Successful POKE
         * -1:  Node unknown
         * -9:  Timeout waiting for PONG response
         */
        int code = 0;

        char addr4[64] = { 0 };
        int port = 0;
        uint32_t pokeTimeMs = 0;
    };

    /**
     * Sends a POKE message to the designated node and waits for 
     * the corresponding PONG.
     */
    static Result poke(Log& log, Clock& clock, const char* nodeNumber, 
        unsigned timeoutMs = 250);
 
    static Result poke(Log& log, Clock& clock, Request req);

    /**
     * A utility function that can be run on an independent thread. 
     * @param runFlag Keep this set to true until it's time to exit the loop.
     */
    static void loop(Log* log, Clock* clock, 
        threadsafequeue<Message>* reqQueue, threadsafequeue<Message>* respQueue,
        std::atomic<bool>* runFlag);
};

}
