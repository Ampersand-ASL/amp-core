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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/sockios.h> // Required for SIOCOUTQ
#endif

#include <sys/types.h>

#include <cstring>
#include <iostream>
#include <algorithm>

#include "ed25519.h"

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/MicroDNS.h"
#include "kc1fsz-tools/md5/md5.h"

#include "LineIAX2.h"
#include "IAX2FrameFull.h"
#include "IAX2Util.h"
#include "MessageConsumer.h"
#include "Message.h"

using namespace std;

#define MAX_TEXT_MESSAGE_SIZE (160 * 6 * 2)

static unsigned AUDIO_TICK_MS = 20;
static const uint32_t NORMAL_PING_INTERVAL_MS = 10 * 1000;
static const uint32_t FAST_PING_INTERVAL_MS = 2 * 1000;
// #### TODO: CLEAN UP
// How long can the call go without receiving any messages before 
// a hangup is generated.
static const uint32_t _inactivityTimeoutMs = 40 * 1000;
// How long we will wait around before cleaning up a terminated call. 
// This window is used to allow time to re-transmit and unacknowledged messages.
static const uint32_t TERMINATION_TIMEOUT_MS = 5 * 1000;
// How long we wait for a callee to respond to our NEW
#define CALL_INITIATION_TIMEOUS_MS (2000)

// #### TODO: CONFIGURATION
static const char* DNS_IP_ADDR = "208.67.222.222";

// The size of the IAX2 socket transmit buffer. This is adjusted
// upwards to help with the large transmission case.
#define IAX_SOCKET_SNDBUF_BYTES (512 * 1024)

