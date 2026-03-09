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
#include <cstring>
#include <cmath>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "pico/time.h"

#include "kc1fsz-tools/Log.h"
#include "kc1fsz-tools/Clock.h"
#include "kc1fsz-tools/NTPUtils.h"

#include "NTPClient.h"

// #### TODO: REMOVE HARD-CODING
static const char* NTP_IP_ADDR = "129.6.15.28";
static unsigned NTP_IP_PORT = 123;

namespace kc1fsz {

NTPClient::NTPClient(Log& log, Clock& clock) 
:   _log(log),
    _clock(clock),
    _timer2(log, clock, NTP_SAMPLE_INTERVAL_S,
        [this, &log, &clock]() {

            if (_sockFd == 0)
                return;

            const unsigned PACKET_SIZE = 48;
            uint8_t packet[PACKET_SIZE] = { 0 };
            int len = 48;
            packet[0] = 0x23;

            uint64_t nowNtp = NTPUtils::usToNtpTime(getNowUs());
            pack_uint64_be(nowNtp, packet + 24);
            // RFC5905 page 29: "A packet is bogus if the origin timestamp T1 in the packet
            // does not match the xmt state variable T1.
            pack_uint64_be(nowNtp, packet + 40);
            //log.infoDump("Send", packet, len);

            // Send the query to a NTP server
            struct sockaddr_in dest_addr;
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(NTP_IP_PORT);
            inet_pton(AF_INET, NTP_IP_ADDR, &dest_addr.sin_addr); 
            int rc = ::sendto(_sockFd, packet, len, 0, 
                (struct sockaddr*)&dest_addr, sizeof(dest_addr));
            if (rc < 0) {
                log.error("Send error %d", rc);
            }
        }
    ),
    _timer3(log, clock, NTP_SAMPLE_INTERVAL_S, 
        [this, &log, &clock]() {

            if (_newSampleCount >= 8) {

                // Consume all samples
                _newSampleCount = 0;

                // Find the smallest delay
                double minDelay = 1000000;
                unsigned minIx = 0;
                for (unsigned i = 0; i < NTP_SAMPLE_COUNT; i++) {
                    if (_samples[i].dispersion == (16 << 16))
                        break;
                    if (_samples[i].delay < minDelay) {
                        minDelay = _samples[i].delay;
                        minIx = i;
                    }
                }

                double selectedOffset = _samples[minIx].offset;

                // Compute the jitter
                double jitterTotal = 0;
                unsigned jitterCount = 0;
                for (unsigned i = 0; i < _sampleCount; i++) {
                    if (_samples[i].dispersion != (16 << 16)) {
                        double offset = _samples[i].offset;
                        jitterTotal += std::pow(offset - selectedOffset, 2.0);
                        jitterCount++;
                    }
                }            
                double jitter = std::sqrt(jitterTotal / (double)jitterCount);

                // Filter the adjustment
                // If the correction is less than 10ms then reduce the size
                if (selectedOffset < 0.010)
                    selectedOffset /= 2.0;

                log.info("Adjusting time by %f (ms) jitter %f (ms)", selectedOffset * 1000, jitter * 1000);

                // Adjust the clock based on the selected offset
                _correctionOffsetUs += selectedOffset * 1000000.0;
            }
        }
    )
{
}

void NTPClient::open() {
    int rc = socket(AF_INET, SOCK_DGRAM, 0);
    if (rc < 0) {
        _log.error("Failed to open socket");
    }
    else {
        _sockFd = rc;
    }

}

void NTPClient::close() { 
    if (_sockFd) 
        ::close(_sockFd);
    _sockFd = 0;
}

void NTPClient::oneSecTick() {
    _timer2.oneSecTick();
    _timer3.oneSecTick();
}

bool NTPClient::run2() {

    if (_sockFd == 0)
        return false;

    // Check for input
    const unsigned readBufferSize = 512;
    uint8_t readBuffer[readBufferSize];
    struct sockaddr_in peerAddr;
    socklen_t peerAddrLen = sizeof(peerAddr);
    int rc = recvfrom(_sockFd, readBuffer, readBufferSize, 0,
        (struct sockaddr *)&peerAddr, &peerAddrLen);
    if (rc > 0) {
        // Immediately record T4
        uint64_t nowUs = getNowUs();
        uint64_t nowNtp = NTPUtils::usToNtpTime(nowUs);
        //log.infoDump("Receive", readBuffer, rc);

        // The offset (theta) represents the
        // maximum-likelihood time offset of the server clock relative to the
        // system clock.  The delay (delta) represents the round-trip delay
        // between the client and server.  The dispersion (epsilon) represents
        // the maximum error inherent in the measurement. 

        // Do the offset/delay calc
        const double offset = NTPUtils::calcOffset(readBuffer, nowNtp);
        const double delay = NTPUtils::calcDelay(readBuffer, nowNtp);
        double disp = unpack_uint32_be(readBuffer + 8);
        disp /= (double(1 << 16));

        // At startup the offset will be meaningless
        if (_startup) {
            _correctionOffsetUs = offset * 1000000.0;
            _startup = false;
        } 
        else {
            
            //log.info("Offset %f", offset);

            // Age the existing dispersion data
            uint32_t secondsElapsed = (nowUs - _lastResponseUs) / 1000000;
            // We age at 15uS per second
            uint32_t usAge = secondsElapsed * 15;
            // Change the microseconds into NTP short units
            uint32_t ageShort = (usAge * (1L << 16)) / 1000000;
            _ageDispersions(ageShort);

            // Load up the shift register 
            _shift();
            _samples[0].offset = offset;
            _samples[0].delay = delay;
            // ### TODO: NEED TO INCLUDE LOCAL DISPERSION
            _samples[0].dispersion = unpack_uint32_be(readBuffer + 8);
            _newSampleCount++;
        }
        _lastResponseUs = nowUs;
        return true;
    }
    else {
        return false;
    }
}

void NTPClient::_shift() {
    for (unsigned i = _sampleCount - 1; i > 0; i--)
        _samples[i] = _samples[i-1];
}

void NTPClient::_ageDispersions(uint32_t ageShort) {
    for (unsigned i = 0; i < _sampleCount; i++)
        // Don't touch the "infinity" dispersions
        if (_samples[i].dispersion != (16 << 16)) 
            _samples[i].dispersion += ageShort;
}

uint64_t NTPClient::getNowUs() const {
    absolute_time_t now = get_absolute_time();
    return to_us_since_boot(now) + _correctionOffsetUs - _fixedOffsetUs;
}

}