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
#include <cstring>
#include <cassert>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/NetUtils.h"

#include "VoterUtil.h"
#include "VoterPeer.h"

#define FRAME_SIZE (160)
#define HEADER_SIZE (24)
#define PING_PAYLOAD_SIZE (164)

// How long we can go without hearing from the other side before
// giving up and resetting.
#define TIMEOUT_INTERVAL_MS (15 * 1000)
// Period of no audio packets before declaring the spurt to be over
#define SPURT_TIMEOUT_MS (80)

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
VoterPeer::VoterPeer(bool isClient)
:   _isClient(isClient),
    _framePtrs(FRAME_COUNT) {
    _localPassword[0] = 0;
    _localChallenge[0] = 0;
    _remotePassword[0] = 0;
    _remoteChallenge[0] = 0;
    reset();
}

VoterPeer::~VoterPeer() {
}

void VoterPeer::init(Clock* clock, Log* log) {
    _clock = clock;
    _log = log;
}

void VoterPeer::reset() {
    _masterTimingSource = false;
    _generalPurposeMode = false;
    _audioTransmit = false;
    _badPackets = 0;
    for (AudioFrame& f : _frames)
        f.rssi = 0;
    _inSpurt = false;
    _spurtStartMs = 0;
    // We start ahead so that the other side has some room to back up
    _audioSeq = 100;
    _playCursorS = 0;
    _playCursorNs = 0;
    _lastRxMs = 0;
    _peerTrusted = false;
    _remoteChallenge[0] = 0;
    _framePtrs.reset();
    _audioAvailableThisTick = false;
    // Only clear out the peer address if we are a server
    if (!_isClient)
        memset(&_peerAddr, 0, sizeof(sockaddr_storage));
}

void VoterPeer::setLocalChallenge(const char* p) { 
    strcpyLimited(_localChallenge, p, sizeof(_localChallenge));
}

void VoterPeer::setLocalPassword(const char* p) { 
    strcpyLimited(_localPassword, p, sizeof(_localPassword));
}

void VoterPeer::setRemoteChallenge(const char* p) { 
    strcpyLimited(_remoteChallenge, p, sizeof(_remoteChallenge));
}