namespace kc1fsz {

static uint32_t alignToTick(uint32_t ts, uint32_t tick) {
    return (ts / tick) * tick;
}

LineIAX2::LineIAX2(Log& log, Log& traceLog, Clock& clock, int busId,
    MessageConsumer& bus, NumberAuthorizer* destAuth, NumberAuthorizer* sourceAuth,
    LocalRegistry* locReg, unsigned destLineId, const char* publicUser,
    LineIAX2::Call* callSpace, unsigned callSpaceLen)
:   _log(log),
    _traceLog(traceLog),
    _clock(clock),
    _busId(busId),
    _bus(bus),
    _destAuthorizer(destAuth),
    _sourceAuthorizer(sourceAuth),
    _locReg(locReg),
    _destLineId(destLineId),
    _publicUser(publicUser),
    _startTime(clock.time()),
    _calls(callSpace),
    _maxCalls(callSpaceLen) {
    // One-time initialization of calls
    for (unsigned i = 0; i < callSpaceLen; i++)
        _calls[i].init(this);
    _privateKeyHex[0] = 0;
    _pokeAddr[0] = 0;
    _pokeNodeNumber[0] = 0;
    strcpyLimited(_dnsRoot, "allstarlink.org", sizeof(_dnsRoot));
}

void LineIAX2::setDNSRoot(const char* dnsRoot) {
    if (dnsRoot)
        strcpyLimited(_dnsRoot, dnsRoot, sizeof(_dnsRoot));
}

void LineIAX2::setPokeAddr(const char* addrAndPort) {
    if (addrAndPort)
        strcpyLimited(_pokeAddr, addrAndPort, sizeof(_pokeAddr));
    else 
        _pokeAddr[0] = 0;
}

void LineIAX2::setPokeNodeNumber(const char* nodeNumber) {
    if (nodeNumber)
        strcpyLimited(_pokeNodeNumber, nodeNumber, sizeof(_pokeNodeNumber));
    else 
        _pokeNodeNumber[0] = 0;
}

void LineIAX2::setPrivateKey(const char* privateKeyHex) {
    if (privateKeyHex)
        strcpyLimited(_privateKeyHex, privateKeyHex, sizeof(_privateKeyHex));
}

void LineIAX2::setAuthMode(AuthMode mode) {
    if (mode == AuthMode::OPEN) {
        _sourceIpValidationRequired = false;
        _authorizeWithCalltoken = false;
        _authorizeWithAuthreq = false;
    }
    else if (mode == AuthMode::SOURCE_IP) {
        _sourceIpValidationRequired = true;
        _authorizeWithCalltoken = true;
        _authorizeWithAuthreq = false;
        
    } else if (mode == AuthMode::CHALLENGE_ED25519) {
        _sourceIpValidationRequired = false;
        _authorizeWithCalltoken = false;
        _authorizeWithAuthreq = true;
    } else {
        assert(false);
    }
}

int LineIAX2::open(short addrFamily, int listenPort) {

    // If the configuration is changing then ignore the request
    if (addrFamily == _addrFamily &&
        _iaxListenPort == listenPort &&
        _iaxSockFd != 0 &&
        _dnsSockFd != 0) {
        return 0;
    }

    close();

    _addrFamily = addrFamily;
    _iaxListenPort = listenPort;

    _log.info("Listening on IAX port %d", _iaxListenPort);

    // UDP open/bind
    int iaxSockFd = socket(_addrFamily, SOCK_DGRAM, 0);
    if (iaxSockFd < 0) {
        _log.error("Unable to open IAX port (%d)", errno);
        return -1;
    }    

    // #### TODO RAAI TO ADDRESS LEAKS BELOW
    
    int optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(iaxSockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("IAX setsockopt SO_REUSEADDR failed (%d)", errno);
        ::close(iaxSockFd);
        return -1;
    }

    optval = IAX_SOCKET_SNDBUF_BYTES;
    if (setsockopt(iaxSockFd, SOL_SOCKET, SO_SNDBUF, (const char*)&optval, sizeof(optval)) == -1) {
        _log.error("Failed to adjust socket send buffer");
    }

    // Get the current send buffer size
    {
        int bufferSize = 0;
        socklen_t optlen = sizeof(bufferSize);
        if (getsockopt(iaxSockFd, SOL_SOCKET, SO_SNDBUF, &bufferSize, &optlen) < 0) {
            _log.error("Unable to get TX buffer size");
        } else {
            _txSocketBufferSize = bufferSize;
        }
    }

    struct sockaddr_storage servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.ss_family = _addrFamily;
    
    if (_addrFamily == AF_INET) {
        // Bind to all interfaces
        // Or, specify a particular IP address: servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        // TODO: Allow this to bec controlled
        ((sockaddr_in&)servaddr).sin_addr.s_addr = INADDR_ANY;
        ((sockaddr_in&)servaddr).sin_port = htons(_iaxListenPort);
    } else if (_addrFamily == AF_INET6) {
        // To bind to all available IPv6 interfaces
        // TODO: Allow this to bec controlled
        ((sockaddr_in6&)servaddr).sin6_addr = in6addr_any; 
        ((sockaddr_in6&)servaddr).sin6_port = htons(_iaxListenPort);
    } else {
        assert(false);
    }

    if (::bind(iaxSockFd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        _log.error("Unable to bind to IAX port (%d)", errno);
        ::close(iaxSockFd);
        return -1;
    }

    if (makeNonBlocking(iaxSockFd) != 0) {
        _log.error("open fcntl failed (%d)", errno);
        ::close(iaxSockFd);
        return -1;
    }

    // Setup a port for DNS activity
    int dnsSockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (dnsSockFd < 0) {
        ::close(iaxSockFd);
        return -1;
    }

    optval = 1; 
    // This allows the socket to bind to a port that is in TIME_WAIT state,
    // or allows multiple sockets to bind to the same port (useful for multicast).
    if (setsockopt(dnsSockFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval)) < 0) {
        _log.error("DNS setsockopt SO_REUSEADDR failed (%d)", errno);
        return -1;
    }

    struct sockaddr_in servaddr4;
    memset(&servaddr4, 0, sizeof(servaddr4));
    servaddr4.sin_family = AF_INET;
    // Bind to all interfaces
    servaddr4.sin_addr.s_addr = INADDR_ANY;
    // Bind to any port
    servaddr4.sin_port = htons(0);
    if (::bind(dnsSockFd, (const struct sockaddr*)&servaddr4, sizeof(servaddr4)) < 0) {
        _log.error("Failed to bind to DNS port");
        ::close(iaxSockFd);
        ::close(dnsSockFd);
        return -1;
    }
    // Make non-blocking
    if (makeNonBlocking(dnsSockFd) != 0) {
        _log.error("Failed to make DNS socket non-blocking %d", errno);
        ::close(iaxSockFd);
        ::close(dnsSockFd);
        return -1;
    }

    _iaxSockFd = iaxSockFd;
    _dnsSockFd = dnsSockFd;

    return 0;
}

void LineIAX2::close() {   

    // Clean up all of the calls
    for (unsigned i = 0; i < _maxCalls; i++)
        _calls[i].reset();

    if (_iaxSockFd) 
        ::close(_iaxSockFd);
    if (_dnsSockFd) 
        ::close(_dnsSockFd);
    _iaxSockFd = 0;
    _dnsSockFd = 0;
    _iaxListenPort = 0;
    _addrFamily = 0;
} 

// TEST: radio@52.8.197.124:4569/61057,NONE
int LineIAX2::call(const char* localNumber, const char* targetNode,
    CODECType desiredCodec) {  

    // It's possible that the node has been specified in explicit 
    // format: radio@127.0.0.1:4569/1951,NONE. If so, parse out the other
    // pieces so 
    fixedstring targetUser;
    fixedstring targetNumber = targetNode;
    fixedstring targetAddrAndPort;
    fixedstring targetPassword;
    bool targetExplicit = false;

    // Look for the case where explicit target information was provided.
    const char* slash = strchr(targetNode, '/');
    if (slash != 0) {
        targetExplicit = true;
        int state = 0;
        targetUser.clear();
        targetNumber.clear();
        for (unsigned i = 0; i < strlen(targetNode); i++) {
            char c = targetNode[i];
            if (state == 0) {
                if (c == '@') {
                    state = 1;
                } else {
                    targetUser.append(c);
                }
            } else if (state == 1) {
                if (c == '/') {
                    state = 2;
                } else {
                    targetAddrAndPort.append(c);
                }
            } else if (state == 2) {
                if (c == ',') {
                    state = 3;
                } else {   
                    targetNumber.append(c);
                }
            } else if (state == 3) {
                targetPassword.append(c);
            }
        }
        if (targetUser.empty() || targetAddrAndPort.empty() ||
            targetNumber.empty() || targetPassword.empty())
            return -4;
    }

    _log.info("Request to call %s -> %s", localNumber, targetNode);

    // Make sure we don't have an active call already to the same target number.
    bool found = false;

    _visitActiveCallsIf(
        // Visitor
        [&found, localNumber, &targetNumber, &log = _log](const Call& call) {
            log.info("%s -> %s already linked in call %u", localNumber, 
                targetNumber.c_str(), call.localCallId);
            found = true;
        },
        // Predicate
        [&localNumber, &targetNumber](const Call& call) {
            return 
              call.remoteNumber == targetNumber && 
              call.state != Call::State::STATE_TERMINATE_WAITING && 
              call.state != Call::State::STATE_TERMINATED;
        }
    );

    if (found)
        return -1;

    // Allocate a call
    int callIx = _allocateCallIx();
    if (callIx == -1) 
        return -2; 
    Call& call = _calls[callIx];

    call.reset();
    call.localNumber = localNumber;
    call.remoteNumber = targetNumber;
    call.desiredCodec = desiredCodec;
    call.side = Call::Side::SIDE_CALLER;
    // Move back a few ms to make sure that the elapsed time is always positive
    // in some of the dispense operations below.
    call.localStartMs = _clock.time() - AUDIO_TICK_MS;
    call.lastLagrqMs = _clock.time();
    call.lastFrameRxMs = _clock.time();

    // If an explicit address was specified 
    if (targetExplicit) {
        struct sockaddr_storage targetAddr;
        memset(&targetAddr, 0, sizeof(sockaddr_storage));
        if (parseIPAddrAndPort(targetAddrAndPort.c_str(), targetAddr) != 0) {
            return -5;
        }
        memcpy(&call.peerAddr, &targetAddr, getIPAddrSize((const sockaddr&)targetAddr));
        call.callUser = targetUser;
        call.callPassword = targetPassword;
        call.state = Call::State::STATE_INITIATION_WAIT;
        call.active = true;
    }
    // Check the local registration (if available) to see if we can resolve the target 
    // without going out to the DNS/directory.
    else if (_locReg) {
        struct sockaddr_storage targetAddr;
        memset(&targetAddr, 0, sizeof(sockaddr_storage));
        if (_locReg->lookup(call.remoteNumber.c_str(), targetAddr, targetUser, targetPassword)) { 
            char addr[64];
            formatIPAddrAndPort((const sockaddr&)targetAddr, addr, 64);
            _log.info("Resolved %s locally -> %s", call.remoteNumber.c_str(), addr);
            memcpy(&call.peerAddr, &targetAddr, getIPAddrSize((const sockaddr&)targetAddr));
            call.callUser = targetUser;
            call.callPassword = targetPassword;
            call.state = Call::State::STATE_INITIATION_WAIT;
            call.active = true;
        }
        // If there's nothing in the local registry then assume this is a public
        // node and trigger a DNS.
        else {
            call.callUser = _publicUser;
            call.state = Call::State::STATE_LOOKUP_0;
            call.active = true;
        }
    }
    // Otherwise, we in a state that will trigger a DNS lookup
    else {
        call.callUser = _publicUser;
        call.state = Call::State::STATE_LOOKUP_0;
        call.active = true;
    }

    // NOTICE: Be careful, at this point a call has been allocated so if
    // **anything** goes wrong it needs to be cleaned up to avoid a call leak!

    return 0;
}

unsigned LineIAX2::_dropIf(std::function<bool(const Call& call)> pred) {
    unsigned count = 0;
    _visitActiveCallsIf(
        // Visitor
        [this, &count](Call& call) {
            _log.info("Hanging up call %u to node %s", 
                call.localCallId, call.remoteNumber.c_str());
            _hangupCall(call);
            count++;
        },
        pred
    );
    return count;
}

int LineIAX2::drop(const char* localNumber, const char* targetNumber) {

    _log.info("Request to drop %s -> %s", localNumber, targetNumber);

    unsigned count = _dropIf(
        [localNumber, targetNumber](const Call& call) {
            return call.remoteNumber == targetNumber && 
              (strcmp("*", localNumber) == 0 || call.localNumber == localNumber) && 
              call.state != Call::State::STATE_TERMINATE_WAITING && 
              call.state != Call::State::STATE_TERMINATED;
        }
    );
    return count > 0 ? 0 : -1;
}

int LineIAX2::dropCall(unsigned callId) {

    _log.info("Request to drop call %u", callId);

    unsigned count = _dropIf(
        [callId](const Call& call) {
            return call.localCallId == callId && 
              call.state != Call::State::STATE_TERMINATE_WAITING && 
              call.state != Call::State::STATE_TERMINATED;
        }
    );
    return count > 0 ? 0 : -1;
}

void LineIAX2::dropAllNonPermanent() {
    _dropIf([](const Call& call) {
            return call.state != Call::State::STATE_TERMINATED;
        }
    );
}

void LineIAX2::dropAllOutbound() {
    _dropIf([](const Call& call) {
            return call.state != Call::State::STATE_TERMINATED &&
                call.side == Call::Side::SIDE_CALLER;
        }
    );
}

void LineIAX2::processManagementCommand(const char* cmd) {  
    fixedstring tokensStore[8];
    fixedqueue<fixedstring> tokens(tokensStore, 8);
    if (tokenize(cmd, ' ', tokens) == 0) {
        /* Making a call:
        Token 0 rpt
        Token 1 cmd
        Token 2 99999
        Token 3 ilink
        Token 4 3
        Token 5 55553
        */
        if (tokens.size() == 6 && 
            tokens.at(0) == "rpt" && tokens.at(1) == "cmd" && tokens.at(4) == "3") {
            call(tokens.at(2).c_str(), tokens.at(5).c_str());
        }
        /* Disconnecting all calls:
        Token 0 rpt
        Token 1 cmd
        Token 2 99999
        Token 3 ilink
        Token 4 6
        Token 5 0
        */
        else if (tokens.size() == 6 && 
            tokens.at(0) == "rpt" && tokens.at(1) == "cmd" && tokens.at(4) == "6") {
            dropAllNonPermanent();
        }
    }
}

int LineIAX2::getPolls(pollfd* fds, unsigned fdsCapacity) {
    if (fdsCapacity < 2) 
        return -1;
    int used = 0;
    if (_iaxSockFd) {
        // We're only watching for receive events
        fds[used].fd = _iaxSockFd;
        fds[used].events = POLLIN;
        used++;
    }
    if (_dnsSockFd > 0) {
        fds[used].fd = _dnsSockFd;
        fds[used].events = POLLIN;
        used++;
    }
    return used;
}

bool LineIAX2::run2() {   
    bool w1 = _processInboundIAXData();
    bool w2 = _processInboundDNSData();
    bool w3 = _progressCalls();
    return w1 || w2 || w3;
}

bool LineIAX2::_processInboundIAXData() {

    if (!_iaxSockFd)
        return false;

    // Check for new data on the socket
    // ### TODO: MOVE TO CONFIG AREA
    const unsigned readBufferSize = 2048;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
// Windows uses slightly different types on the socket calls
#ifdef _WIN32    
    int rc = recvfrom(_iaxSockFd, (char*)readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
#else
    int rc = recvfrom(_iaxSockFd, readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
#endif
    if (rc == 0) {
        return false;
    } 
#ifdef _WIN32
    else if (rc == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return false;
    }
#else
    else if (rc == -1 && errno == 11) {
        return false;
    } 
#endif
    else if (rc > 0) {
        _processReceivedIAXPacket(readBuffer, rc, (const sockaddr&)peerAddr, _clock.time());
        // Return back to be nice, but indicate that there might be more
        return true;
    } else {
        // #### TODO: ERROR COUNTER
        _log.error("IAX2 read error %d/%d", rc, errno);
        return false;
    }
}

bool LineIAX2::_processInboundDNSData() {

    if (!_dnsSockFd)
        return false;

    // Check for new data on the socket
    // ### TODO: MOVE TO CONFIG AREA
    const unsigned readBufferSize = 512;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_storage peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
// Windows uses slightly different types on the socket calls
#ifdef _WIN32    
    int rc = recvfrom(_dnsSockFd, (char*)readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
#else
    int rc = recvfrom(_dnsSockFd, readBuffer, readBufferSize, 0, (sockaddr*)&peerAddr, &peerAddrLen);
#endif
    if (rc == 0) {
        return false;
    } 
#ifdef _WIN32
    else if (rc == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return false;
    }
#else
    else if (rc == -1 && errno == 11) {
        return false;
    } 
#endif
    else if (rc > 0) {
        _processReceivedDNSPacket(readBuffer, rc, (const sockaddr&)peerAddr);
        // Return back to be nice, but indicate that there might be more
        return true;
    } else {
        // #### TODO: ERROR COUNTER
        _log.error("DNS read error %d/%d", rc, errno);
        return false;
    }
}

void LineIAX2::_processReceivedIAXPacket(
    const uint8_t* potentiallyDangerousBuf, unsigned bufLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {
    if (potentiallyDangerousBuf[0] & 0b10000000)
        _processFullFrame(potentiallyDangerousBuf, bufLen, peerAddr, rxStampMs);
    else 
        _processMiniFrame(potentiallyDangerousBuf, bufLen, peerAddr, rxStampMs);
}

/**
 * This gets called to process a full frame. IMPORTANT: At this
 * stage the message has not been validated in any way and is
 * not necessarily part of a call. USE EXTREME CAUTION WHEN 
 * HANDLING THE MESSAGE.
 */
void LineIAX2::_processFullFrame(const uint8_t* potentiallyDangerousBuf, 
    unsigned bufLen,
    const sockaddr& peerAddr, uint32_t rxStampMs) {

    char ipStr[64];
    formatIPAddr(peerAddr, ipStr, 64);

    if (_trace) {
        _log.info("From %s (len=%d)", ipStr, bufLen);
    }

    if (bufLen < 12 || bufLen > 1500) {
        _log.error("Malformed packet from %s", ipStr);
        // TODO: CHECK SECURITY (LENGTH)
        _log.infoDump("Malformed", potentiallyDangerousBuf, bufLen);
        return;
    }

    // SECURITY: This has internal limits on how much of the buffer
    // it will accept.
    const IAX2FrameFull frame(potentiallyDangerousBuf, bufLen);

    if (_trace) {
        char msg[128];
        snprintf(msg, 128, "<==== %s SC=%d DC=%d OS=%d IS=%d TY=%d SC=%d %s",
            ipStr,
            frame.getSourceCallId(), frame.getDestCallId(), 
            frame.getOSeqNo(), frame.getISeqNo(),
            frame.getType(), frame.getSubclass(),
            iax2TypeDesc(frame.getType(), frame.getSubclass()));
        // Supress trace of voice frames
        if (!frame.isVOICE()) {
            // TOOD: CHECK SECURITY - MAKE SURE WE ARE LIMITTED
            _log.infoDump(msg, potentiallyDangerousBuf, bufLen);
        }
    }

    // Determine whether we are associated with an existing call or not
    uint16_t destCallId = frame.getDestCallId();

    // If there is no active call then handle the startup condition.
    // In this case there is no state-dependent behavior because there 
    // is no state being tracked yet.
    if (destCallId == 0) {

        if (frame.isNEW()) {
        
            if (_authorizeWithCalltoken) {

                // Create a token relevant to this NEW request
                char tokenClear[128];
                snprintf(tokenClear, 128, "T:%s:%X", ipStr, _startTime);
                MD5_CTX md5Ctx;
                MD5Init(&md5Ctx);
                MD5Update(&md5Ctx, (unsigned char*)tokenClear, strlen(tokenClear));
                unsigned char tokenHashed[16];
                char tokenHashedText[33];
                MD5Final(tokenHashed, &md5Ctx);
                MD5DigestToText(tokenHashed, tokenHashedText);

                // Check to see if the call token is available.  
                char token[65];
                bool hasToken = frame.getIE_str(54, token, 65);

                // If no token generate a challenge so that the caller comes back 
                // with it the next time.
                if (!hasToken || (hasToken && token[0] == 0)) {

                    _log.info("NEW received with no token: %s", ipStr);

                    IAX2FrameFull callTokenFrame;
                    callTokenFrame.setHeader(
                        // For now the source call ID is set to 1
                        1, 
                        // We give back the callers call ID
                        frame.getSourceCallId(), 
                        // We give back the caller's timestamp
                        frame.getTimeStamp(),
                        0, 1, 6, 40);
                    callTokenFrame.addIE_str(54, tokenHashedText);
                    // Send back to the same address we received
                    _sendFrameToPeer(callTokenFrame, peerAddr);
                    return;
                } 
                // If a NEW with a token has been provided then setup an inbound
                // call for real.
                else {
                    if (strcmp(token, tokenHashedText) != 0) {
                        _log.info("NEW received with invalid token: %s", ipStr);
                        _sendREJECT(destCallId, peerAddr, "Unknown");
                        return;
                    }
                    else {
                        _log.info("NEW received with valid token: %s", ipStr);
                    }
                }
            }

            // Pull out the important information and validate it. 
            // IMPORTANT: The target number comes in with a leading "3"
            // which is removed here.
            fixedstring targetNumber;
            char temp[33];
            bool found = frame.getIE_str(1, temp, 33);
            // NOTE: It's not completely clear, but it 
            // appears that some stations send the number
            // with a leading "3" and some do not.  There
            // are no ASL nodes that start with 3 so there 
            // must be some calling convention here.
            // 
            // If a leading "3" is provided we ignore it.
            if (found && temp[0] == '3') {
                targetNumber = temp + 1;
            } else if (found) {
                targetNumber = temp;
            } else {
                _log.error("No target number provided");
                _sendREJECT(destCallId, peerAddr, "Called number missing");
                return;
            }

            if (_destAuthorizer &&
                !_destAuthorizer->isAuthorized(targetNumber.c_str())) {
                _log.error("Wrong number");
                _sendREJECT(destCallId, peerAddr, "Wrong number");
                return;
            }

            fixedstring callingNumber;
            found = frame.getIE_str(IEType::IAX2_IE_CALLING_NUMBER, temp, 33);
            if (found) {
                callingNumber = temp;
            } else {
                _log.error("No calling number provided");
                _sendREJECT(destCallId, peerAddr, "Calling number missing");
                return;
            }

            if (_sourceAuthorizer &&
                !_sourceAuthorizer->isAuthorized(callingNumber.c_str())) {
                _log.info("Call from %s rejected", callingNumber.c_str());
                _sendREJECT(destCallId, peerAddr, "UNKNOWN");
                return;
            }

            fixedstring callingUser;
            found = frame.getIE_str(6, temp, 33);
            if (found) {
                callingUser = temp;
            } else {
                _log.error("No calling user provided");
                _sendREJECT(destCallId, peerAddr, "Calling user missing");
                return;
            }

            // What CODECs are supported by the caller?
            uint32_t capableCodecs = 0;
            if (!frame.getIE_uint32(IEType::IAX2_IE_CAPABILITY, &capableCodecs)) {
                _log.error("No CODEC capability provided");
                _sendREJECT(destCallId, peerAddr, "CODEC capability missing");
                return;
            }

            // If we're in a situation where the caller isn't capable of any of the 
            // codecs that we're capable of then reject the call.
            if ((capableCodecs & getSupportedCodecs()) == 0) {
                _log.error("No supported CODECs provided %08X", capableCodecs);
                _sendREJECT(destCallId, peerAddr, "No supported CODECs");
                return;
            }
            _log.info("Caller capable CODECs %08X", capableCodecs);

            // What CODECs are desired by the caller?
            //
            // Section 8.6.8 provides an important clarification:
            //
            // The purpose of the FORMAT information element is to indicate a single
            // preferred media CODEC.  When sent with a NEW message, the indicated
            // CODEC is the desired CODEC an IAX peer wishes to use for a call.
            // When sent with an ACCEPT message, it indicates the actual CODEC that
            // has been selected for the call.  Its data is represented in a 4-octet
            // bitmask according to Section 8.7.  
            uint32_t desiredCodec = 0;
            frame.getIE_uint32(IEType::IAX2_IE_FORMAT, &desiredCodec);
            _log.info("Caller desired CODEC %08X", desiredCodec);

            // Get the caller's CODEC preferences (optional). This is represented a 
            // string which gets converted to a list of masks.
            char codecPrefs[8] = { 0 };
            frame.getIE_str(IEType::IAX2_IE_CODEC_PREFS, codecPrefs, 8);
            _log.info("Caller CODEC preferences [%s]", codecPrefs);

            const unsigned preferredCodecsCapacity = 8;
            uint32_t preferredCodecs[preferredCodecsCapacity];
            const unsigned preferredCodecCount = parseCodecPref(codecPrefs, 
                preferredCodecs, preferredCodecsCapacity);

            // Here we assign the CODEC based on what the caller said they 
            // could handle.
            //
            // From RFC section 6.2.3: 
            //
            // An ACCEPT response is issued when a NEW message 
            // is received, and authentication has taken place (if required).
            // It acknowledges receipt of a NEW message and indicates that 
            // the call leg has been set up on the terminating side, including 
            // assigning a CODEC.  An ACCEPT message MUST include the 'format' 
            // IE to indicate its desired CODEC to the originating peer.  The 
            // CODEC format MUST be one of the formats sent in the associated 
            // NEW command.
            //
            // Upon receipt of an ACCEPT, an ACK MUST be sent and the CODEC for 
            // the call MAY be configured using the 'format' IE from the received
            // ACCEPT.  The call then waits for an ANSWER, HANGUP, or other call
            // control signal.  (See Section 6.3.)  If a subsequent ACCEPT message
            // is received for a call that has already started, or has not sent a
            // NEW message, the message MUST be ignored.
            //
            // Section 8.6.8 provides an important clarification:
            //
            // The purpose of the FORMAT information element is to indicate a single
            // preferred media CODEC.  When sent with a NEW message, the indicated
            // CODEC is the desired CODEC an IAX peer wishes to use for a call.
            // When sent with an ACCEPT message, it indicates the actual CODEC that
            // has been selected for the call.  Its data is represented in a 4-octet
            // bitmask according to Section 8.7.  Only one 
            const unsigned ourPreferredCodecsCapacity = 8;
            uint32_t ourPreferredCodecs[ourPreferredCodecsCapacity];
            const unsigned ourPreferredCodecCount = getCodecPrefs(ourPreferredCodecs,
                ourPreferredCodecsCapacity);
            const uint32_t assignedCodec = assignCodec(capableCodecs, desiredCodec,
                preferredCodecs, preferredCodecCount, 
                getSupportedCodecs(),
                ourPreferredCodecs, ourPreferredCodecCount);

            // All good, allocate the call
            int callIx = _allocateCallIx();
            if (callIx == -1) {
                _log.error("No calls available, ignoring");
                _sendREJECT(destCallId, peerAddr, "No calls available");
                return;
            }

            Call& call = _calls[callIx];
            call.reset();
            call.side = Call::Side::SIDE_CALLED;
            call.trusted = false;
            call.localCallId = _localCallIdCounter++;
            call.remoteCallId = frame.getSourceCallId();
            // Leave some space so that the elapsed time never becomes negative
            call.localStartMs = _clock.time() - AUDIO_TICK_MS;
            call.expectedInSeqNo = 1;
            call.remoteNumber = callingNumber;
            call.callUser = callingUser;
            // Move the entire address in for use when sending out messages
            memcpy(&call.peerAddr, &peerAddr, getIPAddrSize(peerAddr));
            // Schedule the ping out
            call.lastLagrqMs = _clock.time();
            call.lastFrameRxMs = _clock.time();
            call.codec = (CODECType)assignedCodec;

            // Explicit ACK 
            _sendACK(0, call);

            if (_authorizeWithAuthreq) {
                // Go out to get the caller's public key
                call.dnsRequestId = _dnsRequestIdCounter++;
                char hostName[128];
                snprintf(hostName, sizeof(hostName), "%s.nodes.%s", call.remoteNumber.c_str(), _dnsRoot);
                _log.info("Call %u starting AUTHREQ process for %s", call.localCallId, hostName);
                // Start the DNS lookup process
                if (_sendDNSRequestTXT(call.dnsRequestId, hostName) != 0) {
                    _log.error("Unable to request public key, ignoring call");
                } else {
                    call.state = Call::State::STATE_AUTHREP_WAIT_0;   
                    call.active = true;
                }
            }
            else {
                // Make an IP address lookup request. 
                call.dnsRequestId = _dnsRequestIdCounter++;
                char hostName[128];
                snprintf(hostName, sizeof(hostName), 
                    "%s.nodes.%s", call.remoteNumber.c_str(), _dnsRoot);
                // Start the DNS lookup process
                if (_sendDNSRequestA(call.dnsRequestId, hostName) != 0) {
                    _log.error("Unable to start address validation, ignoring call");
                } else {
                    call.state = Call::State::STATE_IP_VALIDATION_0;
                    call.active = true;
                }
            }
        }
        // Per the specification, we should respond to POKEs with PONGs. 
        // See RFC 5456 Section 6.7.1
        // https://datatracker.ietf.org/doc/html/rfc5456#section-6.7.1
        //
        // WARNING: This is a completely untrusted operation!
        else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, 
            IAXSubclass::IAX2_SUBCLASS_IAX_POKE)) {
            
            // We've made an extention to the POKE protocol here so that
            // this feature can be used to help with firewall/CGNAT mitigation.
            // If the POKE message contains a target address then the POKE
            // is essentially forwarded on to another node.
            char target[129];
            if (_supportDirectedPoke &&
                frame.getIE_str(IEType::IAX2_IE_TARGET_ADDR, target, 129)) {
                //_log.info("POKE had target address [%s]", target);
                sockaddr_storage poke2Addr;
                int parseRc = parseIPAddrAndPort(target, poke2Addr);
                if (parseRc == 0) {

                    IAX2FrameFull poke2;
                    poke2.setHeader(
                        // Call IDs unused
                        0, 0,
                        // Keep passing through same time (for diagnostics)
                        frame.getTimeStamp(), 
                        // SEQ UNUSED
                        0, 0, 
                        FrameType::IAX2_TYPE_IAX, 
                        IAXSubclass::IAX2_SUBCLASS_IAX_POKE);
                    
                    // The node that originated the POKE gets set as the
                    // return address.
                    char sourceAddrAndPort[128];
                    formatIPAddrAndPort(peerAddr, sourceAddrAndPort, 128);
                    poke2.addIE_str(IEType::IAX2_IE_TARGET_ADDR2, sourceAddrAndPort);

                    _sendFrameToPeer(poke2, (const sockaddr&)poke2Addr);
                }
                else {
                    _log.info("Ignoring directed POKE, unable to parse target");
                }
            }

            // If there is no target address then we create a PONG response 
            // and send it back to the peer.
            else {

                // Respond back per the specification
                IAX2FrameFull pong;
                pong.setHeader(
                    // Call IDs unused
                    0, 0,
                    // Echo back time 
                    frame.getTimeStamp(), 
                    // SEQ UNUSED
                    0, 0, 
                    FrameType::IAX2_TYPE_IAX, 
                    IAXSubclass::IAX2_SUBCLASS_IAX_PONG);
                
                // Send back the "apparent address" to help the peer figure 
                // out how they are perceived to the outside world.
                //
                // We're not using the RFC-defined format for this IE.
                // See https://datatracker.ietf.org/doc/html/rfc5456#section-8.6.17 for
                // notes on the format of the APPARENT ADDR information element.
                // Instead, we are doing ADDR:PORT as a string.
                //
                // NOTE: The use of this IE in the PONG message is not in the RFC but
                // we are adding it as part of the firewall/CGNAT mitigation strategy.
                char sourceAddrAndPort[128];
                formatIPAddrAndPort(peerAddr, sourceAddrAndPort, 128);
                pong.addIE_str(IEType::IAX2_IE_APPARENT_ADDR, sourceAddrAndPort);

                // If the POKE request has a "target address 2" in it then 
                // we use that to set the target address on the PONG. This has 
                // the effect of forwarding the PONG another hop.
                char target2[129];
                if (frame.getIE_str(IEType::IAX2_IE_TARGET_ADDR2, target2, 129)) {
                    //_log.info("POKE had target address 2 [%s]", target2);
                    // Include the address of the original POKE requestor
                    pong.addIE_str(IEType::IAX2_IE_TARGET_ADDR, target2);
                }

                _sendFrameToPeer(pong, peerAddr);
            }
        }

        // We've made an extension to the IAX2 protocol to allow PONG messages
        // to be forwarded.
        else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, 
            IAXSubclass::IAX2_SUBCLASS_IAX_PONG)) {

            // We've made an extension to the PONG protocol to help with firewall/
            // CGNAT traversal. If the POKE message has a target address then we
            // just forward it along.
            char target[129];
            if (_supportDirectedPoke &&
                frame.getIE_str(IEType::IAX2_IE_TARGET_ADDR, target, 129)) {

                _log.info("PONG had target address [%s]", target);

                sockaddr_storage pong2Addr;
                int parseRc = parseIPAddrAndPort(target, pong2Addr);
                if (parseRc == 0) {

                    IAX2FrameFull pong2;
                    pong2.setHeader(
                        // Call IDs unused
                        0, 0,
                        // Echo back time 
                        frame.getTimeStamp(), 
                        // SEQ UNUSED
                        0, 0, 
                        FrameType::IAX2_TYPE_IAX, 
                        IAXSubclass::IAX2_SUBCLASS_IAX_PONG);

                    // If the PONG has an apparent address, copy it and forward.
                    // NOTE: We are using a string format of APPARENT_ADDR.
                    char apparentAddrBuf[65];
                    if (frame.getIE_str(IEType::IAX2_IE_APPARENT_ADDR,
                        apparentAddrBuf, 65)) { 
                        pong2.addIE_str(IEType::IAX2_IE_APPARENT_ADDR, 
                            apparentAddrBuf);
                    }

                    _sendFrameToPeer(pong2, (const sockaddr&)pong2Addr);
                }
                else {
                    _log.info("Ignoring directed PONG, unable to parse target");
                }
            }
        }
        // There are no other message types that are allowed without a valid 
        // destination call ID
        else {
            return;
        }
    }
    
    // Peer is claiming that this is an active call, perform some additional 
    // validation to find out if we trust the message.
    else {

        // Figure out which call this frame potentially belongs to (if any)
        bool recognizedCall = false;        
        unsigned recognizedCallIx = 0;

        for (unsigned i = 0; i < _maxCalls; i++) {
            const Call& call = _calls[i];
            if (call.active && call.localCallId == destCallId) {
                recognizedCall = true;
                recognizedCallIx = i;
                break;
            }
        }

        if (!recognizedCall) {
            _invalidCallPacketCounter++;
            if (_trace)
                _log.info("Call not recognized %d/%d, ignoring", frame.getSourceCallId(), destCallId);
            return;
        }

        Call& untrustedCall = _calls[recognizedCallIx];

        // Validation check - make sure the source IP is still right. 
        if (!untrustedCall.isPeerAddr(peerAddr)) {
            _invalidCallPacketCounter++;
            _log.info("Call %u address invalid", destCallId);
            // Just ignore, don't change state in case this is a DOS attack
            return;
        }

        if (untrustedCall.side == Call::Side::SIDE_CALLER) {
            // Look for the challenge case
            if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_CALLTOKEN)) {
                _log.info("Call %u got CALLTOKEN challenge", destCallId);
                char token[65];
                if (!frame.getIE_str(54, token, 65) || token[0] == 0) {
                    _log.error("Unable to get challenge token");
                    return;
                }
                // Save the token for the NEW retry
                untrustedCall.calltoken = token;
                // Go back to the beginning and send a NEW again.
                untrustedCall.state = Call::State::STATE_INITIATION_WAIT;
                return;   
            }
            // Look for the cases where we should be locking in the peer's call ID
            else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_ACCEPT) ||
                frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_AUTHREQ)) {
                // Lock in the remote call ID
                untrustedCall.remoteCallId = frame.getSourceCallId();  
                untrustedCall.trusted = true;    
            }
        }

        // Look for the completion of an AUTHREQ/AUTHREP cycle
        else if (untrustedCall.side == Call::Side::SIDE_CALLED) {
            if (untrustedCall.state == Call::State::STATE_AUTHREP_WAIT_1 &&
                frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_AUTHREP) &&
                untrustedCall.remoteCallId == frame.getSourceCallId()) {

                // Make a challenge that uses some things that are unique to the call
                char challengeTxt[32];
                snprintf(challengeTxt, 31, "%u%u", 
                    untrustedCall.localCallId, untrustedCall.localStartMs);

                // Only supporting ED25519 challenge, which is found in the 0x20 IE.
                char sigHex[129];
                // NOTE: The getIE includes space for the null termination.
                if (!frame.getIE_str(IEType::IAX2_IE_ED25519_RESULT, sigHex, 129) || sigHex[0] == 0) {
                    _log.error("Call %u no challenge response", destCallId);
                    return;
                }
                if (strlen(sigHex) != 128) {
                    _log.error("Call %u invalid challenge response", destCallId);
                    return;
                }

                // #### TODO ADD CHARACTER VALIDATION HERE
                unsigned char sigBin[64];
                asciiHexToBin(sigHex, 128, sigBin, 64);

                // Do the actual public key validation 
                if (ed25519_verify(sigBin, 
                    (const uint8_t*)challengeTxt, strlen(challengeTxt), 
                    untrustedCall.publicKeyBin) == 1) {
                    untrustedCall.state = Call::State::STATE_CALLER_VALIDATED;

                    _log.info("Call %u good signature", destCallId);                   
                }
                else {
                    _log.info("Call %u invalid signature", destCallId);
                }

                // Normally the sequence/ACK would be handled later, but this message
                // is a special case.
                if (frame.getOSeqNo() == untrustedCall.expectedInSeqNo) {
                    untrustedCall.incrementExpectedInSeqNo();
                    if (frame.isACKRequired())
                        _sendACK(frame.getTimeStamp(), untrustedCall);
                }
                else {
                    _log.error("Call %u sequence number problem", destCallId);
                }

                return;
            }
        }

        if (!untrustedCall.trusted) {
            if (!frame.isACK()) {
                // Diagnostic messages for untrusted messages being ignored
                _log.info("Message for call %u untrusted (state %d)", destCallId, untrustedCall.state);
                char msg[128];
                snprintf(msg, 128, "<==== %s SC=%d DC=%d OS=%d IS=%d TY=%d SC=%d %s",
                    ipStr,
                    frame.getSourceCallId(), frame.getDestCallId(), 
                    frame.getOSeqNo(), frame.getISeqNo(),
                    frame.getType(), frame.getSubclass(),
                    iax2TypeDesc(frame.getType(), frame.getSubclass()));
                // Supress trace of voice frames
                if (!frame.isVOICE()) {
                    _log.infoDump(msg, potentiallyDangerousBuf, bufLen);
                }
            }
            return;
        }
        
        // At this point we have high confidence that the frame belongs to an active call.
        Call& trustedCall = untrustedCall;
        _processFullFrameInCall(frame, trustedCall, rxStampMs);
    }
}

