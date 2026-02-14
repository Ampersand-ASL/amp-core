#include <iostream>
#include <cmath> 
#include <ctime>
#include <fstream>
#include <cassert>
#include <ctime>
#include <cstring>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/NetUtils.h"

#include "VoterUtil.h"
#include "VoterPeer.h"
#include "TestUtil.h"

using namespace std;
using namespace kc1fsz;

static void voterTest0() {

    // Validate CRC code
    uint32_t r1 = VoterUtil::crc32("IzzyHenry");
    assert(r1 == 3749699513);
}

/*
This tests sets up a server and a client and has them exchange a few messages
to validate whether the authentication handshake is working.
*/
static void voterTest1() {

    StdClock clock;
    Log log;

    log.info("Voter protocol implementation unit test 1");
    log.info("Basic handshake and authentication");
    log.info("");

    string serverPwd = "server123";
    string serverChallenge = VoterPeer::makeChallenge();
    string client0Pwd = "client000";

    amp::VoterPeer server0;
    server0.init(&clock, &log);
    server0.setLocalPassword(serverPwd.c_str());
    server0.setLocalChallenge(serverChallenge.c_str());
    server0.setRemotePassword(client0Pwd.c_str());

    amp::VoterPeer client0;
    client0.init(&clock, &log);
    client0.setLocalPassword(client0Pwd.c_str());
    client0.setRemotePassword(serverPwd.c_str());

    bool server0Sent = false;
    uint8_t server0Out[256];
    unsigned server0OutLen;

    bool client0Sent = false;
    uint8_t client0Out[256];
    unsigned client0OutLen;

    // Hook the server/client up to some buffers so we can look at the outbound traffic
    server0.setSink([&log, &server0Sent, &server0Out, &server0OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        server0Sent = true;
        memcpy(server0Out, data, dataLen);
        server0OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Server sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });
    client0.setSink([&log, &client0Sent, &client0Out, &client0OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        client0Sent = true;
        memcpy(client0Out, data, dataLen);
        client0OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Client sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });

    sockaddr_storage serverAddr;
    serverAddr.ss_family = AF_INET;
    setIPAddr(serverAddr,"1.1.1.1");
    setIPPort(serverAddr, 1667);

    sockaddr_storage client0Addr;
    client0Addr.ss_family = AF_INET;
    setIPAddr(client0Addr,"2.2.2.2");
    setIPPort(client0Addr, 1667);

    // Give the client the server's address
    client0.setPeerAddr(serverAddr);

    assert(!client0.isPeerTrusted());
    assert(!server0.isPeerTrusted());

    // ----- Tick ----------------------------------------------------------
    // 1. The client should send a challenge to the server
    // 2. The server shouldn't do anything
    log.info("Tick 0");
    server0.oneSecTick();
    client0.oneSecTick();

    // Server is quiet at the start
    assert(!server0Sent);
    // The client starts by sending a type 0 packet
    assert(client0Sent);

    // The client's message shouldn't belong to server0 yet
    assert(!server0.belongsTo(client0Out, client0OutLen));

    // Make a response to send back to the potential new client
    int rc = VoterPeer::makeInitialChallengeResponse(client0Out,
        serverChallenge.c_str(), serverPwd.c_str(),
        server0Out);
    assert(rc == 24);
    server0OutLen = rc;
    log.info("Server is sending initial challenge response:");
    log.infoDump("", server0Out, server0OutLen);

    // The client should be satisfied with the server now
    client0.consumePacket((const sockaddr&)serverAddr, server0Out, server0OutLen);
    assert(client0.isPeerTrusted());

    // Since the client is happy it sends some audio to the server.
    uint8_t testAudio[160];
    client0.sendAudio(0x77, testAudio, sizeof(testAudio));

    // The server should now see that the message belongs to server0
    assert(!server0.isPeerTrusted());
    assert(server0.belongsTo(client0Out, client0OutLen));
    
    // The server should be happy now
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);
    assert(server0.isPeerTrusted());
   
    // ----- Tick ----------------------------------------------------------
    log.info("Tick 1");
    server0Sent = false;
    client0Sent = false;
    server0.oneSecTick();
    client0.oneSecTick();

    // Nobody should be sending spontaneous messages yet
    assert(!server0Sent);
    assert(!client0Sent);

    // ----- Tick ----------------------------------------------------------
    log.info("Tick 2 (10 second)");
    server0Sent = false;
    client0Sent = false;
    server0.oneSecTick();
    client0.oneSecTick();
    server0.tenSecTick();
    client0.tenSecTick();

    // Should see pings in both directions
    assert(server0Sent);
    assert(client0Sent);

    server0Sent = false;
    client0Sent = false;

    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);

    // Should see the ping response
    assert(server0Sent);
}

