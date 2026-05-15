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
#pragma once

#include <cstdint>
#include <string>

#include "kc1fsz-tools/TimeDebouncer2.h"

#include "Message.h"
#include "Runnable2.h"

namespace kc1fsz {

class Log;
class Clock;
class MessageConsumer;

    namespace amp {

/**
 * An instance of this class is responsible for managing a single output
 * signal, most usually a PTT signal or something like that.
 *
 * Support various modes like HID (typically on a CM108 device) or serial
 * modem control signals like RTS/DTR or GPIO pins.
 */
class SignalOut : public Runnable2, public MessageConsumer {
public:

    SignalOut(Log& log, Clock& clock, MessageConsumer& bus, 
         Message::SignalType sigTypeOn, Message::SignalType sigTypeOff);

    int openHid(const char* deviceName, const char* signalName);

    int openSerial(const char* deviceName, const char* signalName);

    void close();

    void setInvert(bool b) { _invert = b; }

    // ----- MessageConsumer --------------------------------------------------

    virtual void consume(const Message& frame);

    // ------ Runnable2 --------------------------------------------------------

    virtual void audioRateTick(uint32_t tickMs);

private:

    enum Mode {
        MODE_NONE,
        MODE_HID,
        MODE_SERIAL
    };

    Log& _log;
    Clock& _clock;
    MessageConsumer& _bus;
    const Message::SignalType _sigTypeOn;
    const Message::SignalType _sigTypeOff;

    std::string _deviceName;
    std::string _signalName;
    Mode _mode = Mode::MODE_NONE;

    int _fd = 0;
    bool _invert = false;
    bool _rawValue = false;
    bool _officialValue = false;
    TimeDebouncer2 _debouncedState;

    void _setOfficialValue(bool s);

    // ------ HID Related ----------------------------------------------------

    bool _hidFailed = false;
    static const unsigned MAX_HID_SIZE = 16;
    uint8_t _hidAcc[MAX_HID_SIZE];
    unsigned _hidPacketSize = 4;
    unsigned _hidOffset = 1;
    uint8_t _hidMask = 0x04;

    // ----- Serial Related --------------------------------------------------

    int _ticomMask;
};

    }
}

   