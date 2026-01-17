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

#include <functional>

// SDRC stuff
#include "DigitalAudioPortRxHandler.h"

#include "Line.h"
#include "IAX2Util.h"
#include "Message.h"
#include "MessageConsumer.h"

namespace kc1fsz {

class Log;
class Clock;

/**
 * An implementation of the Line interface that communicates with
 * the Software Defined Repeater Controller (SDRC) platform.
 */
class LineSDRC : public Line {
public:

    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_PERIOD_MS = 20;

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. VERY IMPORTANT: Audio frames will not have been 
     * de-jittered before they are passed to this sink. 
     */
    LineSDRC(Log& log, Log& traceLog, Clock& clock, unsigned lineId, unsigned callId, 
        MessageConsumer& consumer, unsigned destLineId);

    // Configuration 

    /**
     * Opens the network connection for in/out traffic for this line.
     * @returns 0 if the open was successful.
     */
    int open(const char* serialDevice);

    void close();

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();

    /**
     * Audio rate tick is required here because of some background (timeout)
     * tasks that are still happening.
     * #### TODO: REMOVE THIS
     */
    virtual void audioRateTick(uint32_t tickMs);
    
    virtual void oneSecTick();

    virtual void tenSecTick();

    /**
     * This function is called by the EventLoop to collect the list of file 
     * descriptors that need to be monitored for asynchronous activity.
     */
    virtual int getPolls(pollfd* fds, unsigned fdsCapacity);

private:

    /**
     * Called whenever there is inbound data available on the socket. Doesn't 
     * hurt to call it when there is no data.
     */
    bool _rxIfPossible();

    Log& _log;
    Log& _traceLog;
    Clock& _clock;
    const unsigned _lineId;
    const unsigned _callId;
    MessageConsumer& _bus;
    // The line that all messages are directed to
    const unsigned _destLineId;

    int _fd = -1;

    // Must be a power-of-two size!
    static const unsigned RX_BUF_SIZE = 512;
    uint8_t _rxBuf[RX_BUF_SIZE];
    // Mask used to make the buffer circular
    const unsigned _rxBufPtrMask;
    // The next write position in a circular buffer
    unsigned _rxBufWrPtr = 0;
    // Manager for the receive buffer
    DigitalAudioPortRxHandler _rxBufHandler;
};

}
