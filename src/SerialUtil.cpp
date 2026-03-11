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
#include <termios.h>
#include <linux/serial.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <cstring>
#include <iostream>

#include "kc1fsz-tools/Log.h"

#include "SerialUtil.h"

using namespace std;

namespace kc1fsz {
    namespace SerialUtil {

int configurePort(int fd, unsigned baud) {

    // Translate the baud rate into the speed_t code
    speed_t speed;
    if (baud == 9600)
        speed = B9600;
    else if (baud == 460800)
        speed = B460800;
    else 
        return -1;

    // Create new termios struct, we call it 'tty' for convention
    // No need for "= {0}" at the end as we'll immediately write the existing
    // config to this struct
    struct termios tty;

    // Read in existing settings, and handle any error
    // NOTE: This is important! POSIX states that the struct passed to tcsetattr()
    // must have been initialized with a call to tcgetattr() overwise behaviour
    // is undefined
    if (tcgetattr(fd, &tty) != 0)
        return -2;

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
    if (cfsetispeed(&tty, speed) != 0)
        return -3;
    if (cfsetospeed(&tty, speed) != 0)
        return -4;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) 
        return -5;

    return 0;
}

static void drainSA818(Log& log, int fd, unsigned ms) {
    unsigned pollingMs = 0;
    unsigned sleepMs = 5;
    while (pollingMs < ms) {
        // Setup a poll 
        pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        int rc = poll(&p, 1, sleepMs);
        // If the poll tells us we've got something then read and discard
        if (rc > 0) {
            char buffer[128];
            int rc2 = read(fd, buffer, sizeof(buffer));
            if (rc2 > 0)
                log.infoDump("Received from SA818", (const uint8_t*)buffer, rc2);
        }
        pollingMs += sleepMs;
    }
}

static int expect818(Log& log, int fd, unsigned ms, const char* target) {
    char acc[64] = { 0 };
    // Leave space for null
    const unsigned maxRead = sizeof(acc) - 1;
    const unsigned sleepMs = 5;
    unsigned accPtr = 0;
    unsigned pollingMs = 0;
    while (pollingMs < ms && accPtr < maxRead) {
        // Setup a poll 
        pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        int rc = poll(&p, 1, sleepMs);
        // If the poll tells us we've got something then read and discard
        if (rc > 0) {
            int rc2 = read(fd, acc + accPtr, maxRead - accPtr);
            if (rc2 > 0) {
                log.infoDump("Received from SA818", (const uint8_t*)acc + accPtr, rc2);
                accPtr += rc2;
                acc[accPtr] = 0;
            }
        }
        pollingMs += sleepMs;
    }
    return strcmp(acc, target) == 0;
}

static int sendToSA818(Log& log, int fd, const char* cmd) {
    log.infoDump("Sending to SA818", (const uint8_t*)cmd, strlen(cmd));
    return write(fd, cmd, strlen(cmd));
}

int configureSA818(Log& log, const char* port, unsigned bw, unsigned txKhz, unsigned rxKhz,
    unsigned txPl, unsigned rxPl, unsigned sq, unsigned vol, 
    bool emp, bool lpf, bool hpf) {

    int fd = ::open(port, O_RDWR | O_NOCTTY);
    if (fd < 0) 
        return -1;
    int rc = SerialUtil::configurePort(fd, 9600);
    if (rc < 0) { 
        close(fd);
        return -2;
    }

    // This is basically a dummy command to clear the queue of any garbage
    const char* cmd0 = "AT+DMOCONNECT\r\n";
    rc = sendToSA818(log, fd, cmd0);
    if (rc < 0) {
        close(fd);
        return -3;
    }
    drainSA818(log, fd, 250);

    // Send the same command again
    rc = sendToSA818(log, fd, cmd0);
    if (rc < 0) {
        close(fd);
        return -3;
    }
    if (!expect818(log, fd, 250, "+DMOCONNECT:0\r\n")) {
        close(fd);
        return -4;
    }

    // Look at version
    rc = sendToSA818(log, fd, "AT+VERSION\r\n");
    if (rc < 0) {
        close(fd);
        return -3;
    }
    // Consume but ignore the output
    drainSA818(log, fd, 250);

    char cmd1[32];
    snprintf(cmd1, sizeof(cmd1), "AT+DMOSETVOLUME=%u\r\n", vol);
    rc = sendToSA818(log, fd, cmd1);
    if (rc < 0) {
        close(fd);
        return -5;
    }
    if (!expect818(log, fd, 250, "+DMOSETVOLUME:0\r\n")) {
        close(fd);
        return -6;
    }

    char cmd2[64];
    snprintf(cmd2, sizeof(cmd2), "AT+DMOSETGROUP=%u,%u.%04u,%u.%04u,%04u,%u,%04u\r\n",
        bw, txKhz / 10000, txKhz % 10000, rxKhz / 10000, rxKhz % 10000, txPl, sq, rxPl);
    rc = sendToSA818(log, fd, cmd2);
    if (rc < 0) {
        close(fd);
        return -7;
    }
    if (!expect818(log, fd, 250, "+DMOSETGROUP:0\r\n")) {
        close(fd);
        return -8;
    }

    char cmd3[64];
    snprintf(cmd3, sizeof(cmd3), "AT+SETFILTER=%u,%u,%u\r\n",
        emp ? 1 : 0, hpf ? 1 : 0, lpf ? 1 : 0);
    rc = sendToSA818(log, fd, cmd3);
    if (rc < 0) {
        close(fd);
        return -9;
    }
    if (!expect818(log, fd, 250, "+DMOSETFILTER:0\r\n")) {
        close(fd);
        return -10;
    }

    close(fd);
    return 0;
}

    }
}