void LineIAX2::_processFullFrameInCall(const IAX2FrameFull& frame, Call& call, 
    uint32_t rxStampMs) {

    call.lastFrameRxMs = _clock.time();
   
    // Use every message to update our information about what the peer
    // has received and what is still in flight. This will have the 
    // side-effect of clearing things from the re-transmit buffer that 
    // were being held for possible re-transmission in the future.
    //
    // NOTE (17-Nov-2025) It appears that the PONG messages that Asterisk
    // sends back might have lower ISeqNos than expected, meaning other 
    // previous messages have acknowledge more receipts already. This 
    // condition is ignored.
    if (!call.reTx.setExpectedSeq(frame.getISeqNo())) {
        //_log.info("Call %d/%d not lowering expected sequence from %d to %d", 
        //    call.localCallId, call.remoteCallId, 
        //    (int)call.reTx.getExpectedSeq(), frame.getISeqNo());
    }

    if (frame.isACK()) {
        if (_trace)
            _log.info("Got ACK for seq %d", frame.getOSeqNo());
        return;
    }        
    
    // VNAK messages don't consume sequence numbers to take care of this
    // before checking sequence coherence.
    if (frame.isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_VNAK)) {
        _log.info("VNAK received, retransmitting to %d", (int)frame.getOSeqNo());
        call.reTx.retransmitToSeq(frame.getOSeqNo(), call.expectedInSeqNo,       
            // The callback that will be fired for anything that rxTx needs to send
            [this, a=call.peerAddr](const IAX2FrameFull& frame) {
                _sendFrameToPeer(frame, (const sockaddr&)a);
            } );
        return;
    }

    // Make sure the sequence number is correct. If so, move the expected 
    // sequence number forward and generate the ACK.
    if (frame.getOSeqNo() == call.expectedInSeqNo) {
        call.incrementExpectedInSeqNo();
        // Generate an ACK in most cases
        //
        // From RFC:
        // ".. and MUST return the same time-stamp it received.  This
        // time-stamp allows the originating peer to determine to which message
        // the ACK is responding.  Receipt of an ACK requires no action."
        if (frame.isACKRequired())
            _sendACK(frame.getTimeStamp(), call);
    }
    else if (compareSeqWrap(frame.getOSeqNo(), call.expectedInSeqNo) < 0) {

        // A re-transmit is a legit reason to have a low sequence number
        if (frame.isRetransmit()) {
            // Apparently it's normal for a CALLTOKEN to have the retransmit flag on.
            if (!frame.isTypeClass(IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_CALLTOKEN)) {
                // Do nothing
            } else {
                // ACK again with the same timestamp and no change to expected sequence number.
                // Hopefully this satisfies the peer. 
                if (frame.isNoACKRequired()) {
                    // Do nothing
                } else {
                    // Being more aggressive and sending an ACK unless we are sure 
                    // it's not needed.
                    _sendACK(frame.getTimeStamp(), call);   
                    // Display to help improve the list of ack/noack cases
                    if (!frame.isACKRequired()) {
                        _log.info("Sent ACK, but not sure if required: %d/%d",
                            (int)frame.getType(), (int)frame.getSubclass());
                    }
                }
            }
        }
        else {
            _log.info("Ignoring message already acknowledged (low sequence) %d",
                (int)frame.getOSeqNo());
            call._rxSeqErrorCount++;
        }
        // NOTE: We return with no further processing since we already processed the message
        return;
    }
    // If the sequence number is wrong then ignore the message (retransmit 
    // requests will clean this up later).
    else {
        //_log.info("Call %u/%u ignoring high sequence frame %d, expected %d", 
        //    call.localCallId, call.remoteCallId,
        //    (int)frame.getOSeqNo(), (int)call.expectedInSeqNo);
        call._rxSeqErrorCount++;
        return;
    }

    // Process the frame based on the type and state

    // Ignore frames for terminated calls.
    if (call.state == Call::State::STATE_TERMINATED ||
        call.state == Call::State::STATE_TERMINATE_WAITING) {
        IAX2FrameFull invalFrame;
        invalFrame.setHeader(call.localCallId, call.remoteCallId, 
            call.dispenseElapsedMs(_clock), 
            call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_IAX,
                IAXSubclass::IAX2_SUBCLASS_IAX_HANGUP);
        _sendFrameToPeer(invalFrame, call);
        return;
    }

    // AUTHREQ
    if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_AUTHREQ)) {
        
        _log.info("Call %u got AUTHREQ challenge", call.localCallId);

        // Look at the challenge type
        uint16_t authmethod = 0;
        if (!frame.getIE_uint16(IEType::IAX2_IE_AUTHMETHODS, &authmethod)) {
            _log.error("Call %u unable to get AUTHMETHOD", call.localCallId);
            return;
        }

        // NOTE: We are assuming the 0x08 bit signifies ED25519 method (not in the official
        // RFC document)
        if ((authmethod & 0x08) == 0) {
            _log.error("Call %u unsupported AUTHMETHOD", call.localCallId);
            return;
        }

        // Pull out the ED25519 challenge token, leaving room for a null-termination
        char token[33];
        if (!frame.getIE_str(IEType::IAX2_IE_CHALLENGE, token, 33) || token[0] == 0) {
            _log.error("Unable to get challenge token");
            return;
        }

        // Sign the challenge token using our private key
        uint8_t seedBin[32];
        asciiHexToBin(_privateKeyHex, 64, seedBin, 32);
        unsigned char pubBin[32];
        unsigned char privBin[64];
        ed25519_create_keypair(pubBin, privBin, seedBin);
        unsigned char sig[64];
        ed25519_sign(sig, (const uint8_t*)token, strlen(token), pubBin, privBin);
        char sigHex[129];
        binToAsciiHex(sig, 64, sigHex, 128);
        sigHex[128] = 0;

        // Make the AUTHREP response
        IAX2FrameFull authrepFrame;
        authrepFrame.setHeader(call.localCallId, call.remoteCallId, 
            call.dispenseElapsedMs(_clock), 
            call.outSeqNo, call.expectedInSeqNo, 
            FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_AUTHREP);
        authrepFrame.addIE_str(IEType::IAX2_IE_ED25519_RESULT, sigHex, 128);

        _sendFrameToPeer(authrepFrame, call);
    } 
    // REJECT
    else if (frame.getType() == 6 && frame.getSubclass() == 6) {
        _log.info("Call %u got REJECT", call.localCallId);
        _hangupCall(call);
    } 
    // ACCEPT (i.e. we are the caller)
    else if (frame.isACCEPT()) {

        _log.info("Call %u got ACCEPT t=%u", call.localCallId, call.localElapsedMs(_clock)); 

        // Since the elapsed time is reset with NEW, the ACCEPT time should
        // represent the round-trip network latency. One-way is half.
        call.setNetworkDelayEstimate(call.localElapsedMs(_clock) / 2, true);

        // Pull out the CODEC assignment
        uint32_t codec;
        if (!frame.getIE_uint32(IEType::IAX2_IE_FORMAT, &codec)) {
            _log.error("Unable to get assigned CODEC");
            _hangupCall(call);
            return;
        } 
        else {
            if (isCodecSupported((CODECType)codec)) {
                call.codec = (CODECType)codec;
                _log.info("CODEC assigned %08X", call.codec);
            } else {
                _log.error("Unsupported CODEC assigned");
                _hangupCall(call);
                return;
            }
        }

        // There are a few text messages that need to be sent 
        // at the beginning of a call. I don't know exactly
        // what these mean.

        IAX2FrameFull respFrame1;
        respFrame1.setHeader(call.localCallId, call.remoteCallId, 
            call.dispenseElapsedMs(_clock), 
            call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_TEXT, 0);
        char strmsg[256];
        snprintf(strmsg, sizeof(strmsg), "T %s COMPLETE", call.localNumber.c_str());
        // NOTE: INCLUDING NULL!
        respFrame1.setBody((const uint8_t*)strmsg, strlen(strmsg) + 1);
        _sendFrameToPeer(respFrame1, call);

        IAX2FrameFull respFrame3;
        respFrame3.setHeader(call.localCallId, call.remoteCallId, 
            call.dispenseElapsedMs(_clock), 
            call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_TEXT, 0);
        snprintf(strmsg, sizeof(strmsg), "T %s CONNECTED,%s,%s", 
            call.localNumber.c_str(), call.localNumber.c_str(),
            call.remoteNumber.c_str());
        // NOTE: INCLUDING NULL!
        respFrame3.setBody((const uint8_t*)strmsg, strlen(strmsg) + 1);
        _sendFrameToPeer(respFrame3, call);

        call.state = Call::State::STATE_LINKED;

        // Generate an internal message to announce the new call
        PayloadCallStart payload;
        payload.codec = call.codec;
        payload.startMs = call.localStartMs;
        strcpyLimited(payload.localNumber, call.localNumber.c_str(), sizeof(payload.localNumber));
        strcpyLimited(payload.remoteNumber, call.remoteNumber.c_str(), sizeof(payload.remoteNumber));
        payload.originated = true;
        MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
            sizeof(payload), (const uint8_t*)&payload, 0, rxStampMs);
        msg.setSource(_busId, call.localCallId);
        msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(msg);
    }
    // ANSWER
    else if (frame.getType() == FrameType::IAX2_TYPE_CONTROL && 
             frame.getSubclass() == ControlSubclass::IAX2_SUBCLASS_CONTROL_ANSWER) {
        
        //_log.info("Call %u got ANSWER", call.localCallId);            

        if (call.side == Call::Side::SIDE_CALLER) {
            if (call.state == Call::State::STATE_LINKED)
                call.state = Call::State::STATE_UP;
            else 
                _log.info("State unexpected");
        }
    }
    // STOP_SOUNDS
    else if (frame.getType() == FrameType::IAX2_TYPE_CONTROL && frame.getSubclass() == 255) {
        //_log.info("Call %u got STOP_SOUNDS", call.localCallId);
    }
    // KEY
    else if (frame.getType() == FrameType::IAX2_TYPE_CONTROL && frame.getSubclass() == 12) {
        //_log.info("Call %u got KEY", call.localCallId);            
    }
    // UNKEY
    else if (frame.getType() == FrameType::IAX2_TYPE_CONTROL && 
             frame.getSubclass() == ControlSubclass::IAX2_SUBCLASS_CONTROL_UNKEY) {
        
        MessageEmpty unkeyMsg(Message::Type::SIGNAL, Message::SignalType::RADIO_UNKEY, 
            frame.getTimeStamp(), rxStampMs);
        unkeyMsg.setSource(_busId, call.localCallId);
        unkeyMsg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(unkeyMsg);

        //_traceLog.info("UNK", frame.getTimeStamp());
    }
    // LAGRQ
    // 6.7.4.  LAGRQ Lag Request Message
    // A LAGRQ is a lag request.  It is sent to determine the lag between
    // two IAX endpoints, including the amount of time used to process a
    // frame through a jitter buffer (if any).  It requires a clock-based
    // time-stamp, and MUST be answered with a LAGRP, which MUST echo the
    // LAGRQ's time-stamp.  The lag between the two peers can be computed on
    // the peer sending the LAGRQ by comparing the time-stamp of the LAGRQ
    // and the time the LAGRP was received.
    // This message does not require any IEs.
    else if (frame.isTypeClass(6, 0x0b)) {
        IAX2FrameFull respFrame;
        respFrame.setHeader(call.localCallId, call.remoteCallId, 
            // In this case we echo back the timestamp that we got.
            // In reality this was supposed to have been passed
            // through the jitter buffer.
            frame.getTimeStamp(), 
            call.outSeqNo, call.expectedInSeqNo, 6, 0x0c);
        _sendFrameToPeer(respFrame, call);
    }
    // LAGRP
    // 6.7.5.  LAGRP Lag Response Message
    // A LAGRP is a lag reply, sent in response to a LAGRQ message.  It MUST
    // send the same time-stamp it received in the LAGRQ after passing the
    // received frame through any jitter buffer the peer has configured.
    // This message does not require any IEs.
    else if (frame.isTypeClass(6, 0x0c)) {
        call.lastLagMs = call.localElapsedMs(_clock) - frame.getTimeStamp();
    }
    // PING
    else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_PING)) {
        IAX2FrameFull respFrame;
        respFrame.setHeader(call.localCallId, call.remoteCallId, 
            call.dispenseElapsedMs(_clock), 
            call.outSeqNo, call.expectedInSeqNo, 6, 3);
        _sendFrameToPeer(respFrame, call);
    }
    // PONG
    else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_PONG)) {

        call.lastPingTimeMs = _clock.time() - call.lastPingSentMs;

        // Eliminate gross outliers, smooth out the delays a bit
        if (call.lastPingTimeMs < 500) {
            call.pingCount++;
            // Assume the one-way delay is half the round-trip ping.
            call.setNetworkDelayEstimate(call.lastPingTimeMs / 2, call.pingCount == 1);
        }
    }
    // VOICE
    else if (frame.isVOICE()) {
        
        _log.info("Call %u got VOICE", call.localCallId);

        // Make a voice message from the network frame content and 
        // pass it to the consumers.
        bool goodVoice = false;
        const unsigned vfs = maxVoiceFrameSize(call.codec);
        if (vfs > 0) {
            if (frame.size() == 12 + vfs) {
                goodVoice = true;
            } else {
                _log.info("Voice frame size error");
            }
        } else {
            _log.error("Unsupported CODEC");
        }

        if (goodVoice) {
            // #### TODO: NEED TO GET THE RIGHT TIMESTAMP!
            // #### TODO: NEED TO GET THE RIGHT TIMESTAMP!
            // #### TODO: NEED TO GET THE RIGHT TIMESTAMP!
            MessageWrapper voiceMsg(Message::Type::AUDIO, call.codec,
                vfs, frame.buf(), frame.getTimeStamp(), rxStampMs);
            voiceMsg.setSource(_busId, call.localCallId);
            voiceMsg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(voiceMsg);
        }

        call.lastRxVoiceFrameMs = _clock.timeUs() / 1000;
        // #### TODO: IGNORED PACKET COUNT
    }
    // TEXT
    else if (frame.isTypeClass(FrameType::IAX2_TYPE_TEXT, 0)) {

        // Truncate/null-terminate the text
        char textMessage[MAX_TEXT_MESSAGE_SIZE];
        unsigned len = min((unsigned)(sizeof(textMessage) - 1), (frame.size() - 12));
        memcpy(textMessage, (const char*)frame.buf() + 12, len);
        textMessage[len] = 0;

        if (len == 0) {
            // Do nothing
        }
        // Not sure what this means, but following the rule of sending back
        // the same message.
        else if (strcmp(textMessage, "!NEWKEY1!") == 0) {
            IAX2FrameFull respFrame0;
            respFrame0.setHeader(call.localCallId, call.remoteCallId, 
                call.dispenseElapsedMs(_clock), 
                call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_TEXT, 0);
            // NOTE: INCLUDING NULL!
            respFrame0.setBody((const uint8_t*)"!NEWKEY1!", 10);
            _sendFrameToPeer(respFrame0, call);
        }
        else if (strcmp(textMessage, "!!DISCONNECT!!") == 0) {
            _log.info("Call %u got forced disconnect", call.localCallId);
        }
        // 27-Jan-2026 Bruce saw this alternate method of sending DTMF
        // commands while testing on IaxRtp (Windows softphone).
        else if (textMessage[0] == 'D') {
            // Tokenize
            unsigned spaceCount = 0;
            unsigned i = 0;
            for (; i < strlen(textMessage) && spaceCount < 4; i++)
                if (textMessage[i] == ' ') 
                    spaceCount++;
            // The pointer should end up on top of the DTMF symbol
            if (textMessage[i] != 0) {
                char symbol = textMessage[i];
                _log.info("Call %u DTMF Press %c", call.localCallId, symbol);
                PayloadDtmfPress payload;
                payload.symbol = symbol;
                MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::DTMF_PRESS, 
                    sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
                msg.setSource(_busId, call.localCallId);
                msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
                _bus.consume(msg);
            }
        } 
        // The "T" messages contain telemetry
        // T <NODE_NB> <CMD>,<PARAMS>
        //   NODE_NB Node sending message
        //   CMD Telemetry command ALLCAPS
        //   PARAMS Optional comma separated list of command specific parameters
        //
        // Specifics about some commands:
        //  
        // T <NODE_NB> STATUS,<NODE_1>,0,<NODE_LIST>
        //   NODE_NB Node sending message
        //   NODE_1 Node reporting status
        //   0 Unknown
        //   NODE_LIST List of nodes connected to NODE_1. Each node number is 
        //   prefixed by a connection mode as follow:
        //       T Transceive mode, send and receive audio
        //       R Receive audio only
        //       C Connection is pending
        //
        // T <NODE_NB> CONNECTED,<NODE_1>,<NODE_2>
        //   NODE_NB Node sending message
        //   NODE_1 Node initiating connection
        //   NODE_2 Other node connected to
        //
        // T <NODE_NB> REMALREADY
        //
        //   Removed Already Connected. Remote node is already part of the network. 
        //   Note that it could be a direct connection or it could be the node is 
        //   connected via other nodes.
        //
        // T <NODE_NB> CONNFAIL,<REMOTE_NODE_NB>
        //
        //   Connection Failed. Remote node can't be connected to.
        //
        // T <NODE_NB> STATS_VERSION,161.327
        // 
        // T <NODE_NB> TALKERID,NAME
        //
        //   (Ampersand extension)
        //   This message is used to identify the active talker on the 
        //   designated node. 
        //   NODE_NNB Node sending the message
        //   NAME Arbitrary text, but should be a callsign and name, only
        //   the first 32 characters are used.
        //
        else if (textMessage[0] == 'T') {

            const unsigned cmdCapacity = 17;
            char cmd[cmdCapacity] = { 0 };
            unsigned cmdLen = 0;
            const unsigned paramsCapacity = 65;
            char params[paramsCapacity] = { 0 };
            unsigned paramsLen = 0;

            int state = 0;
            for (unsigned i = 0; i < strlen(textMessage); i++) {
                char c = textMessage[i];
                if (state == 0) {
                    if (c == ' ')
                        state = 1;
                } else if (state == 1) {
                    if (c == ' ')
                        state = 2;
                } else if (state == 2) {
                    if (c == ',') {
                        cmd[cmdLen] = 0;
                        state = 3;
                    } else {
                        // Always leave space for the null
                        if (cmdLen < cmdCapacity - 1)
                            cmd[cmdLen++] = c;
                    }
                } else if (state == 3) {
                    // Always leave space for the null
                    if (paramsLen < paramsCapacity - 1)
                        params[paramsLen++] = c;
                }
            }
            params[paramsLen] = 0;

            if (strcmp(cmd,"TALKERID") == 0) {
                //_log.info("Talker ID from %s: [%s]", call.remoteNumber.c_str(), 
                //    params);
                MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_TALKERID, 
                    // Include the null
                    paramsLen + 1, (const uint8_t*)params, 
                    0, _clock.time());
                msg.setSource(_busId, call.localCallId);
                msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
                _bus.consume(msg);
            }
            else {
                //_log.infoDump("Telemetry", frame.buf(), frame.size());
            }
        } 
        // The "L" message contains the list of linked nodes.
        // L <MODE><NODE_NB>,<MODE><NODE_NB>,...
        // 
        //  MODE Each node number is prefixed by a connection MODE as follow:
        //    T Transceive mode, send and receive audio
        //    R Receive audio only
        //    C Connection is pending
        //  NODE_NB Linked node
        //
        //  If no other nodes are connected, the list is empty and only L is sent.
        else if (textMessage[0] == 'L') {
            MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::LINK_REPORT, 
                // The initial "L " gets stripped here
                strlen(textMessage) - 2, (const uint8_t*)textMessage + 2, 
                0, _clock.time());
            msg.setSource(_busId, call.localCallId);
            msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(msg);
        }
        else {
            _log.infoDump("Unrecognized text", frame.buf(), frame.size());
        }
    }
    // HANGUP
    else if (frame.isTypeClass(FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_HANGUP)) {
        _log.info("Call %u got HANGUP", call.localCallId); 
        call.state = Call::State::STATE_TERMINATE_WAITING;
    }
    // COMFORT NOISE
    else if (frame.getType() == 10) {
        // Completely ignore this
    }
    // DTMF press
    else if (frame.getType() == 12) {
        _log.info("Call %u DTMF Press %c", call.localCallId, (char)frame.getSubclass());

        PayloadDtmfPress payload;
        payload.symbol = (char)frame.getSubclass();

        MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::DTMF_PRESS, 
            sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
        msg.setSource(_busId, call.localCallId);
        msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(msg);
    }
    // DTMF release
    else if (frame.getType() == 1) {
        _log.info("Call %u DTMF Release %c", call.localCallId, (char)frame.getSubclass());
    }
    else {
        _log.info("Call %u UNRECOGNIZED FRAME %d/%d", call.localCallId,
            frame.getType(), frame.getSubclass());
        _log.infoDump("Frame", frame.buf(), frame.size());
    }
 }

