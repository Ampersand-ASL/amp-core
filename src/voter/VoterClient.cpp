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
#ifndef PICO_BOARD
#include <unistd.h>
#include <fcntl.h>
#endif

#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#include <sys/types.h>

#include <cstring>
#include <iostream>
#include <algorithm>

#include "MessageConsumer.h"

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"

#include "Message.h"

#include "voter/VoterPeer.h"
#include "voter/VoterUtil.h"
#include "voter/VoterClient.h"

#define BLOCK_SIZE_8K (160)

using namespace std;

namespace kc1fsz {

VoterClient::VoterClient(Log& log, Clock& clock, int lineId,
    MessageConsumer& bus, unsigned audioDestLineId)
:   _log(log),
    _clock(clock),
    _lineId(lineId),
    _bus(bus),
    _audioDestLineId(audioDestLineId),
    _client(true) {

    _client.init(&clock, &log);
    // Make the connection so we can send packets out to the client
    _client.setSink([this]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
            _sendPacketToPeer(data, dataLen, addr);
        }
    );
    _client.setAudioTransmit(true);
}

int VoterClient::open(const char* serverAddrAndPort) {

    close();
    
    // Parse the server address and determine IPv4 vs IPv6
    int rc = parseIPAddrAndPort(serverAddrAndPort, _serverAddr);
    if (rc != 0) {
        return -1;
    }
    _addrFamily = _serverAddr.ss_family;

    // UDP open/bind
    int sockFd = socket(_addrFamily, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        _log.error("Unable to open Voter port (%d)", errno);
        return -1;
    }    

    // #### TODO RAAI TO ADDRESS LEAKS BELOW
    
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("Voter setsockopt SO_REUSEADDR failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    if (makeNonBlocking(sockFd) != 0) {
        _log.error("open fcntl failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    _sockFd = sockFd;

    _client.setPeerAddr(_serverAddr);

    _log.info("Opened connection to %s", serverAddrAndPort);

    return 0;
}

void VoterClient::close() {   
    if (_sockFd) 
        ::close(_sockFd);
    _sockFd = 0;
    _listenPort = 0;
    _addrFamily = 0;
} 

void VoterClient::setServerPassword(const char* p) {
    _client.setRemotePassword(p);
}

void VoterClient::setClientPassword(const char* p) {
    // A random challenge for security 
    _client.setLocalChallenge(amp::VoterPeer::makeChallenge().c_str());
    _client.setLocalPassword(p);
}

void VoterClient::consume(const Message& m) {   
    if (m.isVoice()) {
        // #### TEMP
        //_log.info("Time %ld %ld", (long)m.getRxMs(), (long)_clock.timeUs() / 1000L);
        if (_client.isPeerTrusted()) 
            _client.sendAudio(_rssi, m.body(), m.size(), m.getRxMs());
    }
}

bool VoterClient::run2() {   
    return _processInboundData();
}

void VoterClient::audioRateTick(uint32_t ms) {

    _client.audioRateTick(ms);

    if (_client.isAudioAvailable()) {

        // Extract the current audio frame from the Voter buffer
        uint8_t ulaw8[BLOCK_SIZE_8K];
        _client.getAudioFrame(ms, ulaw8, BLOCK_SIZE_8K);

        MessageWrapper msg(Message::Type::AUDIO, 0, BLOCK_SIZE_8K, ulaw8, 0, 0);
        msg.setSource(_lineId, Message::UNKNOWN_CALL_ID);
        msg.setDest(_audioDestLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(msg);

        _client.popAudioFrame();
    } 
}

void VoterClient::oneSecTick() {
    if (_sockFd)
        _client.oneSecTick();    
}

void VoterClient::tenSecTick() {
    _client.tenSecTick();    
}

int VoterClient::getPolls(pollfd* fds, unsigned fdsCapacity) {
    if (fdsCapacity < 1) 
        return -1;
    int used = 0;
    if (_sockFd) {
        // We're only watching for receive events
        fds[used].fd = _sockFd;
        fds[used].events = POLLIN;
        used++;
    }
    return used;
}

bool VoterClient::_processInboundData() {

    if (!_sockFd)
        return false;

    // Check for new data on the socket
    // ### TODO: MOVE TO CONFIG AREA
    const unsigned readBufferSize = 2048;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
    int rc = recvfrom(_sockFd, readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
    if (rc == 0) {
        return false;
    } 
    else if (rc == -1 && errno == 11) {
        return false;
    } 
    else if (rc > 0) {
        _processReceivedPacket(readBuffer, rc, (const sockaddr&)peerAddr, _clock.time());
        // Return back to be nice, but indicate that there might be more
        return true;
    } else {
        // #### TODO: ERROR COUNTER
        _log.error("Voter read error %d/%d", rc, errno);
        return false;
    }
}

void VoterClient::_processReceivedPacket(
    const uint8_t* packet, unsigned packetLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {
    _client.consumePacket(peerAddr, packet, packetLen);
}

void VoterClient::_sendPacketToPeer(const uint8_t* b, unsigned len, 
    const sockaddr& peerAddr) {

    //_log.infoDump("Sending packet", b, len);

    if (!_sockFd)
        return;

    int rc = ::sendto(_sockFd, 
        b,
        len, 0, &peerAddr, getIPAddrSize(peerAddr));
    if (rc < 0) {
        if (errno == 101) {
            char temp[64];
            formatIPAddrAndPort(peerAddr, temp, 64);
            _log.error("Network is unreachable to %s", temp);
        } else 
            _log.error("Send error %d", errno);
    }
}


}
