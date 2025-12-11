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

#include <fstream>

#include "kc1fsz-tools/fixedqueue.h"

#include "Runnable.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "Channel.h"

namespace kc1fsz {

class Log;
class MessageConsumer;
class Clock;

class Analyzer : public Runnable, public MessageConsumer {
public:

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;

    Analyzer(Log&, Clock&, MessageConsumer& consumer, unsigned target);

    virtual void consume(const Message& frame);

    // ----- From Runnable -------------------------------------------------------

    virtual void fastTick();
    virtual void audioRateTick();
    virtual void oneSecTick();

private:

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;
    unsigned _target;
};

}
