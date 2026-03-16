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

#include "radio/RxMgr.h"

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
#pragma once

#include <functiomnal>

namespace kc1fsz {

RxMgr::RxMgr(Log& log, Clock& clock,
    std::function<void(const int16_t* frame, unsigned frameLen)> audioGenerateCb) 
:   _log(log), _clock(clock), _audioGenerateCb(audioGenerateCb) {

}

void RxMgr::consumeAudioFrame(const int16_t* frame, unsigned frameLen) {

}

void RxMgr::setCosState(bool b) {

}

void RxMgr::setToneState(bool b) {

}


void RxMgr::setCosSquelchEnabled(bool b) {

}

void RxMgr::setToneSquelchEnabled(bool b) {
}

void RxMgr::setAudioDelayMs(unsigned ms) { _audioDelayMs = ms; }
void RxMgr::setDebounceMs(unsigned ms) { _debounceMs = ms; }

bool RxMgr::run2() {

}

void RxMgr::audioRateTick() {

}

}
