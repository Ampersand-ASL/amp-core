#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <errno.h>
#include <cstring>

#include <iostream>
#include <cmath>
#include <cassert>

#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Common.h"
#include "cobs.h"

#define NETWORK_BAUD (1152000)
// Fixed size, 1 header null + audio + 2 COBS overhead
#define NETWORK_MESSAGE_SIZE (1 + (160 * 2) + 2)
#define BLOCK_SIZE (160)

using namespace std;
using namespace kc1fsz;

// https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/

int main(int, const char**) {

    int serial_port = open("/dev/ttyUSB0", O_RDWR | O_NONBLOCK);
    cout << serial_port << endl;

    // Create new termios struct, we call it 'tty' for convention
    // No need for "= {0}" at the end as we'll immediately write the existing
    // config to this struct
    struct termios tty;

    // Read in existing settings, and handle any error
    // NOTE: This is important! POSIX states that the struct passed to tcsetattr()
    // must have been initialized with a call to tcgetattr() overwise behaviour
    // is undefined
    if (tcgetattr(serial_port, &tty) != 0) {
        printf("Error %i from tcgetattr\n", errno);
    }
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE; 
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS; 
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;

    // Specifying a custom baud rate when using GNU C
    cfsetispeed(&tty, NETWORK_BAUD);
    cfsetospeed(&tty, NETWORK_BAUD);

    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr\n", errno);
    }

    uint8_t packet[NETWORK_MESSAGE_SIZE];
    Log log;
    StdClock clock;
    StdPollTimer timer(clock, 20 * 1000);

    // Switch into streaming mode
    packet[0] = 'a';
    int rc0 = write(serial_port, packet, 1);
    if (rc0 != 1) {
        log.error("Failed to send switch command");
        return -1;
    }
    sleep(1);

    int sendCount = 0;
    int recPtr = 0;
    char read_buf[256];
    float phi = 0;
    float omega = 2.0f * 3.1415926f * 800.0f / 8000.0f;

    while (true) {

        if (timer.poll()) {

            if (recPtr > 0) 
                log.info("Received frame %d", recPtr);
            //    prettyHexDump((const uint8_t*)read_buf, recCount, cout);

            // Make a PCM tone in 20ms increments, phase continuous
            uint8_t msg[BLOCK_SIZE * 2];
            uint8_t* p = msg;
            for (unsigned i = 0; i < BLOCK_SIZE; i++, p += 2) {
                float v = 0.5 * std::cos(phi);
                phi += omega;
                int16_t pcm = v * 32767.0f;
                pack_int16_le(pcm, p);
            }
            phi = fmod(phi, 2.0f * 3.1415926f);

            if (sendCount < 100) {
                // Header
                packet[0] = 0;
                cobs_encode_result re = cobs_encode(packet + 1, NETWORK_MESSAGE_SIZE - 1, 
                    msg, BLOCK_SIZE * 2);
                assert(re.status == COBS_ENCODE_OK);
                assert(re.out_len <= NETWORK_MESSAGE_SIZE - 1);
                int rc1 = write(serial_port, packet, NETWORK_MESSAGE_SIZE);
                if (rc1 != NETWORK_MESSAGE_SIZE)
                    log.error("Write failed %d", rc1);
                sendCount++;
            }

            recPtr = 0;
            memset(read_buf, 0, 256);
        }

        int rc2 = read(serial_port, read_buf + recPtr, 256);
        if (rc2 == 0) {
        }
        else if (rc2 < 0) {
            log.error("Read error %d", rc2);
        } else {
            recPtr += rc2;
        }
    }
}