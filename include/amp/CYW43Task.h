/**
 * Copyright (C) 2027, Bruce MacKinnon KC1FSZ, All Rights Reserved
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

#include "pico/cyw43_arch.h"

#include "Runnable2.h"

namespace kc1fsz {
    namespace amp {

class CYW43Task : public Runnable2 {
public:
    virtual bool run2() { 
        // Ff you are using pico_cyw43_arch_poll, then you must poll periodically from 
        // your main loop (not from a timer) to check for Wi-Fi driver or lwIP work that 
        // needs to be done.
        cyw43_arch_poll();
        return false; 
    }
};

    }
}
