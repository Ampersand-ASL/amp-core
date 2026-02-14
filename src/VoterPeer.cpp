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
#include <iostream>
#include <string>
#include <cstring>
#include <cassert>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/NetUtils.h"

#include "VoterUtil.h"
#include "VoterPeer.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

    /* From docs:       
       When the client starts operating, it starts periodically sending VOTER packets 
       to the host containing a payload value of 0, the client's challenge string and 
       an authentication response (digest) of 0 (to indicate that it has not as of yet 
       gotten a valid response from the host). When such a packet is received by the host, 
       it responds with a packet containing a payload value of 0, the host's challenge string, 
       an authentication response (digest) based upon the calculated CRC-32 value of the 
       challenge string it received from the client and the host's password, and option flags.    
    */


VoterPeer::VoterPeer() {
    reset();
}

VoterPeer::~VoterPeer() {
}

void VoterPeer::init(Clock* clock, Log* log) {
    _clock = clock;
    _log = log;
}

void VoterPeer::reset() {
    _badPackets = 0;
    _peerTrusted = false;
    _localChallenge.clear();
    _remoteChallenge.clear();
    memset(&_peerAddr, 0, sizeof(sockaddr_storage));
    for (AudioFrame& f : _frames)
        f.rssi = 0;
    _inSpurt = false;
    _spurtStartUs = 0;
    _audioSeq = 1;
    _frameWrPtr = 0;
    _frameRdPtr = 0;
    _playCursorS = 0;
    _playCursorNs = 0;
}

void VoterPeer::setPeerAddr(const sockaddr_storage& addr) {
    memcpy(&_peerAddr, &addr, sizeof(sockaddr_storage));
}

bool VoterPeer::isInitialChallenge(const uint8_t* packet, unsigned packetLen) {
    if (!isValidPacket(packet, packetLen))
        return false;
    return VoterUtil::getHeaderPayloadType(packet) == 0 &&
        VoterUtil::getHeaderAuthResponse(packet) == 0;
}

int VoterPeer::makeInitialChallengeResponse(const uint8_t* packet, 
    const char* localChallenge, const char* localPassword, uint8_t* resp) {
    // Grab the remote challenge since we'll need it for any responses.
    char remoteChallenge[10];
    if (VoterUtil::getHeaderAuthChallenge(packet, 
        remoteChallenge, sizeof(remoteChallenge)) != 0)
        return -1;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s", remoteChallenge, localPassword);
    uint32_t crc = VoterUtil::crc32(buf);
    VoterUtil::setHeaderPayloadType(resp, 0);
    VoterUtil::setHeaderAuthChallenge(resp, localChallenge);
    VoterUtil::setHeaderAuthResponse(resp, crc);
    return 24;
}

bool VoterPeer::belongsTo(const uint8_t* packet, unsigned packetLen) const {
    if (!isValidPacket(packet, packetLen))
        return false;
    if (VoterUtil::getHeaderAuthResponse(packet) == 0) 
        return false;
    // ### TODO SPEED THIS UP
    char buf[64];
    snprintf(buf, sizeof(buf),"%s%s", _localChallenge.c_str(), _remotePassword.c_str());
    return VoterUtil::crc32(buf) == VoterUtil::getHeaderAuthResponse(packet);
}

bool VoterPeer::isValidPacket(const uint8_t* packet, unsigned packetLen) {
    if ((packetLen < 24) ||
        (VoterUtil::getHeaderPayloadType(packet) == 0 && packetLen != 24) ||
        (VoterUtil::getHeaderPayloadType(packet) == 1 && packetLen != 185) ||
        (VoterUtil::getHeaderPayloadType(packet) == 5 && packetLen != 224)) {
        return false;
    }
    if (VoterUtil::getHeaderPayloadType(packet) != 0 &&
        VoterUtil::getHeaderPayloadType(packet) != 1 &&
        VoterUtil::getHeaderPayloadType(packet) != 2 &&
        VoterUtil::getHeaderPayloadType(packet) != 3 &&
        VoterUtil::getHeaderPayloadType(packet) != 4 &&
        VoterUtil::getHeaderPayloadType(packet) != 5) {
        return false;
    }
    return true;
}

void VoterPeer::consumePacket(const sockaddr& peerAddr, const uint8_t* packet, 
    unsigned packetLen) {

    // Quietly Ignore a few types
    if (VoterUtil::getHeaderPayloadType(packet) == 3 ||
        VoterUtil::getHeaderPayloadType(packet) == 4) {
        _badPackets++;
        return;
    }

    // Look for the transition into trust
    if (!_peerTrusted) {
        _log->info("%s now trusts its peer", _localPassword.c_str());
        _peerTrusted = true;
        // Grab the remote challenge since we'll need it for any responses.
        char remoteChallenge[10];
        if (VoterUtil::getHeaderAuthChallenge(packet, 
            remoteChallenge, sizeof(remoteChallenge)) != 0) {
            _log->infoDump("Bad packet ignored", packet, packetLen);
            _badPackets++;
            return;
        }
        _remoteChallenge = remoteChallenge;
        // Grab the peer address
        memcpy(&_peerAddr, &peerAddr, getIPAddrSize(peerAddr));
    }

    _consumePacketTrusted(packet, packetLen);
}

