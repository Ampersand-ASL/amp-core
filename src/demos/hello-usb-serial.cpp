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

#include <termios.h>
#include <linux/serial.h>
#include  <sys/ioctl.h>

#include "kc1fsz-tools/linux/StdClock.h"
#include "kc1fsz-tools/StdPollTimer.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Common.h"

#include "DigitalAudioPortRxHandler.h"

#include "crc.h"
#include "cobs.h"

/* 
[1230981.502074] usb 1-2: new full-speed USB device number 26 using xhci-hcd
[1230981.660464] usb 1-2: New USB device found, idVendor=067b, idProduct=23a3, bcdDevice= 1.05
[1230981.660471] usb 1-2: New USB device strings: Mfr=1, Product=2, SerialNumber=3
[1230981.660473] usb 1-2: Product: USB-Serial Controller
[1230981.660475] usb 1-2: Manufacturer: Prolific Technology Inc.
[1230981.660476] usb 1-2: SerialNumber: CIBYb137X02
[1230981.667574] pl2303 1-2:1.0: pl2303 converter detected
[1230981.667690] usb 1-2: pl2303 converter now attached to ttyUSB0
*/

// https://www.prolific.com.tw/wp-content/uploads/2025/07/DS-23181003_PL2303GT_V1.0.2.pdf

//#define NETWORK_BAUD (B1152000)
#define NETWORK_BAUD (B460800)
#define BLOCK_SIZE (160)

using namespace std;
using namespace kc1fsz;

// https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/

int main(int, const char**) {

    int serial_port = open("/dev/ttyUSB0", O_RDWR | O_NONBLOCK | O_NOCTTY);
    cout << serial_port << endl;

    Log log;
    StdClock clock;
    StdPollTimer timer(clock, 20 * 1000);

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
    cout << "Speed " << (int)cfgetospeed(&tty) << endl;
    cout << "? " << NETWORK_BAUD << endl;

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

    // Configure port to use custom speed instead of 38400
    /*
    int speed = 1152000;
    serial_struct ss;
    ioctl(serial_port, TIOCGSERIAL, &ss);
    ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
    ss.custom_divisor = (ss.baud_base + (speed / 2)) / speed;
    int closestSpeed = ss.baud_base / ss.custom_divisor;
    cout << "B_B " << ss.baud_base << endl;

    if (closestSpeed < speed * 98 / 100 || closestSpeed > speed * 102 / 100) {
        fprintf(stderr, "Cannot set serial port speed to %d. Closest possible is %d\n", speed, closestSpeed);
    }
    ioctl(serial_port, TIOCSSERIAL, &ss);
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
    */
    
    // Specifying a custom baud rate when using GNU C
    if (cfsetispeed(&tty, NETWORK_BAUD) != 0)
        log.error("Invalid baud %d", NETWORK_BAUD);
    if (cfsetospeed(&tty, NETWORK_BAUD) != 0)
        log.error("Invalid baud %d", NETWORK_BAUD);
    
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr\n", errno);
    }

    //if (tcgetattr(serial_port, &tty) != 0) {
    //    printf("Error %i from tcgetattr\n", errno);
    //}
    //cout << "Speed " << (int)cfgetospeed(&tty) << endl;
    //cout << "? " << (int)B230400 << endl;

    // Switch into streaming mode
    uint8_t packet[NETWORK_MESSAGE_SIZE];
    packet[0] = 4;
    int rc0 = write(serial_port, packet, 1);
    if (rc0 != 1) {
        log.error("Failed to send switch command");
        return -1;
    }
    sleep(1);

    int sendCount = 0;
    char read_buf[1024];
    float phi = 0;
    float omega = 2.0f * 3.1415926f * 800.0f / 8000.0f;
    int recBytes = 0;

    timer.reset();

    while (true) {

        if (timer.poll() && sendCount < 50) {

            // Make a PCM tone in 20ms increments, phase continuous
            uint8_t msg[PAYLOAD_SIZE];
            uint8_t* p = msg;
            for (unsigned i = 0; i < BLOCK_SIZE; i++, p += 2) {
                float v = 0.5 * std::cos(phi);
                phi += omega;
                int16_t pcm = v * 32767.0f;
                pack_int16_le(pcm, p);
            }
            phi = fmod(phi, 2.0f * 3.1415926f);

            DigitalAudioPortRxHandler::encodeMsg(msg, PAYLOAD_SIZE,
                packet, NETWORK_MESSAGE_SIZE);
                
            int rc1 = write(serial_port, packet, NETWORK_MESSAGE_SIZE);
            if (rc1 != NETWORK_MESSAGE_SIZE)
                log.error("Write failed %d", rc1);
            else {
                log.info("Sent %d", sendCount);
                sendCount++;
            }
        }

        int rc2 = read(serial_port, read_buf, 256);
        if (rc2 == 0) {
        }
        else if (rc2 < 0) {
            log.error("Read error %d", rc2);
        } else {
            recBytes += rc2;
            prettyHexDump((const uint8_t*)read_buf, rc2, cout);
            log.info("Bytes %d", recBytes);
        }
    }
}
