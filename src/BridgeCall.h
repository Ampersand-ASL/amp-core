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

#include <queue>
#include <string>

#include "kc1fsz-tools/threadsafequeue.h"

#include "PCM16Frame.h"
#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "BridgeIn.h"
#include "BridgeOut.h"
#include "Poker.h"

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge;

/**
 * Each participant in a conference (i.e. any Line) has an instance of this class
 */
class BridgeCall {
public:

    friend class Bridge;

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_16K = 160 * 2;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;
    static const unsigned SESSION_TIMEOUT_MS = 120 * 1000;
    static const unsigned LINE_ID = 10;
    static const unsigned CALL_ID = 1;

    /**
     * Called during startup to get the white noise buffer built
     */
    static void initializeWhiteNoise();

    enum Mode {
        NORMAL,
        PARROT,
        TONE
    };

    BridgeCall();

    /**
     * One-time initialization. Connects the call to the outside world.
     */
    void init(Bridge* bridge, Log* log, Log* traceLog, Clock* clock, 
        MessageConsumer* sink, 
        unsigned bridgeLineId, unsigned bridgeCallId, 
        unsigned ttsLineId, unsigned netTestLineId, const char* netTestBindAddr) {
        _bridge = bridge;
        _log = log;
        _traceLog = traceLog;
        _clock = clock;
        _sink = sink;
        _bridgeLineId = bridgeLineId;
        _bridgeCallId = bridgeCallId;
        _ttsLineId = ttsLineId;
        _netTestLineId = netTestLineId;
        if (netTestBindAddr)
            _netTestBindAddr = netTestBindAddr;
        else 
            _netTestBindAddr = "0.0.0.0";
        _bridgeIn.init(_log, _traceLog, _clock);
    }

    void reset();

    void setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec,
        bool bypassJitterBuffer, bool echo, bool sourceAddrValidated, Mode initialMode,
        const char* remoteNodeNumber);

    bool isActive() const { 
        return _active; 
    }

    bool isNormal() const { return _mode == Mode::NORMAL; }

    bool equals(const BridgeCall& other) const { 
        return _active && _lineId == other._lineId && _callId == other._callId; 
    }

    bool hasInputAudio() const { return _stageInSet; }

    bool belongsTo(const Message& msg) const {
        return _active && 
            ((msg.getSourceBusId() == _lineId && msg.getSourceCallId() == _callId) ||
             (msg.getDestBusId() == _bridgeLineId && msg.getDestCallId() == _bridgeCallId));
    }

    bool isEcho() const { return _echo; }

    std::string getRemoteNodeNumber() const { return _remoteNodeNumber; }

    /**
     * Requests a text-to-speech announcement to be sent to the call.
     */
    void requestTTS(const char* prompt);

    /**
     * This extracts the call's contribution (if any) to the audio frame for the designated
     * tick interval.
     * 
     * @param tickMs The start of the time interval for which this frame is applicable.
     */
    void extractInputAudio(int16_t* pcmBlock, unsigned blockSize, float scale, uint32_t tickMs);    

    /**
     * Clear the call's contribution so that we never use it again.
     */
    void clearInputAudio();

    /**
     * This provides the call with the mixed audio frame for the designated tick interval.
     * 
     * @param tickMs The start of the time interval for which this frame is applicable.
     */
    void setConferenceOutput(const int16_t* pcmBlock, unsigned blockSize, uint32_t tickMs);  

    /**
     * Tells the call to generate its audio output for the designated tick interval,
     * taking into account all of the various sources.
     */
    void produceOutput(uint32_t tickMs);

    // ----- MessageConsumer -------------------------------------------------

    void consume(const Message& frame);

    // ----- Runnable2 --------------------------------------------------------

    void audioRateTick(uint32_t tickMs);
    void oneSecTick();

