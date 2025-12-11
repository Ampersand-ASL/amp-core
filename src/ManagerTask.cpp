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
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/fixedstring.h"

#include "ManagerTask.h"

using namespace std;

namespace kc1fsz {

ManagerTask::ManagerTask(Log& log, Clock& clock, int listenPort)
:   _log(log),
    _clock(clock),
    _listenPort(listenPort) {  

    // TCP bind, non-blocking socket
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        _log.error("Unable to open manager socket %d", errno);
        return;
    }
    const int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        _log.error("Unable to reuse manager socket %d", errno);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(_listenPort);
    int rc = ::bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));    
    if (rc < 0) {
        _log.error("Unable to bind manager to port %d %d", _listenPort, errno);
        ::close(fd);
        return;
    }

    // Make accept socket non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        _log.error("fcntl failed (%d)", errno);
        ::close(fd);
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        _log.error("fcntl failed (%d)", errno);
        ::close(fd);
        return;
    }

    ::listen(fd, 5);

    _fd = fd;
}

int ManagerTask::getPolls(pollfd* fds, unsigned fdsCapacity) {    
    
    int j = 0;

    // The server listen port
    if (fdsCapacity == 0) 
        return -1;
    fds[j].fd = _fd;
    fds[j].events = POLLIN;
    j++;

    // The individual client sockets
    for (unsigned i = 0; i < MAX_SESSIONS; i++) {
        if (!_sessions[i].active) {
            if (fdsCapacity == 0)
                return -1;
            fds[j].fd = _sessions[i].fd;
            fds[j].events = POLLIN;
            j++;
        }
    } 
    return j;
}

bool ManagerTask::run2() {    

    bool didWork = false;

    // Check for new inbound connections
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int clientFd = ::accept(_fd, (struct sockaddr *)&client_addr, &client_len);
    if (clientFd > 0) {

        didWork = true;

        // Make client socket non-blocking
        int flags = fcntl(clientFd, F_GETFL, 0);
        if (flags == -1) {
            _log.error("fcntl failed (%d)", errno);
            ::close(clientFd);
            return true;
        }
        if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1) {
            _log.error("fcntl failed (%d)", errno);
            ::close(clientFd);
            return true;
        }

        bool good = false;
        for (unsigned i = 0; i < MAX_SESSIONS; i++) {
            if (!_sessions[i].active) {
                _sessions[i].reset();
                _sessions[i].active = true;
                _sessions[i].fd = clientFd;
                _sessions[i].startMs = _clock.time();
                good = true;
                _log.info("New manager session %d", i);
                break;
            }
        }
        if (!good) {
            _log.info("Max manager sessions exceeded");
            ::close(clientFd);
        }
    }
    
    // Check for inbound data on any active session and accumulate it
    for (unsigned i = 0; i < MAX_SESSIONS; i++) {
        Session& session = _sessions[i];
        if (session.active) {
            unsigned spaceAvailable = Session::BUF_CAPACITY - session.inBufLen;
            int rc = ::read(session.fd, session.inBuf + session.inBufLen, 
                spaceAvailable);
            // Check for drop 
            if (rc == 0) {
                _log.info("Session %d dropped", i);
                session.reset();
                didWork = true;
            } 
            else if (rc > 0) {
                session.inBufLen += rc;
                didWork = true;
            }
        }
    }

    // Check to see if there are any full commands available
    for (unsigned i = 0; i < MAX_SESSIONS; i++) {
        Session& session = _sessions[i];
        if (session.active) {
            char cmd[65];
            while (session.popCommandIfPossible(cmd, 65)) {
                didWork = true;
                //_log.info("Command %s", cmd);
                // Parse the command and take action based on the type
                fixedstring action, a0, a1;
                visitValues(cmd, strlen(cmd), [this, &action, &a0, &a1](const char* n, const char* v) { 
                    //this->_log.info("n=%s v=%s", n, v);
                    fixedstring name = n;
                    fixedstring value = v;
                    if (strcmp(n, "ACTION") == 0) {
                        action = v;
                    } 
                    else {
                        if (action == "Login") {
                            if (name == "Username") 
                                a0 = value;
                            else if (name == "Secret")
                                a1 = value;
                        }
                        else if (action == "COMMAND") {
                            if (name == "COMMAND") 
                                a1 = value;
                        }
                    }
                });

                if (action == "Login") {
                    _log.info("Login");
                    // TODO: Validate username/secret
                    char resp[] = "Response: Success\r\nMessage: Authentication accepted\r\n\r\n";
                    _send(session, resp, strlen(resp));
                }
                else if (action == "COMMAND") {
                    if (_sink)
                        _sink->execute(a1.c_str());
                    char resp[] = "Response: Success\r\nMessage: Command output follows\r\nOutput:\r\n\r\n";
                    _send(session, resp, strlen(resp));
                }
            }
        }
    }
    return didWork;
}

void ManagerTask::_send(const Session& session, const char* d, unsigned dLen) {
    ::write(session.fd, d, dLen);
}

bool ManagerTask::Session::popCommandIfPossible(char* cmdBuf, unsigned cmdBufCapacity) {
    // Look for \r\n\r\n
    void* p = memmem((const void*)inBuf, inBufLen, (const void*)"\r\n\r\n", 4);
    if (p != 0) {
        unsigned i = 0;
        for (char* a = inBuf; a != p && i < cmdBufCapacity - 1; a++)
            cmdBuf[i++] = *a;
        cmdBuf[i] = 0;
        // Shift remaining data left
        memmove((void*)inBuf, (const void*)(inBuf + i + 4), inBufLen - i - 4);
        // Notice that we drop the training \r\n\r\n
        inBufLen -= (i + 4);
        return true;
    }
    else {
        return false;
    }
}

int visitValues(const char* buf, unsigned bufLen, VisitFn fn) {

    int state = 0;
    int j = 0;
    const int nameCapacity = 65;
    char name[nameCapacity];
    const int valueCapacity = 65;
    char value[valueCapacity];

    for (unsigned i = 0; i < bufLen; i++) {
        // Capturing the name
        if (state == 0) {
            if (buf[i] == '\r' || buf[i] == '\n') {
                // Ignore
            } else if (buf[i] == ':') {
                if (j >= nameCapacity)
                    return -1;
                name[j] = 0;
                state = 1;
                j = 0;
            } else {
                if (j >= nameCapacity)
                    return -1;
                name[j++] = buf[i];
            }
        }
        // Past the :, looking for the space
        else if (state == 1) {
            // Spaces are ok to skip
            if (buf[i] == ' ') {
            } 
            else {
                if (j >= valueCapacity)
                    return -1;
                value[j++] = buf[i];
                state = 2;
            }
        }
        // Past the :_ and ready to accumulate a value
        else if (state == 2) {
            // The value ends with a \r or th
            if (buf[i] == '\r') {
                if (j >= valueCapacity)
                    return -1;
                value[j] = 0;
                // Fire the listener
                fn(name, value);
                state = 4;
            }
            else {
                if (j >= valueCapacity)
                    return -1;
                value[j++] = buf[i];
            }
        }
        else if (state == 4) {
            if (buf[i] == '\n') {
                state = 0;
                j = 0;
            }
            else {
                return -1;
            }
        }  
    }

    // If there is no trailing \r\n
    if (state == 2) {
        if (j >= valueCapacity)
            return -1;
        value[j] = 0;
        // Fire the listener
        fn(name, value);
    }

    return 0;
}
}