int LineIAX2::_allocateCallIx() {
    for (unsigned i = 0; i < _maxCalls; i++)
        if (!_calls[i].active)
            return i;
    return -1;
}

void LineIAX2::_processMiniFrame(const uint8_t* buf, unsigned bufLen,
    const sockaddr& unverifiedPeerAddr, uint32_t rxStampMs) {

    // Validate
    if (bufLen < 4) {
        return;
    }

    // Figure out which call this frame belong to (if any)
    uint16_t sourceCallId = unpack_uint16_be(buf) & 0x7fff;

    _visitActiveCallsIf(
        // Visitor
        [line=this, &log=_log, buf, bufLen, rxStampMs](Call& call) {

            call.lastFrameRxMs = line->_clock.time();
            call.lastRxVoiceFrameMs = line->_clock.timeUs() / 1000;

            // Get the short time from the frame and convert it into a 
            // full time. IMPORTANT: There is an assumption here that 
            // the remote time and local time are fairly close to each 
            // other in order for this conversion to work.
            uint16_t lowRemoteTime = unpack_uint16_be(buf + 2);
            uint32_t remoteTime = amp::SequencingBufferStd<MessageCarrier>::extendTime(lowRemoteTime,
                call.localElapsedMs(line->_clock));

            // Make a voice message from the network frame content and pass it
            // to the consumers.
            unsigned vfs = maxVoiceFrameSize(call.codec);
            if (vfs > 0) {
                if (bufLen <= 4 + vfs) {
                    MessageWrapper voiceMsg(Message::Type::AUDIO, call.codec,
                        vfs, buf + 4, remoteTime, rxStampMs);
                    voiceMsg.setSource(line->_busId, call.localCallId);
                    voiceMsg.setDest(line->_destLineId, Message::UNKNOWN_CALL_ID);
                    line->_bus.consume(voiceMsg);
                } else {
                    log.error("Voice frame size error");
                    return;
                }
            } else {
                log.error("Unsupported CODEC");
                return;
            }
        },
        // Predicate
        [sourceCallId, unverifiedPeerAddr](const Call& call) {
            return call.remoteCallId == sourceCallId && call.isPeerAddr(unverifiedPeerAddr);
        }
    );
}

