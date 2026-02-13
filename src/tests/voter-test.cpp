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

static void voterTest1() {

    // Validate CRC code
    uint32_t r1 = VoterUtil::crc32("IzzyHenry");
    assert(r1 == 3749699513);
}

/*
This tests sets up a server and a client and has them exchange a few messages
to validate whether the authentication handshake is working.
*/
static void voterTest2() {

    string serverPwd = "server123";
    string client0Pwd = "client000";

    amp::VoterPeer server;
    server.setLocalPassword(serverPwd.c_str());
    server.setRemotePassword(client0Pwd.c_str());

    amp::VoterPeer client0;
    client0.setLocalPassword(client0Pwd.c_str());
    client0.setRemotePassword(serverPwd.c_str());

    uint8_t serverOut[256];
    unsigned serverOutLen;

    uint8_t client0Out[256];
    unsigned client0OutLen;

    // Hook the server/client up to some buffers so we can look at the outbound traffic
    server.setSink([&serverOut, &serverOutLen](const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        memcpy(serverOut, data, dataLen);
        serverOutLen = dataLen;
    });
    client0.setSink([&client0Out, &client0OutLen](const sockaddr& addr, const uint8_t* data, unsigned dataLen) {
        assert(dataLen <= 256);
        memcpy(client0Out, data, dataLen);
        client0OutLen = dataLen;
    });
}

int main(int, const char**) {
    voterTest1();
    voterTest2();
}