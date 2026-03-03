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

#include <functional>
#include <queue>
#include <vector>

#include "PCM16Frame.h"
#include "Line.h"
#include "IAX2Util.h"
#include "Message.h"
#include "MessageConsumer.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class LineParrot : public Line {
public:

    static const unsigned BLOCK_SIZE_8K = 160;

    /**
     * @param consumer This is the sink interface that received messages
     * will be sent to. 
     */
    LineParrot(Log& log, Clock& clock, unsigned lineId, MessageConsumer& consumer, 
        unsigned audioDestLineId, unsigned ttsLineId);

    int open();

    /**
     * Resets all calls as a side-effect.
     */
    void close();
    
    void setTrace(bool a) { _trace = a; }

    struct AudioStats {
        bool good = false;
        float peakPower = 0;
        float avgPower = 0;
    };

    /**
     * Performs analysis of an audio recording
     */
    static AudioStats analyzeRecording(const std::vector<PCM16Frame>& audio);

    /**
     * @returns A statement of the audio analysis, suitable for TTS.
     */
    static std::string summarizeAnalysis(const AudioStats& stats, 
        std::vector<int>& levelThresholds);

    // ----- Line/MessageConsumer-----------------------------------------------------

    virtual void consume(const Message& m);

    // ----- Runnable -------------------------------------------------------

    virtual void audioRateTick(uint32_t tickTimeMs);

private:

    enum State {
        STATE_LISTENING,
        STATE_RECORDING,
        STATE_POST_RECORDING_TTS,
        STATE_PAUSE_AFTER_RECORDING,
        STATE_PLAYING
    };

    void _setState(State state);
    void _endRecording();
    void _endAnalysisTTS();

    // #### TODO: CONSIDER MOVING SOME OF THIS STUFF TO Line
    void _requestTTS(const char* prompt);
    void _sendSignal(Message::SignalType type, void* body, unsigned len);
    void _sendSignal(Message::SignalType type, void* body, unsigned len,
        unsigned destLineId, unsigned destCallId);

    Log& _log;
    Clock& _clock;
    const unsigned _lineId;
    const unsigned _callId = 1;
    MessageConsumer& _bus;
    // Where the inbound audio gets sent
    const unsigned _audioDestLineId;
    // Where the text-to-speech requests get sent
    const unsigned _ttsLineId;
    // Enables detailed network tracing
    bool _trace = false;

    State _state = State::STATE_LISTENING;
    uint64_t _stateStartMs = 0;
    uint64_t _lastAudioRxMs = 0;

    // The audio captured from the caller
    std::queue<PCM16Frame> _captureQueue;
    unsigned _captureQueueDepth = 0;

    std::queue<PCM16Frame> _playQueue;
    std::vector<int> _levelThresholds;
};
    }
}