void LineIAX2::_processReceivedDNSPacket(const uint8_t* buf, unsigned bufLen,
    const sockaddr& peerAddr) {
    if (bufLen < 12)
        return;
    // Look at the request ID to see if it matches any active calls that
    // are waiting for DNS data to progress.
    uint16_t dnsRequestId = unpack_uint16_be(buf);
    _visitActiveCallsIf(
        // Visitor
        [buf, bufLen, &log=_log, line=this](Call& call) { 
            if (call.state == Call::State::STATE_LOOKUP_0A)
                line->_processDNSResponse0(call, buf, bufLen);
            else if (call.state == Call::State::STATE_LOOKUP_1A)
                line->_processDNSResponse1(call, buf, bufLen);
            else if (call.state == Call::State::STATE_IP_VALIDATION_0)
                line->_processDNSResponseIPValidation(call, buf, bufLen);
            else if (call.state == Call::State::STATE_AUTHREP_WAIT_0)
                line->_processDNSResponsePublicKey(call, buf, bufLen);
            // Otherwise ignore
            else 
                log.info("Ignoring unexpected DNS response");
        },
        // Predicate
        [dnsRequestId](const Call& call) {
            // Does this call care about this DNS request?
            return call.dnsRequestId == dnsRequestId;
        }
    );
}

void LineIAX2::_processDNSResponse0(Call& call, 
    const uint8_t* buf, unsigned bufLen) {

    if (_trace)
        _log.infoDump("DNS response (SRV)", buf, bufLen);

    // Pull the port number and domain name from the response 
    uint16_t pri, weight, port;
    char srvHost[65];
    int rc1 = microdns::parseDNSAnswer_SRV(buf, bufLen,
        &pri, &weight, &port, srvHost, 65);
    if (rc1 < 0) {
        if (rc1 == -3) {
            _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
                "Node is not registered");
        } else {
            _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
                "DNS error (SRV)");
        }
        _terminateCall(call);
        return;
    }
    // #### TODO: This will be based on address type, but since
    // this is an IPv4 DNS lookup we assume that.
    call.peerAddr.ss_family = AF_INET;
    setIPPort(call.peerAddr, port);

    // Start a second DNS request
    call.dnsRequestId = _dnsRequestIdCounter++;
    if (_sendDNSRequestA(call.dnsRequestId, srvHost) != 0) {
        _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
            "DNS error (A2)");
        _terminateCall(call);
    } else {
        call.state = Call::State::STATE_LOOKUP_1A;
    }
}

void LineIAX2::_processDNSResponse1(Call& call, 
    const uint8_t* buf, unsigned bufLen) {

    if (_trace)
        _log.infoDump("DNS response (A)", buf, bufLen);

    // Pull the IP address out of the DNS response
    uint32_t addr;
    int rc1 = microdns::parseDNSAnswer_A(buf, bufLen, &addr);
    if (rc1 < 0) {
        _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
            "DNS error (A)");
        _terminateCall(call);
        return;
    }

    // #### TODO: AVOID STRING CONVERSION?
    char addrStr[64];
    formatIP4Address(addr, addrStr, 64);
    setIPAddr(call.peerAddr, addrStr);

    _log.info("DNS responded with %s", addrStr);

    call.dnsRequestId = 0;
    // Now in a state to start connecting
    call.state = Call::State::STATE_INITIATION_WAIT;
}