void VoterPeer::_consumePacketTrusted(const uint8_t* packet, unsigned packetLen) {
    // Audio packet
    if (VoterUtil::getHeaderPayloadType(packet) == 1) {

        // Capture in the circular frame buffer
        _frames[_frameWrPtr].arrivalUs = _clock->timeUs();
        _frames[_frameWrPtr].packetS = VoterUtil::getHeaderTimeS(packet);
        _frames[_frameWrPtr].packetNs= VoterUtil::getHeaderTimeNs(packet);
        _frames[_frameWrPtr].rssi = VoterUtil::getType1RSSI(packet);
        VoterUtil::getType1Audio(packet, _frames[_frameWrPtr].content, 160);
        if (++_frameWrPtr == FRAME_COUNT)
            _frameWrPtr = 0;

        // Look for the leading edge of a spurt
        if (!_inSpurt) {
            _inSpurt = true;
            _spurtStartUs = _clock->timeUs();
            _playCursorS = 0;
            _playCursorNs = VoterUtil::getHeaderTimeNs(packet) - _initialMargin;
            _log->info("Start of TS for %s", _localPassword.c_str());
        }

    } else if (VoterUtil::getHeaderPayloadType(packet) == 5) {
        // Make a pong
        uint8_t resp[224];
        _populateAuth(resp);
        VoterUtil::setHeaderPayloadType(resp, 5);
        // From Docs:
        // Octets 24 and up contain 200 bytes of payload for evaluation of connectivity 
        // quality. When a client receives this packet, it is intended to be transmitted 
        // (with the payload information intact) immediately back to the host from which 
        // it came. The actual contents of the payload are not specifically defined for 
        // the purposes of this protocol, and is entirely determined by the implementation 
        // of the applicable function in the host.
        memcpy(resp + 24, packet + 24, 200);
        _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   
    }
}

void VoterPeer::sendAudio(uint8_t rssi, const uint8_t* frame, unsigned frameLen) {

    assert(frameLen == 160);

    uint8_t resp[24 + 1 + 160];
    _populateAuth(resp);
    VoterUtil::setHeaderPayloadType(resp, 1);
    uint32_t nowS = _clock->timeUs() / 1000000;
    VoterUtil::setHeaderTimeS(resp, nowS);
    VoterUtil::setHeaderTimeNs(resp, _audioSeq);
    // #### TODO: FUNCTIONS FOR THIS
    resp[24] = rssi;
    memcpy(resp + 25, frame, 160);
    _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   

    _audioSeq++;
}

uint8_t VoterPeer::getRSSI(uint64_t ms) {
    _flushExpiredFrames();
    if (_frames[_frameRdPtr].packetNs == _playCursorNs)
        return _frames[_frameRdPtr].rssi;
    else 
        return 0;
}

void VoterPeer::getAudioFrame(uint64_t ms, uint8_t* frame, unsigned frameLen) {    
    _flushExpiredFrames();
    if (_frames[_frameRdPtr].packetNs == _playCursorNs)
        memcpy(frame, _frames[_frameRdPtr].content, 160);
    else 
        memset(frame, 0, 160);
}

void VoterPeer::_flushExpiredFrames() {
    while (true) {
        // Is the current packet empty or in the future? If so, 
        // nothing to flush.
        if (_frames[_frameRdPtr].packetNs == 0 ||
            _frames[_frameRdPtr].packetNs >= _playCursorNs)
            break;
        // Are we on the verge of underflow? If so, nothing to flush
        if (_frameRdPtr + 1 % FRAME_COUNT == _frameWrPtr)
            break;
        // Flush and manage wrap
        if (++_frameRdPtr == FRAME_COUNT)
            _frameRdPtr = 0;
    }
}

string VoterPeer::makeChallenge() {
    // ### TODO RANDOMIZE!
    return string("hello");
}

void VoterPeer::audioRateTick(uint32_t tickTimeMs) {
    _playCursorNs++;
}

void VoterPeer::oneSecTick() {    
    // Check to see if an authentication packet should be sent out
    if (_peerAddr.ss_family != 0 && !_peerTrusted) {
        uint8_t resp[24];
        _localChallenge = makeChallenge();
        VoterUtil::setHeaderPayloadType(resp, 0);
        VoterUtil::setHeaderAuthChallenge(resp, _localChallenge.c_str());
        VoterUtil::setHeaderAuthResponse(resp, 0);
        _log->info("%s initiating handshake", _localPassword.c_str());
        _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));
    }
}

void VoterPeer::tenSecTick() {
    if (_peerTrusted) {
        // Generate a ping
        uint8_t resp[224];
        memset(resp + 24, 0xba, 200);
        _populateAuth(resp);
        VoterUtil::setHeaderPayloadType(resp, 5);
        _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   
    }
}

void VoterPeer::_populateAuth(uint8_t* resp) const {
    char buf[64];
    // When computing authentication response we concatenate the most recent challenge 
    // received from the client with the host's our password. This order is described
    // in the Voter documentation and is confirmed around line 3359 of chan_voter
    // https://github.com/AllStarLink/app_rpt/blob/master/channels/chan_voter.c#L3359
    // #### TODO: SPEED UP
    snprintf(buf, sizeof(buf), "%s%s", _remoteChallenge.c_str(), _localPassword.c_str());
    VoterUtil::setHeaderAuthResponse(resp, VoterUtil::crc32(buf));
    VoterUtil::setHeaderAuthChallenge(resp, _localChallenge.c_str());
}

}
    }