static void voterTest2() {

    StdClock clock;
    Log log;

    log.info("Voter protocol implementation unit test 2");
    log.info("Audio test");
    log.info("");

    amp::VoterPeer server0;
    server0.init(&clock, &log);

    amp::VoterPeer server1;
    server1.init(&clock, &log);

    amp::VoterPeer client0;
    client0.init(&clock, &log);

    amp::VoterPeer client1;
    client1.init(&clock, &log);

    // Setup so we can skip past all handshaking
    string serverPwd = "server123";
    string client0Pwd = "client000";
    string client1Pwd = "client001";

    string serverChallenge = VoterPeer::makeChallenge();
    string client0Challenge = VoterPeer::makeChallenge();
    string client1Challenge = VoterPeer::makeChallenge();

    server0.setLocalPassword(serverPwd.c_str());
    server0.setRemotePassword(client0Pwd.c_str());
    server1.setLocalPassword(serverPwd.c_str());
    server1.setRemotePassword(client1Pwd.c_str());

    client0.setLocalPassword(client0Pwd.c_str());
    client0.setRemotePassword(serverPwd.c_str());
    client1.setLocalPassword(client1Pwd.c_str());
    client1.setRemotePassword(serverPwd.c_str());

    server0.setLocalChallenge(serverChallenge.c_str());
    server0.setRemoteChallenge(client0Challenge.c_str());
    server1.setLocalChallenge(serverChallenge.c_str());
    server1.setRemoteChallenge(client1Challenge.c_str());
    client0.setLocalChallenge(client0Challenge.c_str());
    client0.setRemoteChallenge(serverChallenge.c_str());
    client1.setLocalChallenge(client1Challenge.c_str());
    client1.setRemoteChallenge(serverChallenge.c_str());

    server0.setPeerTrusted(true);
    server1.setPeerTrusted(true);
    client0.setPeerTrusted(true);
    client1.setPeerTrusted(true);

    bool server0Sent = false;
    uint8_t server0Out[256];
    unsigned server0OutLen;

    bool server1Sent = false;
    uint8_t server1Out[256];
    unsigned server1OutLen;

    bool client0Sent = false;
    uint8_t client0Out[256];
    unsigned client0OutLen;

    bool client1Sent = false;
    uint8_t client1Out[256];
    unsigned client1OutLen;

    // Hook the server/client up to some buffers so we can look at the outbound traffic
    server0.setSink([&log, &server0Sent, &server0Out, &server0OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        server0Sent = true;
        memcpy(server0Out, data, dataLen);
        server0OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Server 0 sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });
    server1.setSink([&log, &server1Sent, &server1Out, &server1OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        server1Sent = true;
        memcpy(server1Out, data, dataLen);
        server1OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Server 1 sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });
    client0.setSink([&log, &client0Sent, &client0Out, &client0OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        client0Sent = true;
        memcpy(client0Out, data, dataLen);
        client0OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Client 0 sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });
    client1.setSink([&log, &client1Sent, &client1Out, &client1OutLen]
        (const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        client1Sent = true;
        memcpy(client1Out, data, dataLen);
        client1OutLen = dataLen;
        // Tracing
        char addrStr[64];
        formatIPAddrAndPort(addr, addrStr, sizeof(addrStr));
        char msg[256];
        snprintf(msg, sizeof(msg), "Client 1 sending to %s:", addrStr);
        log.infoDump(msg, data, dataLen);
    });

    sockaddr_storage serverAddr;
    serverAddr.ss_family = AF_INET;
    setIPAddr(serverAddr,"1.1.1.1");
    setIPPort(serverAddr, 1667);

    sockaddr_storage client0Addr;
    client0Addr.ss_family = AF_INET;
    setIPAddr(client0Addr,"2.2.2.2");
    setIPPort(client0Addr, 1667);

    sockaddr_storage client1Addr;
    client1Addr.ss_family = AF_INET;
    setIPAddr(client1Addr,"3.3.3.3");
    setIPPort(client1Addr, 1667);

    // Skip past the authentication
    client0.setPeerAddr(serverAddr);
    client1.setPeerAddr(serverAddr);
    server0.setPeerAddr(client0Addr);
    server1.setPeerAddr(client1Addr);

    // Send some audio client0->server0
    uint8_t testAudio[160];
    memset(testAudio, 0xab, 160);

    client0.sendAudio(0x77, testAudio, sizeof(testAudio));
    assert(server0.isPeerTrusted());
    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);

    // Make sure all authentication is right
    assert(server0.isPeerTrusted());
    assert(client0.isPeerTrusted());

    // Shouldn't see the audio on the conference side yet (delayed playout)
    assert(server0.getRSSI(1000) == 0);

    // ----- Tick -----------------------------------------------------
    uint32_t t = 1000;
    server0.audioRateTick(t);
    client0.audioRateTick(t);

    // Contribute the next audio frame
    client0.sendAudio(0x78, testAudio, sizeof(testAudio));
    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);

    // Shouldn't see the audio on the conference side yet (delayed playout)
    assert(server0.getRSSI(t) == 0);

    // ----- Tick -----------------------------------------------------
    t = 1020;
    server0.audioRateTick(t);
    client0.audioRateTick(t);

    // NOTE: CONTRIBUTION GAP

    // Should see the audio on the conference side now (first frame)
    assert(server0.getRSSI(t) == 0x77);

    // ----- Tick -----------------------------------------------------
    t = 1040;
    server0.audioRateTick(t);
    client0.audioRateTick(t);

    // NOTE: CONTRIBUTION GAP

    // Should see the audio on the conference side now (second frame)
    assert(server0.getRSSI(t) == 0x78);

    // ----- Tick -----------------------------------------------------
    // Should have seen 79, but it's late
    t = 1060;
    server0.audioRateTick(t);
    client0.audioRateTick(t);

    // Gap
    assert(server0.getRSSI(1000) == 0);

    // ----- Tick -----------------------------------------------------
    // Should have seen 80, but it's late
    t = 1060;
    server0.audioRateTick(1000);
    client0.audioRateTick(1000);

    // Contribute the next audio frame (late)
    client0.sendAudio(0x79, testAudio, sizeof(testAudio));
    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);

    // Gap
    assert(server0.getRSSI(1000) == 0);

    // ----- Tick -----------------------------------------------------
    // Should see 81. Notice that 0x79 and 0x80 were flushed because
    // they were too late to be used.
    t = 1080;
    server0.audioRateTick(t);
    client0.audioRateTick(t);

    // Contribute the next audio frame (late)
    client0.sendAudio(0x80, testAudio, sizeof(testAudio));
    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);

    // Contribute the next audio frame (late)
    client0.sendAudio(0x81, testAudio, sizeof(testAudio));
    assert(server0.belongsTo(client0Out, client0OutLen));
    server0.consumePacket((const sockaddr&)client0Addr, client0Out, client0OutLen);


    assert(server0.getRSSI(t) == 0x81);
}

int main(int, const char**) {
    voterTest0();
    voterTest1();
    voterTest2();
}