void LineIAX2::_processDNSResponseIPValidation(Call& call, 
    const uint8_t* buf, unsigned bufLen) {

    if (_trace)
        _log.infoDump("DNS response (A)", buf, bufLen);

    // Pull the IP address out of the DNS response
    uint32_t addr;
    int rc1 = microdns::parseDNSAnswer_A(buf, bufLen, &addr);
    if (rc1 < 0) {
        if (_sourceIpValidationRequired) {
            _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
                "DNS error (A)");
            _terminateCall(call);
        } else {
            call.state = Call::State::STATE_CALLER_VALIDATED;
        }
    }
    else {
        // NOTE: All of this is assuming IPv4!
        // Get the address returned by DNS
        char addrStr[64];
        formatIP4Address(addr, addrStr, 64);
        // Get the address of the peer
        char addrStrPeer[64];
        formatIPAddr((const sockaddr&)call.peerAddr, addrStrPeer, 64);

        if (strcmp(addrStr, addrStrPeer) == 0) {
            _log.info("Call %u IP validation succeeded", call.localCallId);
            call.state = Call::State::STATE_CALLER_VALIDATED;
            call.sourceAddrValidated = true;
        } else {
            if (_sourceIpValidationRequired) {
                _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
                    "IP address validation failed");
                _terminateCall(call);
            } else{
                _log.info("Call %u ignoring IP validation failure", call.localCallId);
                call.state = Call::State::STATE_CALLER_VALIDATED;
            }
        }
    }
}

void LineIAX2::_processDNSResponsePublicKey(Call& call, 
    const uint8_t* buf, unsigned bufLen) {

    if (_trace)
        _log.infoDump("DNS response (TXT)", buf, bufLen);

    // Pull the port number and domain name from the response 
    char txt[256];
    int rc1 = microdns::parseDNSAnswer_TXT(buf, bufLen, txt, 256);
    if (rc1 < 0) {
        _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
            "DNS error (TXT)");
        _terminateCall(call);
        return;
    }
    if (strlen(txt) != 64) {
        _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
            "Invalid public key");
        _terminateCall(call);
        return;
    }

    // Once a valid public key is obtained we launch an AUTHREQ challenge
    // to the calling peer.

    // #### TODO: ADD SYNTAX CHECKING ON HEX CHARACTERS
    // Convert the hex public key to binary 
    asciiHexToBin(txt, 64, call.publicKeyBin, 32);

    // Make a challenge that uses some things that are unique to the call
    char challenge[32];
    snprintf(challenge, 31, "%u%u", call.localCallId, call.localStartMs);

    IAX2FrameFull authreqFrame;
    authreqFrame.setHeader(call.localCallId, call.remoteCallId, 
        call.dispenseElapsedMs(_clock), 
        call.outSeqNo, call.expectedInSeqNo, 
        FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_AUTHREQ);
    // NOTE: We are requiring AUTHMETHOD 0x08, which is ED25519 (not in RFC)
    authreqFrame.addIE_uint16(0x0e, 0x08);
    authreqFrame.addIE_str(0x0f, challenge);

    _sendFrameToPeer(authreqFrame, call);

    call.state = Call::State::STATE_AUTHREP_WAIT_1;                
}

bool LineIAX2::_progressCalls() {

    // IMPORTANT: If any of the calls say that they may need more work then 
    // the entire line should say that it might need more work.
    bool moreNeeded = false;

    _visitActiveCallsIf(
        // Visitor
        [line=this, &moreNeeded](Call& call) {
            bool w = line->_progressCall(call);
            if (w)
                moreNeeded = true;
        },
        // Predicate
        [](const Call& call) { return true; }
    );
    return moreNeeded;
}

// #### TODO: MOVE TO CALL CLASS

bool LineIAX2::_progressCall(Call& call) {

    const Call::State originalState = call.state;

    // State-dependent activity
    if (call.side == Call::Side::SIDE_CALLER) {
        if (call.state == Call::State::STATE_LOOKUP_0) {
            // Generate a new DNS ID
            call.dnsRequestId = _dnsRequestIdCounter++;
            // Create the SRV query hostname
            char srvHostName[256];
            snprintf(srvHostName, sizeof(srvHostName), 
                "_iax._udp.%s.nodes.%s", call.remoteNumber.c_str(), _dnsRoot);
            // Start the DNS lookup process
            if (_sendDNSRequestSRV(call.dnsRequestId, srvHostName) != 0) {
                _publishCallFailed(call.localNumber.c_str(), call.remoteNumber.c_str(),
                    "DNS error (SRV2)");
                _terminateCall(call);
            } else {
                call.state = Call::State::STATE_LOOKUP_0A;
            }
        }
        else if (call.state == Call::State::STATE_INITIATION_WAIT) { 
            
            char addr[64];
            formatIPAddrAndPort((const sockaddr&)call.peerAddr, addr, 64);
            _log.info("Initiating a call %s -> %s (%s)", 
                call.localNumber.c_str(), call.remoteNumber.c_str(), addr); 
            
            // Put sequence back to the beginning and generate a new call ID
            // #### TODO: THIS IS PROBLEMATIC BECAUSE SOME THINGS IN THE 
            // #### STATE NEED TO BE ESTABLISHED IN THE call() FUNCTION
            // #### AND SOME THINGS HERE.  KEEP THE RECONNECT CASE IN MIND
            // #### (WHICH DOESN'T GO THROUGH call() AGAIN).
            call.outSeqNo = 0;
            call.expectedInSeqNo = 0;
            call.localCallId = _localCallIdCounter++;
            call.remoteCallId = 0;
            call.reTx.reset();

            // Make a NEW frame
            //
            // A NEW message MUST include the 'version' IE, and it MUST be the first
            // IE; the order of other IEs is unspecified.  A NEW SHOULD generally
            // include IEs to indicate routing on the remote peer, e.g., via the
            // 'called number' IE or to indicate a peer partition or ruleset, the
            // 'called context' IE.  Caller identification and CODEC negotiation IEs
            // MAY also be included.
            IAX2FrameFull frame;
            frame.setHeader(call.localCallId, call.remoteCallId, 
                call.dispenseElapsedMs(_clock), 
                call.outSeqNo, call.expectedInSeqNo, 
                FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_NEW);
            // From RFC: A NEW message MUST include the 'version' IE, and it MUST 
            // be the first IE; the order of other IEs is unspecified. 
            frame.addIE_uint16(IEType::IAX2_IE_VERSION, 0x0002);
            frame.addIE_str(1, call.remoteNumber);
            // CODECs are mapped to letters staring with "B". D means ulaw.
            // This code means SLIN16, SLIN8, ULAW
            frame.addIE_str(IEType::IAX2_IE_CODEC_PREFS, "QHD", 3);
            frame.addIE_str(2, call.localNumber);
            // Not sure what these are for?
            frame.addIE_uint8(38, 0x00);
            frame.addIE_uint8(39, 0x00);
            frame.addIE_uint16(40, 0x0000);
            // Unknown what this is, seen in WireShark when connecting to 55553
            // Per the IANA, this is unassigned.
            frame.addIE_uint32(57, 0x00000000);
            // Name of caller, not sent by Asterisk
            //frame.addIE_str(4, 0, 0);
            frame.addIE_str(10, "en", 2);
            frame.addIE_str(6, call.callUser);

            // Desired CODEC
            frame.addIE_uint32(IEType::IAX2_IE_FORMAT, call.desiredCodec);
            char desiredCodecWide[9];
            fillCodecWide(call.desiredCodec, desiredCodecWide);
            frame.addIE_str(0x38, desiredCodecWide, 9);        

            // CODEC capabilities
            frame.addIE_uint32(IEType::IAX2_IE_CAPABILITY, getSupportedCodecs());
            char capabilityCodecWide[9];
            fillCodecWide(getSupportedCodecs(), capabilityCodecWide);
            frame.addIE_str(0x37, capabilityCodecWide, 9);

            // CPE ADSI - "Analog display services interface (ADSI)"
            // TODO: WHAT IS THIS FOR?
            frame.addIE_uint16(12, 0x0002);
            frame.addIE_uint32(31, makeIAX2Time());
            // Setup the call token.  NOTE: This will be blank 
            // the first time through.
            frame.addIE_str(54, call.calltoken);        

            _sendFrameToPeer(frame, call);

            call.state = Call::State::STATE_WAITING;
            call._callInitiatedMs = _clock.time();
        }
        else if (call.state == Call::State::STATE_WAITING) { 
            // Check to see if we should give up on a new call that isn't
            // responding.
            // #### TODO: Investigate whether the REJECT/HANGUP cases are 
            // #### handled explicitly since the error message would be 
            // #### different in that case.
            if (_clock.isPast(call._callInitiatedMs + CALL_INITIATION_TIMEOUS_MS)) {
                _publishCallFailed(call.localNumber.c_str(),call.remoteNumber.c_str(), 
                    "Node not responding");
                _terminateCall(call);
                // #### WHY ISN'T THIS BEING SET IN ALL TERMINATED CASES?
                call.terminationMs = _clock.time();
            }
        }
    }
    else if (call.side == Call::Side::SIDE_CALLED) {
        if (call.state == Call::State::STATE_CALLER_VALIDATED) {

            // Send the ACCEPT message
            IAX2FrameFull acceptFrame;
            acceptFrame.setHeader(call.localCallId, call.remoteCallId, 
                call.dispenseElapsedMs(_clock), 
                call.outSeqNo, call.expectedInSeqNo, 
                FrameType::IAX2_TYPE_IAX, IAXSubclass::IAX2_SUBCLASS_IAX_ACCEPT);
            acceptFrame.addIE_uint32(IEType::IAX2_IE_FORMAT, call.codec);
            char codecWide[9];
            fillCodecWide(call.codec, codecWide);
            acceptFrame.addIE_str(IEType::IAX2_IE_FORMAT_WIDE, codecWide, 9);
            _sendFrameToPeer(acceptFrame, call);

            _log.info("Call %u accepted from %s %s using CODEC %08X",
                call.localCallId,
                call.remoteNumber.c_str(), call.callUser.c_str(), call.codec);

            call.trusted = true;
            call.state = Call::State::STATE_LINKED;

            PayloadCallStart payload;
            payload.codec = call.codec;
            payload.startMs = call.localStartMs;
            payload.sourceAddrValidated = call.sourceAddrValidated;
            strcpyLimited(payload.localNumber, call.localNumber.c_str(), sizeof(payload.localNumber));
            strcpyLimited(payload.remoteNumber, call.remoteNumber.c_str(), sizeof(payload.remoteNumber));
            payload.originated = false;

            MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
                sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
            msg.setSource(_busId, call.localCallId);
            msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(msg);
        }

        // In the ACCEPTED state we let the caller know 
        // and then switch into LINKED mode until the 
        // application accepts the call.
        else if (call.state == Call::State::STATE_LINKED) {

            // Send the ANSWER message
            IAX2FrameFull answerFrame;
            answerFrame.setHeader(call.localCallId, call.remoteCallId, 
                call.dispenseElapsedMs(_clock), 
                call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_CONTROL, 
                ControlSubclass::IAX2_SUBCLASS_CONTROL_ANSWER);
            _sendFrameToPeer(answerFrame, call);

            // Send the STOP_SOUNDS message
            IAX2FrameFull stopSoundsFrame;
            stopSoundsFrame.setHeader(call.localCallId, call.remoteCallId, 
                call.dispenseElapsedMs(_clock), 
                call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_CONTROL, 
                ControlSubclass::IAX2_SUBCLASS_CONTROL_STOP_SOUNDS);
            _sendFrameToPeer(stopSoundsFrame, call);

            call.state = Call::State::STATE_UP;
        }
    }

    // Doesn't matter whether we called or was called for these tasks.

    if (call.state == Call::State::STATE_TERMINATE_WAITING) {

        // Broadcast a message on the bus to inform listeners that 
        // the call was terminated.
        PayloadCallEnd payload;
        strcpyLimited(payload.localNumber, call.localNumber.c_str(), 
            sizeof(payload.localNumber));
        strcpyLimited(payload.remoteNumber, call.remoteNumber.c_str(), 
            sizeof(payload.remoteNumber));
        MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_END, 
            sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());            
        msg.setSource(_busId, call.localCallId);
        msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
        _bus.consume(msg);

        _terminateCall(call);
    }
    else if (call.state == Call::State::STATE_TERMINATED) {
        // Calls are allowed to hangout in terminated state
        // until the retransmit buffer is clear, or until
        // a timeout has expired.
        if (call.reTx.empty() || 
            _clock.isPast(call.terminationMs + TERMINATION_TIMEOUT_MS)) {
            _log.info("Call %u/%d has ended", call.localCallId, call.remoteCallId);
            call.reset();
        }
    }

    // If the state changes then its possible that more work is 
    // needed immediately.
    return call.state != originalState;
}

void LineIAX2::_hangupCall(Call& call) {
    IAX2FrameFull hangupFrame;
    hangupFrame.setHeader(call.localCallId, call.remoteCallId, 
        call.dispenseElapsedMs(_clock), 
        call.outSeqNo, call.expectedInSeqNo, IAX2_TYPE_IAX, IAX2_SUBCLASS_IAX_HANGUP);
    _sendFrameToPeer(hangupFrame, call);
    // Go into a state that will publish a CALL_END message
    call.state = Call::State::STATE_TERMINATE_WAITING;
}

void LineIAX2::_terminateCall(Call& call) {
    call.state = Call::State::STATE_TERMINATED;
    call.terminationMs = _clock.time();
}

void LineIAX2::_dtmfGen(char symbol) {
    // For now we send on all
    _visitActiveCallsIf(
        [this, symbol](Call& call) { 
            call.dtmfGen(this->_log, this->_clock, *this, symbol); 
        },
        [](const Call& call) { return true; }
    );
}

unsigned LineIAX2::getActiveCalls() const {
    unsigned result = 0;
    _visitActiveCallsIf(
        [&result](const Call& call) { result++; },
        [](const Call& call) { return true; }
    );
    return result;
}

