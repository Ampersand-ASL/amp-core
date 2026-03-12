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

namespace kc1fsz {
    namespace SerialUtil {

/**
 * Configures the UART parameters of the serial device. 
 * NOTE: Make sure the device is opened with these flags: 
 *    O_RDWR | O_NOCTTY
 * @returns 0 on success, -1 means baud rate not supported.
 */
int configurePort(int fd, unsigned baud);

/**
 * @param txFreq Frequency in kHz, so 446.05 MHz is passed as 4460500.
 * @param rxFreq Frequency in kHz, so 446.05 MHz is passed as 4460500.
 * @param txPl The SA818 code from 0 to 38
 * @param rxPl The SA818 code from 0 to 38
 */
int configureSA818(Log& log, const char* port, unsigned bw, unsigned txKhz, unsigned rxKhz,
    unsigned txPl, unsigned rxPl, unsigned sq, unsigned vol,
    bool emp, bool lpf, bool hpf);

    }
}