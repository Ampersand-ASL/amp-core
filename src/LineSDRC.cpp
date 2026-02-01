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

#include <termios.h>
#include <linux/serial.h>
#include <sys/ioctl.h>

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

#include "LineSDRC.h"

#define NETWORK_BAUD (B460800)

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

    // Create new termios struct, we call it 'tty' for convention
    // No need for "= {0}" at the end as we'll immediately write the existing
    // config to this struct
    struct termios tty;

    // Read in existing settings, and handle any error
    // NOTE: This is important! POSIX states that the struct passed to tcsetattr()
    // must have been initialized with a call to tcgetattr() overwise behaviour
    // is undefined
    if (tcgetattr(_fd, &tty) != 0) {
        _log.error("Error %i from tcgetattr\n", errno);
    }
    cout << "Speed " << (int)cfgetospeed(&tty) << endl;
    cout << "? " << NETWORK_BAUD << endl;

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE; 
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS; 
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;
    
    // Specifying a custom baud rate when using GNU C
    if (cfsetispeed(&tty, NETWORK_BAUD) != 0)
        _log.error("Invalid baud %d", NETWORK_BAUD);
    if (cfsetospeed(&tty, NETWORK_BAUD) != 0)
        _log.error("Invalid baud %d", NETWORK_BAUD);    
    if (tcsetattr(_fd, TCSANOW, &tty) != 0) {
        _log.error("Error %i from tcsetattr\n", errno);
    }

    // Generate the same kind of call start message that would
    // come from the IAX2Line after a new connection.
    PayloadCallStart payload;
    payload.codec = CODECType::IAX2_CODEC_SLIN_8K;
    payload.bypassJitterBuffer = false;
    payload.echo = false;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "sdrc-%d", _lineId);
    payload.originated = true;
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
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
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "sdrc-%d", _lineId);
    Message msg(Message::Type::SIGNAL, Message::SignalType::CALL_END, 
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
                Message msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_SLIN_8K,
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
