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
#include "VoterClient.h"

using namespace std;

namespace kc1fsz {

    namespace amp {

void VoterClient::reset() {
    _authState = 0;
}


void VoterClient::consumePacket(const uint8_t* packet, unsigned packetLen) {

    // Ignore spurious packets
    if (packetLen < 24) {
        _log->infoDump("Bad packet ignored", packet, packetLen);
        _badPackets++;
        return;
    }

    if (VoterUtil::getHeaderPayloadType(packet) == 0) {
        /* From docs:       
        When the client starts operating, it starts periodically sending VOTER packets 
        to the host containing a payload value of 0, the client's challenge string and 
        an authentication response (digest) of 0 (to indicate that it has not as of yet 
        gotten a valid response from the host). When such a packet is received by the host, 
        it responds with a packet containing a payload value of 0, the host's challenge string, 
        an authentication response (digest) based upon the calculated CRC-32 value of the 
        challenge string it received from the client and the host's password, and option flags.    
        */
        if (VoterUtil::getHeaderAuthResponse(packet) == 0) {

            _authState = 0;

            // Pull out the client's challenge string
            char clientChallenge[10];
            if (VoterUtil::getHeaderAuthChallenge(packet, 
                clientChallenge, sizeof(clientChallenge)) != 0) {
                _log->infoDump("Bad packet ignored", packet, packetLen);
                _badPackets++;
                return;
            }

            char buf[64];
            snprintf(buf, sizeof(buf),"%s%s", _hostPassword.c_str(), clientChallenge);
            uint32_t crc = VoterUtil::crc32(buf);
            _hostChallenge = _makeChallenge();

            uint8_t resp[24];
            VoterUtil::setHeaderPayloadType(resp, 0);
            VoterUtil::setHeaderAuthChallenge(resp, _hostChallenge.c_str());
            VoterUtil::setHeaderAuthResponse(resp, crc);

            _sendCb((const sockaddr&)_peerAddr, resp, sizeof(resp));

            _authState = 1;
        }
        else {
            if (_authState == 0) {
                _log->infoDump("Unauthorized packet ignored", packet, packetLen);
                _badPackets++;
                return;
            }
            /* From docs:
               If the client approves of the host's response, it may then start sending packets 
               with a payload type of 1 or 3, the client's challenge string, and an 
               authentication response (digest) based upon the calculated CRC-32 value of the 
               challenge string it received from the host and the client's password, 
            */
            // Is the first time through a new authentication?
            else if (_authState == 1) {
                char buf[64];
                snprintf(buf, sizeof(buf),"%s%s", _hostChallenge.c_str(), _clientPassword.c_str());
                uint32_t crc = VoterUtil::crc32(buf);
                if (crc == VoterUtil::getHeaderAuthResponse(packet)) {
                    _log->info("Authentication succeeded.");
                    _authState = 2;
                } else {
                    _log->info("Authentication failed.");
                    _authState = 0;
                }
            }
        }
    }
}

string VoterClient::_makeChallenge() {
    return string("hello");
}


}
    }
