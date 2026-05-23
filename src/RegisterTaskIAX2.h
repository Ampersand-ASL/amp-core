/**
 * Copyright (C) 2026, Bruce MacKinnon KC1FSZ
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

#include <netinet/in.h>

#include "kc1fsz-tools/fixedstring.h"
#include "kc1fsz-tools/StateMachine.h"

#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;

class RegisterTaskIAX2 : public Runnable2 {
public:

    RegisterTaskIAX2(Log& log, Clock& clock);

    /**
     * NOTE: This function can be called at any time. The updates will take effect
     * in the next polling cycle.
     * 
     * @param regServerAddr The IP address of the ASL registration server.
     */
    void configure(const char* regServerAddr, const char* nodeNumber, const char* password, 
        unsigned iaxPort);

    // ----- Runnable -------------------------------------------------------

    virtual bool run2();
    void tenSecTick();

private:

    void _doRegister();
    bool _openIAX();
    void _closeIAX();
    bool _sendREGREQ();
    bool _processInboundIAXData();
    void _processReceivedIAXPacket(const uint8_t* buf, unsigned bufLen, 
        const sockaddr& peerAddr, uint32_t stampMs);
    void _sendFrameToPeer(const uint8_t* b, unsigned len, 
        const sockaddr& peerAddr);

    Log& _log;
    Clock& _clock;
    int _iaxFd;

    fixedstring _regServerAddr;
    fixedstring _nodeNumber;
    fixedstring _password;
    int _iaxPort;

    uint32_t _regIntervalMs;
    uint64_t _lastGoodRegistrationMs = 0;

    enum State {
        STATE_IDLE,
        // A registration needs to be started
        STATE_REG_PENDING,
        // First REGREQ sent, waiting for REGAUTH
        STATE_0,
        // Second REGAUTH sent, waiting for REGACK
        STATE_1,
        // The request was rejected or timed out.
        STATE_FAILED
    };
    
    StateMachine _state;
    fixedstring _challengeResponse;
};

}
