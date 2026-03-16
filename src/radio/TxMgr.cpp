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
#include <cassert>

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"

#include "radio/TxMgr.h"

namespace kc1fsz {

TxMgr::TxMgr(Log& log, Clock& clock,
        std::function<void(const int16_t* frame, unsigned frameLen)> audioGenerateCb,
        std::function<void(bool active)> pttControlCb,
        std::function<void(bool active, bool secondary)> toneControlCb) 
: _log(log), _clock(clock), _audioGenerateCb(audioGenerateCb), _pttControlCb(pttControlCb),
    _toneControlCb(toneControlCb) {
}

void TxMgr::consumeAudioFrame(const int16_t* frame, unsigned frameLen) {
}

void TxMgr::setAudioDelayMs(unsigned ms) {
}

void TxMgr::setHangMs(unsigned ms) {
}

void TxMgr::setChickenDelayMs(unsigned ms) {
}

void TxMgr::setToneBreak(bool b) {
}

// ----- Runnable2 --------------------------------------------------------

bool TxMgr::run2() {
}

void TxMgr::audioRateTick() {
}

}