private:

    Bridge* _bridge;
    Log* _log;
    Log* _traceLog;
    Clock* _clock;
    MessageConsumer* _sink;
    unsigned _bridgeLineId = 0;
    unsigned _bridgeCallId = 0;
    unsigned _ttsLineId = 0;
    unsigned _netTestLineId = 0;
    std::string _netTestBindAddr;

    bool _echo = false;
    bool _sourceAddrValidated = false;
    Mode _mode = Mode::NORMAL;

    bool _active = false;
    unsigned _lineId = 0;
    unsigned _callId = 0;
    uint32_t _startMs = 0;
    std::string _remoteNodeNumber;
    uint32_t _lastAudioMs = 0;

    BridgeIn _bridgeIn;
    BridgeOut _bridgeOut;

    // The audio waiting to be sent to the caller in PCM16 48K format.
    std::queue<PCM16Frame> _playQueue;

    std::string _dtmfAccumulator;
    uint32_t _lastDtmfRxMs = 0;

    void _processTTSAudio(const Message& msg);

    // ----- Normal Mode Related ----------------------------------------------

    void _processDtmfCommand(const std::string& cmd);

    void _processNormalAudio(const Message& msg);

    // This is the call's contribution to the conference when in normal mode.
    // IMPORTANT: All of the signaling has been handled ahead of this point
    // so _stageIn will either be silence or audio.  
    int16_t _stageIn[BLOCK_SIZE_48K];
    // Indicates whether any input was provided during this tick
    bool _stageInSet = false;

    // This is the conference audio prepared for this call. 
    int16_t _stageOut[BLOCK_SIZE_48K];
    // Indicates whether any output was provided during this tick
    bool _stageOutSet = false;

    // Used to identify the trailing edge of output generation so that we 
    // can make an UNKEY at the right time.
    bool _lastCycleGeneratedOutput = false;

    // ----- Tone Mode Related ------------------------------------------------

    void _toneAudioRateTick(uint32_t tickMs);

    bool _toneActive = false;
    float _toneOmega;
    float _tonePhi;
    float _toneLevel;

    // ----- Parrot Mode Related ----------------------------------------------

    void _processParrotAudio(const Message& msg);
    void _parrotAudioRateTick(uint32_t tickMs);
    void _processParrotTTSAudio(const Message& msg);

    void _loadAudioFile(const char* fn, std::queue<PCM16Frame>& queue) const;
    void _loadSilence(unsigned ticks, std::queue<PCM16Frame>& queue) const;
    void _loadAudio(const std::vector<PCM16Frame>& audio, std::queue<PCM16Frame>& queue) const;
    void _loadSweep(std::queue<PCM16Frame>& queue);
    void _loadCw(float amp, float hz, unsigned ticks, std::queue<PCM16Frame>& queue);

    void _loadWhite(float amp, unsigned ticks, std::queue<PCM16Frame>& queue) const;

    /**
     * Puts one 16K LE frame onto the queue provided
     */
    void _loadAudioMessage(const Message& msg, std::queue<PCM16Frame>& queue) const;

    void _analyzeRecording(const std::vector<PCM16Frame>& audio, float* peakPower, float* avgPower);

    // The audio captured from the caller
    std::queue<PCM16Frame> _captureQueue;
    unsigned _captureQueueDepth = 0;

    enum ParrotState {
        NONE,
        ACTIVE,
        CONNECTED,
        WAITING_FOR_NET_TEST,
        READY_FOR_GREETING,
        TTS_AFTER_CONNECTED,
        PLAYING_PROMPT_GREETING,
        WAITING_FOR_RECORD,
        RECORDING,
        PAUSE_AFTER_RECORD,
        TTS_AFTER_RECORD,
        PLAYING,
        TIMEDOUT
    };

    ParrotState _parrotState = ParrotState::NONE;
    uint32_t _parrotStateStartMs = 0;
    uint32_t _lastUnkeyProcessedMs = 0;
    Poker::Result _netTestResult;
};

    }
}
