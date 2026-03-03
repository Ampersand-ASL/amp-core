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
#include <iostream>
#include <algorithm>

#include "kc1fsz-tools/Common.h"
#include "kc1fsz-tools/NetUtils.h"
#include "kc1fsz-tools/Log.h"

#include "MessageConsumer.h"
#include "Message.h"
#include "LineParrot.h"

// #### TODO: CONSOLIDATE
#define BLOCK_SIZE_8K (160)
#define BLOCK_SIZE_16K (160 * 2)
#define BLOCK_SIZE_48K (160 * 6)

#define MAX_CAPTURE_FRAMES (750)

using namespace std;

namespace kc1fsz {
    namespace amp {

LineParrot::LineParrot(Log& log, Clock& clock, unsigned lineId,
    MessageConsumer& bus, unsigned audioDestLineId)
:   _log(log),
    _clock(clock),
    _lineId(lineId),
    _bus(bus),
    _audioDestLineId(audioDestLineId) {
}

int LineParrot::open() {

    close();

    // Generate a start of call message so that the bridge will accept audio
    // when it arrives.
    PayloadCallStart payload;
    // NOTE: Parrot uses the bridge audio rate/format to improve efficiency
    payload.codec = CODECType::IAX2_CODEC_PCM_48K;
    payload.bypassJitterBuffer = true;
    payload.echo = false;
    payload.startMs = _clock.time();
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "Parrot");
    payload.originated = true;
    payload.permanent = true;

    MessageWrapper msg(Message::Type::SIGNAL, Message::SignalType::CALL_START, 
        sizeof(payload), (const uint8_t*)&payload, 0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(_audioDestLineId, Message::BROADCAST);
    _bus.consume(msg);

    return 0;
}

void LineParrot::close() {   
    PayloadCallEnd payload;
    payload.localNumber[0] = 0;
    snprintf(payload.remoteNumber, sizeof(payload.remoteNumber), "Parrot");
    _sendSignal(Message::SignalType::CALL_END, &payload, sizeof(payload));
} 

void LineParrot::_sendSignal(Message::SignalType type, void* body, unsigned len) {
    _sendSignal(type, body, len, _audioDestLineId, Message::UNKNOWN_CALL_ID);
}

void LineParrot::_sendSignal(Message::SignalType type, void* body, unsigned len,
    unsigned destLineId, unsigned destCallId) {
    MessageWrapper msg(Message::Type::SIGNAL, type, len, (const uint8_t*)body, 
        0, _clock.time());
    msg.setSource(_lineId, _callId);
    msg.setDest(destLineId, destCallId);
    _bus.consume(msg);
}

void LineParrot::consume(const Message& msg) {   

    if (msg.isVoice()) {

        assert(msg.getFormat() == CODECType::IAX2_CODEC_PCM_48K);
        assert(msg.size() == BLOCK_SIZE_48K * 2);

        // Look for a new talkspurt to record
        if (_state == State::STATE_LISTENING) {

            _log.info("LineParrot started recording");

            _captureQueue = std::queue<PCM16Frame>();
            _captureQueueDepth = 0;

            _setState(State::STATE_RECORDING);
        } 

        if (_state == State::STATE_RECORDING) {
            if (_captureQueueDepth < MAX_CAPTURE_FRAMES) {
                _captureQueue.push(PCM16Frame((const int16_t*)msg.body(), BLOCK_SIZE_48K));
                _captureQueueDepth++;
            }
        }
    }
    else if (msg.getType() == Message::Type::SIGNAL) {
        // This is the case where an UNKEY is requested by something 
        // on the internal bus. Create an UNKEY frame and send it out.
        if (msg.getFormat() == Message::SignalType::RADIO_UNKEY_GEN) {                        
            _log.info("LineParrot unkey");
            if (_state == State::STATE_RECORDING) {
                _endRecording();
            }
        }
        // This is the case when the TALKERID is being asserted
        else if (msg.getFormat() == Message::SignalType::CALL_TALKERID) {                        
        }
    }
}

void LineParrot::_endRecording() {

    // Move the recorded audio onto the playback queue
    while (!_captureQueue.empty()) {
        _playQueue.push(_captureQueue.front());
        _captureQueue.pop();
    }

    // Trigger playback
    _setState(State::STATE_PLAYING);
}

void LineParrot::_setState(State state) {
    _state = state;
    _stateStartMs = _clock.timeMs();
}

bool LineParrot::run2() {   
    return false;
}

void LineParrot::audioRateTick(uint32_t ms) {

    if (_state == State::STATE_PLAYING) {
        if (!_playQueue.empty()) {
            // Make a message and transmit to the Bridge
            MessageWrapper msg(Message::Type::AUDIO, CODECType::IAX2_CODEC_PCM_48K, 
                BLOCK_SIZE_48K * 2, (const uint8_t*)_playQueue.front().data(), 0, ms);
            msg.setSource(_lineId, _callId);
            msg.setDest(_audioDestLineId, Message::UNKNOWN_CALL_ID);
            _bus.consume(msg);
            _playQueue.pop();
        } 
        // Finished playing
        else {
            _log.info("LineParrot playback complete");
            _setState(State::STATE_LISTENING);
        }
    }
}

void LineParrot::oneSecTick() {    
}

    }
}
