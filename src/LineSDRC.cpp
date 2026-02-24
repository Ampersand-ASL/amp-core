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
#include <unistd.h>
#include <fcntl.h>

//#include <termios.h>
//#include <linux/serial.h>
//#include <sys/ioctl.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#endif

#include <cstring>
#include <iostream>
#include <algorithm>

// 3rd party
#include "crc.h"
#include "cobs.h"

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/Log.h"

#include "Transcoder_SLIN_8K.h"
#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Message.h"

#include "SerialUtil.h"
#include "LineSDRC.h"

#define NETWORK_BAUD (460800)

using namespace std;

namespace kc1fsz {

LineSDRC::LineSDRC(Log& log, Log& traceLog, Clock& clock, unsigned lineId, unsigned callId,
    MessageConsumer& bus, unsigned destLineId)
:   _log(log),
    _traceLog(traceLog),
    _clock(clock),
    _lineId(lineId),
    _callId(callId),
    _bus(bus),
    _destLineId(destLineId),
    _rxBufPtrMask(sizeToBitMask(RX_BUF_SIZE)),
    _rxBufHandler(_rxBuf, RX_BUF_SIZE) {
}

int LineSDRC::open(const char* serialDevice) {

    close();

    _fd = ::open(serialDevice, O_RDWR | O_NONBLOCK | O_NOCTTY);

    int rc = SerialUtil::configurePort(_fd, NETWORK_BAUD);
    if (rc == -1 || rc == -3 || rc == -4) {
        _log.error("Invalid baud %d", NETWORK_BAUD);
        ::close(_fd);
        return -1;
    }
    else if (rc != 0) {
        _log.error("Unable to open SDRC network port %d", rc);
        ::close(_fd);
        return -2;
    }

    // Generate the same kind of call start message that would
    // come from the IAX2Line after a new connection.
    PayloadCallStart payload;
    payload.codec = CODECType::IAX2_CODEC_SLIN_8K;
    payload.bypassJitterBuffer = false;
    payload.echo = false;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "sdrc");
    payload.originated = true;
    payload.permanent = true;
    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(_destLineId, Message::BROADCAST);
    _bus.consume(msg);

    _log.info("LineSDRC %d/%d opened on %s", _lineId, _callId, serialDevice);

    _originMsCounter = 100;

    return 0;
}

void LineSDRC::close() {   
    if (_fd != -1) {
        ::close(_fd);
        _fd = -1;
    }
    PayloadCallEnd payload;
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "sdrc");
    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_END, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(_destLineId, Message::BROADCAST);
    _bus.consume(msg);
} 

int LineSDRC::getPolls(pollfd* fds, unsigned fdsCapacity) {
    int used = 0;
    if (_fd != -1) {
        // We're only watching for receive events
        fds[used].fd = _fd;
        fds[used].events = POLLIN;
        used++;
    }
    return used;
}

/**
 * This function gets called when an audio/text message is available.
 */
void LineSDRC::consume(const Message& frame) {  

    if (frame.getType() == Message::Type::AUDIO) {

        if (_fd == -1)
            return;

        assert(frame.size() == BLOCK_SIZE_8K * 2);
        assert(frame.getFormat() == CODECType::IAX2_CODEC_SLIN_8K);

        uint8_t packet[NETWORK_MESSAGE_SIZE];
        DigitalAudioPortRxHandler::encodeMsg(frame.body(), frame.size(),
            packet, NETWORK_MESSAGE_SIZE);
        int rc1 = write(_fd, packet, NETWORK_MESSAGE_SIZE);
        if (rc1 != NETWORK_MESSAGE_SIZE) {
            _log.error("SDRC write failed %d", rc1);
        }
    }
}

void LineSDRC::audioRateTick(uint32_t tickMs) {
}

void LineSDRC::oneSecTick() { 
}

void LineSDRC::tenSecTick() {
}

bool LineSDRC::run2() {   
    // Pull from socket if possible
    bool r = _rxIfPossible();
    if (r) {
        // Check to see if we've got a full audio message. If so, package
        // up a Message and send out on the bus.
        _rxBufHandler.processRxBuf(_rxBufWrPtr,
            // Callback fired when a full/valid message is received
            // from the network.
            [this](const uint8_t* frame, unsigned frameLen) {
                assert(frameLen == BLOCK_SIZE_8K * 2);
                // Make an audio message and send it to the listeners for 
                // processing. This is convenient because the audio comes in 
                // from the SDRC in 16-bit linear format LE already.
                MessageWrapper msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_8K,
                    frameLen, frame, _originMsCounter, _clock.time());
                msg.setSource(_lineId, _callId);
                msg.setDest(_destLineId, Message::BROADCAST);
                _bus.consume(msg);
                // Make these audio frames perfectly spaced
                _originMsCounter += 20;
            }
        );
    }
    return r;
}

bool LineSDRC::_rxIfPossible() {
    if (_fd == -1)
        return false;
    // Read as much as possible without running off the end 
    unsigned linearSpaceAvailable = RX_BUF_SIZE - _rxBufWrPtr;
    int rc2 = ::read(_fd, _rxBuf + _rxBufWrPtr, linearSpaceAvailable);
    if (rc2 == 0) {
        return false;
    }
    else if (rc2 < 0) {
        _log.error("Read error %d", rc2);
        return false;
    } else {
        // Move the write pointer forward and wrap
        _rxBufWrPtr = (_rxBufWrPtr + rc2) & _rxBufPtrMask;
        return true;
    }
}

}
