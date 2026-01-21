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

#include <atomic>

#include "kc1fsz-tools/threadsafequeue2.h"

namespace kc1fsz {

class Log;

    namespace amp {

/**
 * A function that can be put on a background thread to do TTS via
 * the message bus.
 *
 * @param reqQueue Request queue.
 * @param resQueue Response queue.
 * @param runFlag Used to exit the thread.
 */
void ttsLoop(Log* log, threadsafequeue2<Message>* reqQueue,
    threadsafequeue2<Message>* resQueue, std::atomic<bool>* runFlag);

}

}