/**
 * This function gets called when an audio/text message is available.
 */
void LineIAX2::consume(const Message& msg) {  

    // Look at for non-call signals
    if (msg.isSignal(Message::SignalType::DROP_ALL_CALLS)) {
        dropAllNonPermanent();
    } else if (msg.isSignal(Message::SignalType::DROP_ALL_CALLS_OUTBOUND)) {
        dropAllOutbound();
    } else if (msg.isSignal(Message::SignalType::DROP_CALL)) {
        dropCall(msg.getDestCallId());
    } 
    // This signal requests an outbound call 
    else if (msg.isSignal(Message::SignalType::CALL_NODE)) {
        PayloadCall* payload = (PayloadCall*)msg.body();
        assert(msg.size() == sizeof(PayloadCall));
        int rc = call(payload->localNumber, payload->targetNumber);
        if (rc != 0) {
            // If the call fails then build a message that contains
            // the details suitable for display to an end-user.
            if (rc == -4) {
                _publishCallFailed(payload->localNumber, payload->targetNumber,
                    "Node number syntax error");
            } else if (rc == -1) {
                _publishCallFailed(payload->localNumber, payload->targetNumber,
                    "Node already connected");
            } else if (rc == -2) {
                _publishCallFailed(payload->localNumber, payload->targetNumber,
                    "Call limit exceeded");
            } else if (rc == -5) {
                _publishCallFailed(payload->localNumber, payload->targetNumber,
                    "Address format error");
            } else {
                _publishCallFailed(payload->localNumber, payload->targetNumber,
                    "Other error");
            }
        }
    } else if (msg.isSignal(Message::SignalType::DTMF_GEN)) {
        
        // #### TODO: WE DON'T NEED A STRUCT FOR THIS!
        
        PayloadDtmfGen payload;
        assert(sizeof(payload) == msg.size());
        memcpy(&payload, msg.body(), msg.size());
        _dtmfGen(payload.symbol);
    }
    // Everything else gets handed to the calls for processing.
    else {
        _visitActiveCallsIf(
            // Visitor
            [line=this, &msg](Call& call) {
                if (msg.getType() == Message::Type::AUDIO) {

                    // At the moment we are assuming that the CODEC isn't changing 
                    // after negotiation.
                    if ((CODECType)msg.getFormat() != call.codec) {
                        line->_log.error("Voice frame with unexpected CODEC");
                        return;
                    }

                    // Figure out which kind of frame to send.  This follows 
                    // some tricky logic defined in RFC 5456 Section 8.1.2.
                    // 
                    // "Mini frames carry a 16-bit time-stamp, which is the lower 16 bits
                    // of the transmitting peer's full 32-bit time-stamp for the call.
                    // The time-stamp allows synchronization of incoming frames so that
                    // they MAY be processed in chronological order instead of the
                    // (possibly different) order in which they are received.  The 16-bit
                    // time-stamp wraps after 65.536 seconds, at which point a full frame
                    // SHOULD be sent to notify the remote peer that its time-stamp has
                    // been reset.  A call MUST continue to send mini frames starting
                    // with time-stamp 0 even if acknowledgment of the resynchronization
                    // is not received."
                    //
                    // In order to implement this we keep track of the 32-bit timestamp 
                    // of each voice frame sent (full or mini) and watch for the overflow 
                    // between the lower 16 and upper 16 bits of the timestamp.

                    // One other important thing here related to timing. The voice 
                    // frames are sent out with the elapsed time aligned to 20ms boundaries.
                    // We keep track of the previous elapsed time used to make sure that 
                    // we don't sent out two voice frames with the same timestamp.
                    //
                    uint32_t elapsed = call.dispenseElapsedMsForVoice(msg);
                    //line->_log.info("Send e %u", elapsed);
                    // The wrap case is identified by looking at the top 16 bits of the 
                    // last voice frame we transmitted.
                    bool hasWrapped = call.lastVoiceFrameElapsedMs == 0 || 
                        (elapsed & 0xffff0000) != (call.lastVoiceFrameElapsedMs & 0xffff0000);
                    // In the wrap case we send a full frame
                    if (hasWrapped) {
                        IAX2FrameFull voiceFrame;
                        voiceFrame.setHeader(call.localCallId, call.remoteCallId, 
                            elapsed, 
                            call.outSeqNo, call.expectedInSeqNo, 
                            FrameType::IAX2_TYPE_VOICE, call.codec);
                        voiceFrame.setBody(msg.body(), msg.size());
                        line->_sendFrameToPeer(voiceFrame, call);
                    }
                    // If no wrap then we can safely use a mini-frame
                    else {
                        // NOTE: Make this large enough for the biggest code!
                        const unsigned miniFrameMaxSize = 160 * 2 * 2 + 4;
                        assert(msg.size() <= miniFrameMaxSize - 4);
                        uint8_t miniFrame[miniFrameMaxSize];
                        // We make sure the F bit =0 to indicate a mini-frame
                        pack_uint16_be((0x7fff & call.localCallId), miniFrame);
                        // Send only the lower 16 bits of the timestamp per the spec
                        pack_uint16_be(0xffff & elapsed, miniFrame + 2);
                        memcpy(miniFrame + 4, msg.body(), msg.size());
                        line->_sendFrameToPeer(miniFrame, msg.size() + 4, 
                            (const sockaddr&)call.peerAddr);              
                    }

                    call.lastTxVoiceFrameMs = line->_clock.timeUs() / 1000;
                    call.lastVoiceFrameElapsedMs = elapsed;
                }
                else if (msg.getType() == Message::Type::SIGNAL) {
                    if (msg.getFormat() == Message::SignalType::CALL_TERMINATE) {
                        line->_hangupCall(call);
                    } 
                    // This is the case where an UNKEY is requested by something 
                    // on the internal bus. Create an UNKEY frame and send it out.
                    else if (msg.getFormat() == Message::SignalType::RADIO_UNKEY_GEN) {                        
                        //line->_log.info("Call %u sending unkey", call.localCallId);
                        IAX2FrameFull frame;
                        frame.setHeader(call.localCallId, call.remoteCallId, 
                            call.dispenseElapsedMs(line->_clock), 
                            call.outSeqNo, call.expectedInSeqNo, 
                            FrameType::IAX2_TYPE_CONTROL,
                            ControlSubclass::IAX2_SUBCLASS_CONTROL_UNKEY);
                        line->_sendFrameToPeer(frame, call);
                    }
                    // This is the case when the TALKERID is being asserted
                    else if (msg.getFormat() == Message::SignalType::CALL_TALKERID) {                        
                        // A text telemetry frame is used for this control
                        IAX2FrameFull frame;
                        frame.setHeader(call.localCallId, call.remoteCallId, 
                            call.dispenseElapsedMs(line->_clock), 
                            call.outSeqNo, call.expectedInSeqNo, 
                            FrameType::IAX2_TYPE_TEXT, 0);
                        // Pull out the talker ID
                        char talkerId[33];
                        strcpyLimited(talkerId, (const char*)msg.body(), sizeof(talkerId));
                        // Build a full text message
                        char text[128];
                        snprintf(text, sizeof(text), "T %s TALKERID,%s", 
                            call.localNumber.c_str(), talkerId);
                        // NOTE: Including null!
                        frame.setBody((const uint8_t*)text, strlen(text) + 1);
                        line->_sendFrameToPeer(frame, call);
                    }
                }
            },
            // Predicate (filters the calls)
            [&msg](const Call& call) {
                return call.state == Call::State::STATE_UP &&
                    msg.getDestCallId() == call.localCallId;
            }
        );
    }
}

void LineIAX2::_sendACK(uint32_t timeStamp, Call& call) {    
    // RFC Section 6.9.1: "And MUST return the same time-stamp it 
    // received.  This time-stamp allows the originating peer to 
    // determine to which message the ACK is responding."
    //
    // RFC Section 6.9.1: "It MUST also not change the sequence number
    // counters."
    //
    IAX2FrameFull frame;
    frame.setHeader(call.localCallId, call.remoteCallId, timeStamp,
        call.outSeqNo, call.expectedInSeqNo, FrameType::IAX2_TYPE_IAX, 
        IAXSubclass::IAX2_SUBCLASS_IAX_ACK);
    // Go straight to the wire, bypassing the retransmit buffer
    _sendFrameToPeer(frame, (const sockaddr&)call.peerAddr);
}

void LineIAX2::_sendREJECT(uint16_t destCall, const sockaddr& peerAddr, const char* cause) {
    IAX2FrameFull frame;
    frame.setHeader(0, destCall, 0, 0, 0, 6, 6);
    // See section 8.6.21
    frame.addIE_str(0x31, cause);
    _sendFrameToPeer(frame, peerAddr);
}

void LineIAX2::_sendFrameToPeer(const IAX2FrameFull& frame, Call& call) {    

    // Check to see if we even have an address for this peer. There are a few
    // situations where this can happen: (1) very early phase before the 
    // IP address of a call has come back (2) if a call that never gets 
    // an address goes through the hangup process.
    if (call.peerAddr.ss_family == 0) {
        _log.error("Call %d/%d unable to send %d/%d, no peer address",
            call.localCallId, call.remoteCallId, 
            (int)frame.getType(), (int)frame.getSubclass());
        return;
    }

    // Save the frame into the retransmission buffer for future use 
    // (just in case)
    if (!call.reTx.consume(frame)) {
        _log.error("Call %u/%u retx buffer error", call.localCallId, call.remoteCallId);
    }
    
    // Do the actual transmission on the socket
    _sendFrameToPeer(frame, (const sockaddr&)call.peerAddr);

    // Adjust the outbound sequence number as needed
    if (frame.shouldIncrementSequence())
        call.outSeqNo++;
}

void LineIAX2::_sendFrameToPeer(const IAX2FrameFull& frame, 
    const sockaddr& peerAddr) {

    if (_trace) {
        char addrStr[64];
        formatIPAddrAndPort(peerAddr, addrStr, 64);
        _log.info("Sending frame to %s", addrStr);
        char msg[64];
        snprintf(msg, 64, "====> SC=%d DC=%d OS=%d IS=%d TY=%d SC=%d %s",
            frame.getSourceCallId(), frame.getDestCallId(), 
            frame.getOSeqNo(), frame.getISeqNo(),
            frame.getType(), frame.getSubclass(),
            iax2TypeDesc(frame.getType(), frame.getSubclass()));
        _log.infoDump(msg, frame.buf(), frame.size());
    }

    _sendFrameToPeer(frame.buf(), frame.size(), peerAddr);
}

// NOTE: This is the ONLY place where IAX transmissions happen.

