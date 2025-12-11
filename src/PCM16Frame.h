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

#include <cstdint>

namespace kc1fsz {

/**
 * Used for passing events/data INTERNALLY within the application.
 */
class PCM16Frame {
public:

    // Space for a 48K PCM16 frame
    static const unsigned MAX_SIZE = 160 * 6;

    PCM16Frame() : _size(0) { }
    
    PCM16Frame(const int16_t* data, unsigned size) {
        assert(size <= MAX_SIZE);
        memcpy(_body, data, sizeof(int16_t) * size);
        _size = size;
    }
    
    PCM16Frame(const PCM16Frame& other) {
        (*this) = other;
    }

    PCM16Frame& operator=(const PCM16Frame& other) {
        assert(other._size <= MAX_SIZE);
        if (other._size > 0)
            memcpy(_body, other._body, sizeof(int16_t) * other._size);
        _size = other._size;
        return *this;
    }

    unsigned size() const { return _size; }

    const int16_t* data() const { return _body; }
    
private:

    unsigned _size;
    int16_t _body[MAX_SIZE];
};

}