void VoterPeer::setRemotePassword(const char* p) { 
    strcpyLimited(_remotePassword, p, sizeof(_remotePassword));
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

int VoterPeer::makeInitialChallengeResponse(Clock* clock, const uint8_t* packet, 
    bool isClient, bool isGeneralPurposeMode, 
    const char* localChallenge, const char* localPassword, uint8_t* resp) {
    // Grab the remote challenge since we'll need it for any responses.
    char remoteChallenge[10];
    if (VoterUtil::getHeaderAuthChallenge(packet, 
        remoteChallenge, sizeof(remoteChallenge)) != 0)
        return -1;
    memset(resp, 0, HEADER_SIZE + 1);
    VoterUtil::setHeaderPayloadType(resp, 0);
    VoterUtil::setHeaderAuthChallenge(resp, localChallenge);
    uint32_t crc = VoterUtil::crc32(remoteChallenge, localPassword);
    VoterUtil::setHeaderAuthResponse(resp, crc);
    VoterUtil::setHeaderTimeS(resp, clock->timeMs() / 1000);
    uint8_t flags = 0;
    if (isClient && isGeneralPurposeMode)
        flags |= 32;
    VoterUtil::setType0Flags(resp, flags);
    return HEADER_SIZE + 1;
}

bool VoterPeer::isValidPacket(const uint8_t* packet, unsigned packetLen) {
    if ((packetLen < HEADER_SIZE) ||
        (VoterUtil::getHeaderPayloadType(packet) == 0 && packetLen != 25) ||
        (VoterUtil::getHeaderPayloadType(packet) == 1 && packetLen < HEADER_SIZE + 1 + FRAME_SIZE)) {
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

bool VoterPeer::belongsTo(const sockaddr& potentialPeerAddr, const uint8_t* packet, unsigned packetLen) const {
    if (!isValidPacket(packet, packetLen))
        return false;
    if (VoterUtil::getHeaderAuthResponse(packet) == 0)
        return false;
    // Once a peer is trusted then the IP address/port must match to prevent 
    // hijacking. Otherwise, any address that knows the password is fine.
    if (_peerTrusted && 
        !equalIPAddrAndPort(potentialPeerAddr, (const sockaddr&)_peerAddr)) {
        return false;
    }
    // If we get to this point then it comes down to password validation
    return VoterUtil::crc32(_localChallenge, _remotePassword) == 
        VoterUtil::getHeaderAuthResponse(packet);
}

void VoterPeer::consumePacket(const sockaddr& peerAddr, const uint8_t* packet, 
    unsigned packetLen) {

    // Record time so we can manage timeouts
    _lastRxMs = _clock->timeMs();

    // Lock in their reply address if it has changed
    if (!equalIPAddr(peerAddr, (const sockaddr&)_peerAddr)) {
        char addr[64];
        formatIPAddrAndPort(peerAddr, addr, sizeof(addr));
        _log->info("Peer %s has new address %s", _remotePassword, addr);
        memcpy(&_peerAddr, &peerAddr, getIPAddrSize(peerAddr));
    }

    // Quietly Ignore a few types
    if (VoterUtil::getHeaderPayloadType(packet) == 3 ||
        VoterUtil::getHeaderPayloadType(packet) == 4) {
        _badPackets++;
        return;
    }

    // Look for the transition into trust
    if (!_peerTrusted) {

         _peerTrusted = true;

        char addr[64];
        formatIPAddrAndPort(peerAddr, addr, sizeof(addr));
        _log->info("VOTER %s now trusts its peer %s (%s)", _localPassword,
            _remotePassword, addr);

        // Grab the remote challenge since we'll need it for any responses.
        char remoteChallenge[10];
        if (VoterUtil::getHeaderAuthChallenge(packet, 
            remoteChallenge, sizeof(remoteChallenge)) != 0) {
            _log->infoDump("Bad VOTER packet ignored", packet, packetLen);
            _badPackets++;
            return;
        }

        strcpyLimited(_remoteChallenge, remoteChallenge, sizeof(remoteChallenge));

        // Send one more type 0 packet in case it is needed to establish
        // trust in the other direction.
        uint8_t resp[HEADER_SIZE + 1] = { 0 };
        _populateHeader(0, resp);
        _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));
    }

    _consumePacketTrusted(packet, packetLen);
}

void VoterPeer::_consumePacketTrusted(const uint8_t* packet, unsigned packetLen) {

    // Pull out flags
    if (VoterUtil::getHeaderPayloadType(packet) == 0) {
        if (!_isClient) {
            if (VoterUtil::getType0Flags(packet) & 32) {
                if (!_generalPurposeMode) {
                    _log->info("Peer %s entering general purpose mode", _remotePassword);
                    _generalPurposeMode = true;
                }
            }
            else {
                if (_generalPurposeMode) {
                    _log->info("Peer %s leaving general purpose mode", _remotePassword);
                    _generalPurposeMode = false;
                }
            }
        }
    }

    // Audio packet
    if (VoterUtil::getHeaderPayloadType(packet) == 1) {

        _lastAudioMs = _clock->timeMs();

        // Capture in the circular frame buffer, if possible
        if (!_framePtrs.isFull()) {

            // Look for the leading edge of a spurt and use it to synchronize playout
            if (!_inSpurt) {
                _inSpurt = true;
                _spurtStartMs = _clock->timeMs();
                if (_generalPurposeMode) {
                    _playCursorS = 0;
                    _playCursorNs = VoterUtil::getHeaderTimeNs(packet) - _initialMarginGP;
                }
                else {
                    _playCursorS = VoterUtil::getHeaderTimeS(packet);
                    uint32_t ns = VoterUtil::getHeaderTimeNs(packet);
                    if (ns < _initialMarginGPS) {
                        _playCursorS -= 1;
                        ns = _initialMarginGPS - ns;                        
                        _playCursorNs = 1000000000 - ns;
                    }
                    else {
                        _playCursorNs = ns - _initialMarginGPS;
                    }
                }
                _log->info("VOTER start of TS from %s", _localPassword);
            }

            //_log->info("In TS %u %u", _framePtrs.getDepth(), VoterUtil::getHeaderTimeNs(packet));

            // Save the frame
            const unsigned ptr = _framePtrs.writePtrThenPush();
            _frames[ptr].arrivalUs = _clock->timeUs();
            _frames[ptr].packetS = VoterUtil::getHeaderTimeS(packet);
            _frames[ptr].packetNs = VoterUtil::getHeaderTimeNs(packet);
            _frames[ptr].rssi = VoterUtil::getType1RSSI(packet);
            VoterUtil::getType1Audio(packet, _frames[ptr].content, FRAME_SIZE);
        }
        else {
            // #### TODO: OVERFLOW COUNTER
            uint32_t packetS = VoterUtil::getHeaderTimeS(packet);
            uint32_t packetNs = VoterUtil::getHeaderTimeNs(packet);
            _log->info("VOTER buffer full %u %u", packetS, packetNs);
            for (unsigned i = 0; i < FRAME_COUNT; i++)
                _log->info("  Frame %u -> %u %u", i, _frames[i].packetS, _frames[i].packetNs);
        }
    } 
    // Ping packet
    else if (VoterUtil::getHeaderPayloadType(packet) == 5) {
        if (_isClient) {
            // The pinger gets to decide how long the payload is, as long as
            // it doesn't exceed the maximum that we are allowed to send back.
            const unsigned pingPayloadLen = min(packetLen - HEADER_SIZE,
                (unsigned)PING_PAYLOAD_SIZE);

            // Make a pong
            uint8_t resp[HEADER_SIZE + PING_PAYLOAD_SIZE] = { 0 };
            _populateHeader(5, resp);

            // IMPORTANT NOTE: After review of the VOTER source code, I think there
            // is a mistake in the VOTER protocol documentation. The original docs read:
            // 
            // **Payload type 5 - "PING" (Connectivity Test)**
            // "Octets 24 and up contain 200 bytes of payload for evaluation of connectivity 
            // quality. When a client receives this packet, it is intended to be transmitted 
            // (with the payload information intact) immediately back to the host from which 
            // it came. The actual contents of the payload are not specifically defined for 
            // the purposes of this protocol, and is entirely determined by the implementation 
            // of the applicable function in the host.""
            //
            // However, the structure allocated to read the packet only has room for 164 bytes
            // of payload. When sending more than 164 you start to overwrite other memory.

            memcpy(resp + HEADER_SIZE, packet + HEADER_SIZE, pingPayloadLen);
            _sendCb((const sockaddr&)_peerAddr, resp, HEADER_SIZE + pingPayloadLen);   
        }
    }
}

void VoterPeer::sendAudio(uint8_t rssi, const uint8_t* frame, unsigned frameLen,
    uint64_t originMs) {

    assert(frameLen == FRAME_SIZE);

    uint8_t resp[HEADER_SIZE + 1 + FRAME_SIZE] = { 0 };
    _populateHeader(1, resp);

    // NOTE: Time is being looked at

    uint32_t nowS = _clock->timeUs() / 1000000;
    VoterUtil::setHeaderTimeS(resp, nowS);

    if (_generalPurposeMode) {
        VoterUtil::setHeaderTimeNs(resp, _audioSeq);
    } 
    else {
        uint32_t nowUs = _clock->timeUs() % 1000000;
        // Round to 20ms boundary
        uint32_t nowMs = nowUs / 1000;
        uint32_t nowMsRounded = (nowMs / 20) * 20;
        // Change from ms -> ns
        uint32_t nowNs = nowMsRounded * 1000000;
        VoterUtil::setHeaderTimeNs(resp, nowNs);
    }

    // #### TODO: FUNCTIONS FOR THIS
    resp[HEADER_SIZE] = rssi;
    memcpy(resp + HEADER_SIZE + 1, frame, FRAME_SIZE);
    _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   

    _audioSeq++;
}

void VoterPeer::audioRateTick(uint32_t tickTimeMs) {

    // The play cursor moves forward no matter what
    if (_generalPurposeMode) 
        _playCursorNs++;
    else {
        _playCursorNs += 20000000;
        // Pay attention to the wrap at 1 billion
        if (_playCursorNs == 1000000000) {
            _playCursorS++;
            _playCursorNs = 0;
        }
    }

    // Figure out if we have audio available on this tick

    _audioAvailableThisTick = false;

    if (!_framePtrs.isEmpty()) {

        // Clean up all expired frames
        while (!_framePtrs.isEmpty() &&
            _frames[_framePtrs.readPtr()].isExpired(
                _generalPurposeMode, _playCursorS, _playCursorNs)) {
            _framePtrs.pop();
        }

        // Is the next frame playable now? (If not, the only other 
        // possibility is that it is waiting to be played in the future.)
        _audioAvailableThisTick = !_framePtrs.isEmpty() &&
            _frames[_framePtrs.readPtr()].isCurrent(
                _generalPurposeMode, _playCursorS, _playCursorNs);
    }

    // Is the spurt timed out?
    if (_inSpurt && _clock->isPastWindow(_lastAudioMs, SPURT_TIMEOUT_MS)) {
        _inSpurt = false;
        _spurtStartMs = 0;
        _log->info("VOTER end of TS for %s", _localPassword);
    }
}

bool VoterPeer::isAudioAvailable() const { 
    return _audioAvailableThisTick; 
}

uint8_t VoterPeer::getRSSI(uint64_t ms) {
    if (isAudioAvailable())
        return _frames[_framePtrs.readPtr()].rssi;
    else 
        return 0;
}

void VoterPeer::getAudioFrame(uint64_t ms, uint8_t* frame, unsigned frameLen) {    
    if (isAudioAvailable())
        memcpy(frame, _frames[_framePtrs.readPtr()].content, FRAME_SIZE);
    else 
        memset(frame, 0, 160);
}

void VoterPeer::popAudioFrame() {
    if (!_framePtrs.isEmpty())
        _framePtrs.pop();
}

string VoterPeer::makeChallenge() {
    char ch[10];
    long randomNum = rand();
    snprintf(ch, sizeof(ch), "%09lu", randomNum);
    return string(ch);
    //return string("123456789");
}

void VoterPeer::oneSecTick() {    

    // Check to see if an authentication packet should be sent out
    if (_peerAddr.ss_family != 0 && !_peerTrusted && 
        _localChallenge[0] && _localPassword[0]) {
        uint8_t resp[HEADER_SIZE + 1] = { 0 };
        VoterUtil::setHeaderPayloadType(resp, 0);
        VoterUtil::setHeaderAuthChallenge(resp, _localChallenge);
        VoterUtil::setHeaderAuthResponse(resp, 0);
        VoterUtil::setHeaderTimeS(resp, _clock->timeMs() / 1000);
        uint8_t flags = 0;
        if (_isClient && _generalPurposeMode)
            flags |= 32;
        VoterUtil::setType0Flags(resp, flags);
        _log->info("VOTER %s initiating handshake", _localPassword);
        _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));
    }

    if (++_oneTickCounter % 2 == 0 && _peerTrusted) {

        // Generate a ping (only the server does this)
        if (!_isClient) {
            // IMPORTANT NOTE: After review of the VOTER source code, I think there
            // is a mistake in the VOTER protocol documentation. The original docs read:
            // 
            // **Payload type 5 - "PING" (Connectivity Test)**
            // "Octets 24 and up contain 200 bytes of payload for evaluation of connectivity 
            // quality. When a client receives this packet, it is intended to be transmitted 
            // (with the payload information intact) immediately back to the host from which 
            // it came. The actual contents of the payload are not specifically defined for 
            // the purposes of this protocol, and is entirely determined by the implementation 
            // of the applicable function in the host.""
            //
            // However, the structure allocated to read the packet only has room for 164 bytes
            // of payload. When sending more than 164 you start to overwrite other memory
            // in the device.
            uint8_t resp[HEADER_SIZE + PING_PAYLOAD_SIZE] = { 0 };
            // #### TODO: Put a sequence number and send time into the payload.
            _populateHeader(5, resp);
            _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   
        }
        // Generate GPS
        else {
            uint8_t resp[HEADER_SIZE + 26] = { 0 };
            // Long (9 including null)
            strcpy((char*)resp + 24, "4231.36N");
            // Lat (10 including null)
            strcpy((char*)resp + 33, "07127.45W");
            // Evel (7 including null)
            strcpy((char*)resp + 33, "40");
            _populateHeader(2, resp);
            _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));   
        }
    }
}

void VoterPeer::tenSecTick() {
    // Check for timeout
    if (_peerTrusted && _clock->isPastWindow(_lastRxMs, TIMEOUT_INTERVAL_MS)) {
        _log->info("VOTER timing out connection with %s", _remotePassword);
        reset();        
    }
}

void VoterPeer::_populateHeader(uint16_t type, uint8_t* resp) const {
    VoterUtil::setHeaderPayloadType(resp, type);
    VoterUtil::setHeaderTimeS(resp, _clock->timeMs() / 1000);
    char buf[64];
    // When computing authentication response we concatenate the most recent challenge 
    // received from the client with the host's our password. This order is described
    // in the Voter documentation and is confirmed around line 3359 of chan_voter
    // https://github.com/AllStarLink/app_rpt/blob/master/channels/chan_voter.c#L3359
    // #### TODO: SPEED UP
    snprintf(buf, sizeof(buf), "%s%s", _remoteChallenge, _localPassword);
    VoterUtil::setHeaderAuthResponse(resp, VoterUtil::crc32(buf));
    VoterUtil::setHeaderAuthChallenge(resp, _localChallenge);
}

}
    }
