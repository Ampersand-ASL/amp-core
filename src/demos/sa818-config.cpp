#include <unistd.h>
#include <fcntl.h>

#include <iostream>
#include <cstring>
#include <cassert>

#include <kc1fsz-tools/Log.h>

#include "SerialUtil.h"

using namespace std;
using namespace kc1fsz;

int pull(Log& log, int fd) {

    for (unsigned i = 0; i < 2; i++) {

        char buf[128];
        int rc = read(fd, buf, sizeof(buf));
        if (rc < 0) {
            cout << "ERROR 3" << endl;
            return -1;
        }

        log.infoDump("Return", (const uint8_t*)buf, rc);

        sleep(1);
    }
    return 0;
}

int test1() {

    Log log;

    const char* serialDevice = "/dev/ttyUSB0";

    //int fd = ::open(serialDevice, O_RDWR | O_NONBLOCK | O_NOCTTY);
    int fd = ::open(serialDevice, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        cout << "ERROR 1" << endl;
        return -1;
    }

    int rc = SerialUtil::configurePort(fd, 9600);
    if (rc < 0) {
        cout << "ERROR 2" << endl;
        return -1;
    }

    const char* cmd0 = "AT+DMOCONNECT\r\n";
    rc = write(fd, cmd0, strlen(cmd0));
    if (rc < 0) {
        cout << "ERROR 3" << endl;
        return -1;
    }
    pull(log, fd);

    const char* cmd1 = "AT+DMOSETVOLUME=8\r\n";
    rc = write(fd, cmd1, strlen(cmd1));
    if (rc < 0) {
        cout << "ERROR 4" << endl;
        return -1;
    }
    pull(log, fd);

    const char* ctcss[] = { "None", "67.0", "71.9", "74.4", "77.0", "79.7", "82.5", "85.4", "88.5",
  "91.5", "94.8", "97.4", "100.0", "103.5", "107.2", "110.9", "114.8", "118.8",
  "123.0", "127.3", "131.8", "136.5", "141.3", "146.2", "151.4", "156.7",
  "162.2", "167.9", "173.8", "179.9", "186.2", "192.8", "203.5", "210.7",
  "218.1", "225.7", "233.6", "241.8", "250.3" };

    for (unsigned i = 0; i < std::size(ctcss); i++) {
        char value[8];
        sprintf(value, "%04d", i);
        cout << "<option value=\"" << value << "\">" << ctcss[i] << "</option>" << endl;
    }

    const char* cmd2 = "AT+DMOSETGROUP=1,446.0500,446.0500,0003,4,0003\r\n";
    rc = write(fd, cmd2, strlen(cmd2));
    if (rc < 0) {
        cout << "ERROR 5" << endl;
        return -1;
    }
    pull(log, fd);

    return 0;
}

static int test2() {
    Log log;
    const char* serialDevice = "/dev/ttyUSB0";
    int rc = SerialUtil::configureSA818(log, serialDevice, 1, 4460500, 4460500, 0, 0, 4, 8);
    cout << "RC " << rc << endl;
    assert(rc == 0);
    return 0;
}

int main(int, const char**) {
    test1();
    test2();
}
