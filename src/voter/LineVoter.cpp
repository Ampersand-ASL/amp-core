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
#include <unistd.h>
#include <fcntl.h>

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
#include <sstream>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"

#include "MessageConsumer.h"
#include "Message.h"

#include "voter/VoterUtil.h"
#include "voter/VoterPeer.h"
#include "voter/LineVoter.h"

#define CALL_ID_FIXED (1)

using namespace std;

namespace kc1fsz {

//static uint32_t alignToTick(uint32_t ts, uint32_t tick) {
//    return (ts / tick) * tick;
//}

LineVoter::LineVoter(Log& log, Clock& clock, unsigned lineId,
    MessageConsumer& bus, unsigned audioDestLineId)
:   _log(log),
    _clock(clock),
    _lineId(lineId),
    _bus(bus),
    _audioDestLineId(audioDestLineId) {

    for (unsigned i = 0; i < MAX_PEERS; i++) {
        _clients[i].init(&clock, &log);
        // Make the connection so we can send packets out to the client
        _clients[i].setSink([this]
            (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
                _sendPacketToPeer(data, dataLen, addr);
            }
        );
    }
}

int LineVoter::open(short addrFamily, int listenPort) {

    // If the configuration is changing then ignore the request
    if (addrFamily == _addrFamily &&
        _listenPort == listenPort &&
        _sockFd != 0) {
        return 0;
    }

    close();

    _addrFamily = addrFamily;
    _listenPort = listenPort;

    _log.info("Listening on VOTER port %d", _listenPort);

    // UDP open/bind
    int sockFd = socket(_addrFamily, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        _log.error("Unable to open VOTER port (%d)", errno);
        return -1;
    }    

    // #### TODO RAAI TO ADDRESS LEAKS BELOW
    
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("VOTER setsockopt SO_REUSEADDR failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    struct sockaddr_storage servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.ss_family = _addrFamily;
    
    if (_addrFamily == AF_INET) {
        // Bind to all interfaces
        // Or, specify a particular IP address: servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        // TODO: Allow this to bec controlled
        ((sockaddr_in&)servaddr).sin_addr.s_addr = INADDR_ANY;
        ((sockaddr_in&)servaddr).sin_port = htons(_listenPort);
    } else if (_addrFamily == AF_INET6) {
        // To bind to all available IPv6 interfaces
        // TODO: Allow this to bec controlled
        ((sockaddr_in6&)servaddr).sin6_addr = in6addr_any; 
        ((sockaddr_in6&)servaddr).sin6_port = htons(_listenPort);
    } else {
        assert(false);
    }

    if (::bind(sockFd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        _log.error("Unable to bind to VOTER port (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    if (makeNonBlocking(sockFd) != 0) {
        _log.error("open fcntl failed (%d)", errno);
        ::close(sockFd);
        return -1;
    }

    _sockFd = sockFd;

    // Generate a start of call message so that the bridge will accept audio
    // when it arrives.
    PayloadCallStart payload;
    payload.codec = CODECType::IAX2_CODEC_G711_ULAW;
    payload.bypassJitterBuffer = true;
    payload.echo = false;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "VOTER");
    payload.originated = true;
    payload.permanent = true;
    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_lineId, CALL_ID_FIXED);
    msg.setDest(_audioDestLineId, Message::BROADCAST);
    _bus.consume(msg);

    return 0;
}

void LineVoter::close() {   
    if (_sockFd) 
        ::close(_sockFd);
    _sockFd = 0;
    _listenPort = 0;
    _addrFamily = 0;
} 

void LineVoter::setServerPassword(const char* p) {

    // A random challenge for security reasons
    _serverPassword = p;
    _serverChallenge = amp::VoterPeer::makeChallenge();

    for (unsigned i = 0; i < MAX_PEERS; i++) {
        _clients[i].setLocalPassword(p);
        _clients[i].setLocalChallenge(_serverChallenge.c_str());
    }
}

void LineVoter::setClientPasswords(const char* ps) {
    // Parse comma-delimited list
    string s(ps);
    stringstream ss(s);
    string token;
    unsigned peerCount = 0;
    while (std::getline(ss, token, ',') && peerCount < MAX_PEERS) {
        trim(token);
        if (token.empty())
            continue;
        bool transmitFlag = false;
        string password = token;
        // Are ther flags besides the password?
        size_t index = token.find('/');
        if (index != std::string::npos) {
            password = token.substr(0, index);
            string flags = token.substr(index + 1);
            if (flags.find("transmit") != std::string::npos)
                transmitFlag = true;
        }
        if (password.length() > 9)
            continue;
        _clients[peerCount].setRemotePassword(password.c_str());
        _clients[peerCount].setAudioTransmit(transmitFlag);
        // Only the first client is the master source
        _clients[peerCount].setMasterTimingSource(peerCount == 0);
        peerCount++;
    }
}

void LineVoter::consume(const Message& msg) {   
    if (msg.isVoice()) {
        // This handles internal voice from conference out to VOTER client
        assert(msg.getFormat() == CODECType::IAX2_CODEC_G711_ULAW);
        assert(msg.size() == BLOCK_SIZE_8K);
        for (unsigned i = 0; i < MAX_PEERS; i++)
            if (_clients[i].isPeerTrusted() && _clients[i].isAudioTransmit())
                // NOTE: RSSI value has no significance on transmit
                _clients[i].sendAudio(0, msg.body(), msg.size(), msg.getRxMs());
    }
}

bool LineVoter::run2() {   
    return _processInboundData();
}

void LineVoter::audioRateTick(uint32_t ms) {

    // Pass the tick to all of the trusted clients
    for (unsigned i = 0; i < MAX_PEERS; i++)
        if (_clients[i].isPeerTrusted())
            _clients[i].audioRateTick(ms);

    // #### TODO: VOTER LOGIC!
    // At the moment it is first-come, first-served

    for (unsigned i = 0; i < MAX_PEERS; i++) {
        if (_clients[i].isPeerTrusted() && _clients[i].isAudioAvailable()) {

            // Extract the current audio frame from the VOTER buffer
            uint8_t ulaw8[BLOCK_SIZE_8K];
            _clients[i].getAudioFrame(ms, ulaw8, BLOCK_SIZE_8K);

            // Make a message and transmit to the Bridge
            MessageWrapper msg(Message::Type::AUDIO, 0, BLOCK_SIZE_8K, ulaw8, 0, 0);
            msg.setSource(_lineId, i);
            msg.setDest(_audioDestLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(msg);

            _clients[i].popAudioFrame();
            break;
        } 
    }
}

void LineVoter::oneSecTick() {    
    for (unsigned i = 0; i < MAX_PEERS; i++)
        if (_clients[i].isPeerTrusted())
            _clients[i].oneSecTick();
}

void LineVoter::tenSecTick() {
    for (unsigned i = 0; i < MAX_PEERS; i++)
        if (_clients[i].isPeerTrusted())
            _clients[i].tenSecTick();
}

int LineVoter::getPolls(pollfd* fds, unsigned fdsCapacity) {
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

bool LineVoter::_processInboundData() {

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

void LineVoter::_processReceivedPacket(
    const uint8_t* packet, unsigned packetLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {

    char addr[64];
    formatIPAddrAndPort((const sockaddr&)peerAddr, addr, 64);
   
    if (_trace) {
        char msg[128];
        snprintf(msg, sizeof(msg), "VOTER packet from %s", addr);
        _log.infoDump(msg, packet, packetLen);
    }
   
    // Figure out who the packet belongs to.
    for (unsigned i = 0; i < MAX_PEERS; i++) {
        if (_clients[i].belongsTo(peerAddr, packet, packetLen)) {
            _clients[i].consumePacket(peerAddr, packet, packetLen);
            return;
        } 
    }

    // If we get here then the message wasn't trusted. Generate a challenge
    // to try to establish communications with a potential client.
    // ### TODO: MAKE THIS SMARTER TO AVOID DUPLICATES
    _log.info("VOTER packet from %s, not trusted yet", addr);

    uint8_t resp[24] = { 0 };
    // If a zero auth response is received then send back the initial 
    // challenge response.
    int rc = amp::VoterPeer::makeInitialChallengeResponse(&_clock, packet,
        _serverChallenge.c_str(), _serverPassword.c_str(), resp);
    assert(rc == 24);
    _sendPacketToPeer(resp, 24, peerAddr);
}

void LineVoter::_sendPacketToPeer(const uint8_t* b, unsigned len, 
    const sockaddr& peerAddr) {

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

    if (_trace) {
        char addr[64];
        formatIPAddrAndPort((const sockaddr&)peerAddr, addr, 64);
        char msg[128];
        snprintf(msg, sizeof(msg), "VOTER packet to %s", addr);
        _log.infoDump(msg, b, len);
    }
}

}
