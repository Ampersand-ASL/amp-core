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

#include "kc1fsz-tools/Log.h"

#include "VoterUtil.h"
#include "VoterPeer.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

VoterPeer::VoterPeer() {
}

VoterPeer::~VoterPeer() {
}

void VoterPeer::reset() {
    _peerTrusted = false;
}

void VoterPeer::consumePacket(const uint8_t* packet, unsigned packetLen) {

    // Ignore spurious packets
    if ((packetLen < 24) ||
        (VoterUtil::getHeaderPayloadType(packet) == 0 && packetLen != 24) ||
        (VoterUtil::getHeaderPayloadType(packet) == 1 && packetLen != 185) ||
        (VoterUtil::getHeaderPayloadType(packet) == 5 && packetLen != 224)) {
        _log->infoDump("Bad packet length ignored", packet, packetLen);
        _badPackets++;
        return;
    }
    if (VoterUtil::getHeaderPayloadType(packet) != 0 &&
        VoterUtil::getHeaderPayloadType(packet) != 1 &&
        VoterUtil::getHeaderPayloadType(packet) != 2 &&
        VoterUtil::getHeaderPayloadType(packet) != 3 &&
        VoterUtil::getHeaderPayloadType(packet) != 4 &&
        VoterUtil::getHeaderPayloadType(packet) != 5) {
        _log->infoDump("Bad packet type ignored", packet, packetLen);
        _badPackets++;
        return;
    }

    // Quietly Ignore a few types
    if (VoterUtil::getHeaderPayloadType(packet) != 3 &&
        VoterUtil::getHeaderPayloadType(packet) != 4) {
        _badPackets++;
        return;
    }

    // Determine if we know this client already or whether a authentication cycle
    // needs to be started.
    if (VoterUtil::getHeaderAuthResponse(packet) != 0) {
        // ### TODO SPEED THIS UP
        char buf[64];
        snprintf(buf, sizeof(buf),"%s%s", _localChallenge.c_str(), _remotePassword.c_str());
        if (VoterUtil::crc32(buf) == VoterUtil::getHeaderAuthResponse(packet)) {
            _peerTrusted = true;
        } else {
            _log->info("Authentication failed.");
            _localChallenge.clear();
            _remoteChallenge.clear();
            _peerTrusted = false;
            return;
        }
    }
    // If there is no authentication response then use this to start a 
    // new authentication cycle.
    /* From docs:       
       When the client starts operating, it starts periodically sending VOTER packets 
       to the host containing a payload value of 0, the client's challenge string and 
       an authentication response (digest) of 0 (to indicate that it has not as of yet 
       gotten a valid response from the host). When such a packet is received by the host, 
       it responds with a packet containing a payload value of 0, the host's challenge string, 
       an authentication response (digest) based upon the calculated CRC-32 value of the 
       challenge string it received from the client and the host's password, and option flags.    
    */
    else {
        char remoteChallenge[10];
        if (VoterUtil::getHeaderAuthChallenge(packet, 
            remoteChallenge, sizeof(remoteChallenge)) != 0) {
            _log->infoDump("Bad packet ignored", packet, packetLen);
            _badPackets++;
            return;
        }

        // Setup the information that wil be used on the next transmission
        _localChallenge = _makeChallenge();
        _remoteChallenge = remoteChallenge;
    }

    if (_peerTrusted) {
        _consumePacketTrusted(packet, packetLen);
    }
}

void VoterPeer::_consumePacketTrusted(const uint8_t* packet, unsigned packetLen) {
}

void VoterPeer::sendAudio(uint64_t ms, const uint8_t* frame, unsigned frameLen) {

    /* From docs:
        If the client approves of the host's response, it may then start sending packets 
        with a payload type of 1 or 3, the client's challenge string, and an 
        authentication response (digest) based upon the calculated CRC-32 value of the 
        challenge string it received from the host and the client's password, 
    */


    // When computing authentication response we concatenate the most recent challenge 
    // received from the client with the host's our password. This order is described
    // in the Voter documentation and is confirmed around line 3359 of chan_voter
    // https://github.com/AllStarLink/app_rpt/blob/master/channels/chan_voter.c#L3359
    char buf[64];
    snprintf(buf, sizeof(buf),"%s%s", _remoteChallenge.c_str(), _localPassword.c_str());
    uint32_t crc = VoterUtil::crc32(buf);

    uint8_t resp[24];
    VoterUtil::setHeaderPayloadType(resp, 0);
    VoterUtil::setHeaderAuthChallenge(resp, _localChallenge.c_str());
    VoterUtil::setHeaderAuthResponse(resp, crc);

    _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));
}

string VoterPeer::_makeChallenge() {
    // ### TODO RANDOMIZE!
    return string("hello");
}

void VoterPeer::oneSecTick() {    
}

}
    }