void LineIAX2::_sendFrameToPeer(const uint8_t* b, unsigned len, 
    const sockaddr& peerAddr) {

    if (!_iaxSockFd)
        return;

    int rc = ::sendto(_iaxSockFd, 
// Windows uses slightly different types on the socket calls
#ifdef _WIN32    
        (const char*)b,
#else
        b,
#endif
        len, 0, &peerAddr, getIPAddrSize(peerAddr));

    if (rc < 0) {
// No errno on Windows
#ifdef _WIN32
        if (WSAGetLastError() == WSAENETUNREACH) {
#else
        if (errno == 101) {
#endif        
            char temp[64];
            formatIPAddrAndPort(peerAddr, temp, 64);
            _log.error("Network is unreachable to %s", temp);
        } else 
            _log.error("Send error %d", errno);
    }

    // Take a look at the kernel buffer
    int bytes_in_buffer = 0;
    if (ioctl(_iaxSockFd, SIOCOUTQ, &bytes_in_buffer) == -1) {
    } else {
        if ((unsigned)bytes_in_buffer > ((_txSocketBufferSize * 3) / 4))
            _log.info("Socket transmit buffer >75 percent at %d", bytes_in_buffer);
    }
}

int LineIAX2::_sendDNSRequest(const uint8_t* dnsPacket, unsigned dnsPacketLen) {

    if (_trace)
        _log.infoDump("DNS request", dnsPacket, dnsPacketLen);

    // Send the query to a DNS server
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    inet_pton(AF_INET, DNS_IP_ADDR, &dest_addr.sin_addr); 
    int rc = ::sendto(_dnsSockFd, 
// Windows uses slightly different types on the socket calls
#ifdef _WIN32    
        (const char*)dnsPacket, 
#else
        dnsPacket, 
#endif
        dnsPacketLen, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (rc < 0)
        return rc;
    return 0;
}

int LineIAX2::_sendDNSRequestSRV(uint16_t requestId, const char* name) {    

    _log.info("Making DNS request (SRV) for %s", name);

    // Make the query
    const unsigned dnsPacketCapacity = 128;
    uint8_t dnsPacket[dnsPacketCapacity];
    int rc0 = microdns::makeDNSQuery_SRV(requestId, name, dnsPacket, dnsPacketCapacity);
    if (rc0 < 0) {
        _log.error("Unable to make DNS SRV query");
        return rc0;
    }
    unsigned dnsPacketLen = rc0;
    return _sendDNSRequest(dnsPacket, dnsPacketLen);
}

int LineIAX2::_sendDNSRequestA(uint16_t requestId, const char* name) {    

    _log.info("Making DNS request (A) for %s", name);

    // Make the query
    const unsigned dnsPacketCapacity = 128;
    uint8_t dnsPacket[dnsPacketCapacity];
    int rc0 = microdns::makeDNSQuery_A(requestId, name, dnsPacket, dnsPacketCapacity);
    if (rc0 < 0) {
        _log.error("Unable to make DNS A query");
        return rc0;
    }
    unsigned dnsPacketLen = rc0;
    return _sendDNSRequest(dnsPacket, dnsPacketLen);
}

int LineIAX2::_sendDNSRequestTXT(uint16_t requestId, const char* name) {    

    _log.info("Making DNS request (TXT for %s", name);

    // Make the query
    const unsigned dnsPacketCapacity = 128;
    uint8_t dnsPacket[dnsPacketCapacity];
    int rc0 = microdns::makeDNSQuery_TXT(requestId, name, dnsPacket, dnsPacketCapacity);
    if (rc0 < 0) {
        _log.error("Unable to make DNS TXT query");
        return rc0;
    }
    unsigned dnsPacketLen = rc0;
    return _sendDNSRequest(dnsPacket, dnsPacketLen);
}

void LineIAX2::oneSecTick() { 
    _visitActiveCallsIf(
        // This function will be called for each active call in the system.
        // (Even the terminated ones)
        [line=this](Call& call) {
            call.oneSecTick(line->_log, line->_clock, *line);
        },
        // Predicate
        [](const Call& call) { return true; }       
    );

    // Publish the status messages for each call. This is useful to keep 
    // UIs up to date
    _visitActiveCallsIf(
        // This function will be called for each active call in the system.
        // (Even the terminated ones)
        [this](Call& call) {
            PayloadCallStatus status;
            status.lastRxMs = call.lastRxVoiceFrameMs;
            status.lastTxMs = call.lastTxVoiceFrameMs;
            MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_STATUS, 
                sizeof(status), (const uint8_t*)&status, 0, 0);
            msg.setSource(_busId, call.localCallId);
            msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
            // #### TODO: NEED TO PUBLISH THIS SOMEWHERE!
        },
        // Predicate
        [](const Call& call) { return true; }
    );
}

void LineIAX2::tenSecTick() {

    _visitActiveCallsIf(
        [line = this](Call& call) {
            call.tenSecTick(line->_log, line->_clock, *line);
        },
        // Predicate
        [](const Call& call) { return true; }
    );

    // An optional feature to generate a POKE request out to an arbitrary server.
    // This would be used to keep a UDP firewall hole open.

    if (_pokeEnabled && _pokeAddr[0] != 0) {
        
        sockaddr_storage pokeAddr;
        parseIPAddrAndPort(_pokeAddr, pokeAddr);
        IAX2FrameFull poke2;
        poke2.setHeader(
            // Call IDs unused
            0, 0,
            // Keep passing through same time (for diagnostics)
            7777,
            // SEQ UNUSED
            0, 0, 
            FrameType::IAX2_TYPE_IAX, 
            IAXSubclass::IAX2_SUBCLASS_IAX_POKE);

        // NOTE: This isn't in the RFC, but we include the calling node in the 
        // poke message. This is useful as part of the firewall/CGNAT mitigation
        // strategy.
        if (_pokeNodeNumber[0] != 0)
            poke2.addIE_str(IEType::IAX2_IE_CALLING_NUMBER, _pokeNodeNumber);

        _sendFrameToPeer(poke2, (const sockaddr&)pokeAddr);
    }
}

void LineIAX2::_visitActiveCallsIf(std::function<void(LineIAX2::Call& call)> v,
    std::function<bool(const LineIAX2::Call& call)> predicate) {
    for (unsigned i = 0; i < _maxCalls; i++) {
        Call& call = _calls[i];
        if (call.active && predicate(call))
            v(call);
    }
}

void LineIAX2::_visitActiveCallsIf(std::function<void(const LineIAX2::Call& call)> v,
    std::function<bool(const LineIAX2::Call& call)> predicate) const {
    for (unsigned i = 0; i < _maxCalls; i++) {
        const Call& call = _calls[i];
        if (call.active && predicate(call))
            v(call);
    }
}

void LineIAX2::_publishCallFailed(const char* localNumber, const char* remoteNumber, 
    const char* text) {
    _log.info("Call %s->%s failed: %s", localNumber, remoteNumber, text);
    PayloadCallFailed payload;
    strcpyLimited(payload.targetNumber, remoteNumber, sizeof(payload.targetNumber));
    strcpyLimited(payload.message, text, sizeof(payload.message));
    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_FAILED, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_busId, Message::UNKNOWN_CALL_ID);
    msg.setDest(_destLineId, Message::UNKNOWN_CALL_ID);
    _bus.consume(msg);
}

// ===== LineIAX2::Call ===================================================

LineIAX2::Call::Call() { }

void LineIAX2::Call::reset() {

    resetStats();

    active = false;
    side = Side::SIDE_NONE;
    state = State::STATE_NONE;
    trusted = false;
    sourceAddrValidated = false;
    localCallId = 0;
    remoteCallId = 0;
    localStartMs = 0;
    lastElapsedMsDispensed = 0;
    outSeqNo = 0;
    expectedInSeqNo = 0;
    lastVoiceFrameElapsedMs = 0;
    localNumber.clear();
    remoteNumber.clear();
    callUser.clear();
    callPassword.clear();
    calltoken.clear();
    memset(publicKeyBin, 0, sizeof(publicKeyBin));
    memset(&peerAddr, 0, sizeof(peerAddr));
    desiredCodec = CODECType::IAX2_CODEC_SLIN_16K;
    codec = CODECType::IAX2_CODEC_UNKNOWN;
    lastFrameRxMs = 0;
    terminationMs = 0;
    networkDelayEstimateMs = 0;
    _ndi = 0;
    _ndi_1 = 0;
    _nvi = 0;
    _nvi_1 = 0;
    reTx.reset();
    dnsRequestId = 0;
    lastPingSentMs = 0;
    lastPingTimeMs = 0;
    pingCount = 0;
    lastLagMs = 0;
    lastLagrqMs = 0;
    lastRxVoiceFrameMs = 0;
    lastTxVoiceFrameMs = 0;
    _callInitiatedMs = 0;
    _rxSeqErrorCount = 0;
}

// #### TODO: THINK ABOUT THE NEGATIVE CASE HERE?
uint32_t LineIAX2::Call::localElapsedMs(Clock& clock) const {
    return clock.time() - localStartMs;
}

// #### TODO: THINK ABOUT THE NEGATIVE CASE HERE?
uint32_t LineIAX2::Call::dispenseElapsedMs(Clock& clock) {
    uint32_t currentTickStartMs = alignToTick(localElapsedMs(clock), AUDIO_TICK_MS);
    // Give out the timestamp that is as close as possible to the
    // start of the tick, but never go backwards.
    lastElapsedMsDispensed = max(lastElapsedMsDispensed + 1, currentTickStartMs);
    return lastElapsedMsDispensed;
}

uint32_t LineIAX2::Call::dispenseElapsedMsForVoice(const Message& msg) {
    uint32_t voiceElapsedMs = alignToTick(msg.getRxMs() - localStartMs, AUDIO_TICK_MS);
    // Give out the timestamp that is as close as possible to the
    // start of the tick, but never go backwards.
    lastElapsedMsDispensed = max(lastElapsedMsDispensed + 1, voiceElapsedMs);
    return lastElapsedMsDispensed;
}

bool LineIAX2::Call::isPeerAddr(const sockaddr& addr) const {
    return equalIPAddr(addr, (const sockaddr&)peerAddr);
    // Changed on 19-Nov-25 after problems on MacOS
    //return ((sockaddr_in&)addr).sin_addr.s_addr == 
    //    ((sockaddr_in&)peerAddr).sin_addr.s_addr;
}

void LineIAX2::Call::resetStats() {
}

void LineIAX2::Call::setNetworkDelayEstimate(unsigned ms, bool first) {
    float nni = ms;
    // On the first ping we lock in first delay value
    if (first == 1) {
        _ndi_1 = nni;
        _nvi_1 = 0;
    }
    // Update moving delay
    _ndi = _nAlpha * _ndi_1 + (1 - _nAlpha) * nni;
    _ndi_1 = _ndi;
    // Update moving variance, not used at the moment
    _nvi = _nAlpha * _nvi_1 + (1 - _nAlpha) * fabs(_ndi - nni);
    _nvi_1 = _nvi;
    networkDelayEstimateMs = _ndi;
}

/**
 * Take care of background, non-time-sensitive operations.
 */
void LineIAX2::Call::oneSecTick(Log& log, Clock& clock, LineIAX2& line) {

    // The PING/LAGRQ is only performed on calls that are fully up.
    // 10-Feb-2026 Bruce saw some problems where the PING was getting 
    // generated before a call had even received the peer's IP address
    // which was creating problems.

    // There's also the issue of terminated calls. The way things are 
    // currently setup the pings will not be sent to calls that are in 
    // the process of terminating.

    if (state == State::STATE_UP) {

        // Need a ping?
        uint32_t pingTargetMs;
        // IMPORTANT: I noticed that sending a ping too early in the 
        // call process resulted in the remote side hanging up.
        // (Seen at 17:01 PM on 16-Nov-2025)
        if (lastPingSentMs == 0)
            pingTargetMs = localStartMs + NORMAL_PING_INTERVAL_MS;
        // We are more agressive at the start to improve our understanding
        // of the network latency of the connection.
        else if (pingCount < 5)
            pingTargetMs = lastPingSentMs + FAST_PING_INTERVAL_MS;
        else 
            pingTargetMs = lastPingSentMs + NORMAL_PING_INTERVAL_MS;

        if (clock.isPast(pingTargetMs)) {
            IAX2FrameFull pingFrame;
            pingFrame.setHeader(localCallId, remoteCallId, 
                dispenseElapsedMs(clock), 
                outSeqNo, expectedInSeqNo, 6, 2);
            line._sendFrameToPeer(pingFrame, *this);
            lastPingSentMs = clock.time();
        }

        // Need a LAGRQ?
        if (clock.isPast(lastLagrqMs + LAGRQ_INTERVAL_MS)) {
            // 6.7.4.  LAGRQ Lag Request Message
            // A LAGRQ is a lag request.  It is sent to determine the lag between
            // two IAX endpoints, including the amount of time used to process a
            // frame through a jitter buffer (if any).  It requires a clock-based
            // time-stamp, and MUST be answered with a LAGRP, which MUST echo the
            // LAGRQ's time-stamp.  The lag between the two peers can be computed on
            // the peer sending the LAGRQ by comparing the time-stamp of the LAGRQ
            // and the time the LAGRP was received.
            // This message does not require any IEs.
            //
            IAX2FrameFull lagrqFrame;
            lagrqFrame.setHeader(localCallId, remoteCallId, 
                dispenseElapsedMs(clock), 
                outSeqNo, expectedInSeqNo, 6, 0x0b);
            line._sendFrameToPeer(lagrqFrame, *this);
            lastLagrqMs = clock.time();
        }
    }

    // Inactive call?
    if (state != Call::State::STATE_TERMINATED &&
        state != Call::State::STATE_TERMINATE_WAITING) {
        if (clock.isPast(lastFrameRxMs + _inactivityTimeoutMs)) {
            log.info("Hanging up inactive call %d/%d", 
                localCallId, remoteCallId);
            line._hangupCall(*this);
        }
        else if (reTx.getUsed() == reTx.getCapacity()) {
            log.info("Hanging up call with full retransmit buffer %d/%d", 
                localCallId, remoteCallId);
            line._hangupCall(*this);
        }
    }

    // Look for things waiting to go out on the network (like retransmits)
    reTx.retransmitIfNecessary(localElapsedMs(clock), expectedInSeqNo, 
        // The callback that will be fired for anything that reTx needs to send
        [this, &context=line](const IAX2FrameFull& frame) {
            context._sendFrameToPeer(frame, (const sockaddr&)peerAddr);
        });

}

void LineIAX2::Call::tenSecTick(Log& log, Clock& clock, LineIAX2& line) {

    resetStats();

    // Generate the L message with the list of linked nodes
    // #### TODO: CONSIDER WHETHER THIS SHOULD BE INITIATED IN BRIDGE?

    if (state == Call::State::STATE_UP) {

        // NOTE: There is a limit to the number of nodes that can be reported here.
        // Anything above the limit will be quietly dropped.
        const unsigned textBufferCapacity = IAX2FrameFull::MAX_BODY_LEN;
        char textBuffer[textBufferCapacity];

        strcpy(textBuffer, "L ");
        unsigned textBufferLen = 2;

        line._visitActiveCallsIf(
            // Visitor
            [&textBuffer, textBufferCapacity, &textBufferLen](const Call& call) {
                // This is a comma-separated list
                char linkNode[128];
                snprintf(linkNode, sizeof(linkNode), "T%s,", call.remoteNumber.c_str());
                if (textBufferLen + strlen(linkNode) < textBufferCapacity) {
                    strcpy(textBuffer + textBufferLen, linkNode);
                    textBufferLen += strlen(linkNode);
                }
            },
            // Predicate
            [](const Call& call) {
                return true;
            }
        );
        // Add terminating null which will cover the last comma
        textBuffer[textBufferLen - 1] = 0;   

        IAX2FrameFull respFrame2;
        respFrame2.setHeader(localCallId, remoteCallId, 
            dispenseElapsedMs(clock), 
            outSeqNo, expectedInSeqNo, FrameType::IAX2_TYPE_TEXT, 0);
        respFrame2.setBody((const uint8_t*)textBuffer, textBufferLen);
        line._sendFrameToPeer(respFrame2, *this);
    }
}

void LineIAX2::Call::logStats(Log& log) {
}

void LineIAX2::Call::dtmfGen(Log& log, Clock& clock, LineIAX2& line, char symbol) {
    log.info("Call %u sending DTMF %c", localCallId, symbol);
    IAX2FrameFull frame;
    frame.setHeader(localCallId, remoteCallId, 
        dispenseElapsedMs(clock), 
        outSeqNo, expectedInSeqNo, FrameType::IAX2_TYPE_DTMF2, symbol);
    line._sendFrameToPeer(frame, *this);
}

}
