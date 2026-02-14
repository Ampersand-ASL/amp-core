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

// 3rd party
#include <nlohmann/json.hpp>

#include "kc1fsz-tools/threadsafequeue.h"

#include "PCM16Frame.h"
#include "Runnable2.h"
#include "MessageConsumer.h"
#include "Message.h"
#include "BridgeIn.h"
#include "BridgeOut.h"
#include "Poker.h"

using json = nlohmann::json;

namespace kc1fsz {

class Log;
class Clock;

    namespace amp {

class Bridge;

/**
 * Each participant in a conference (i.e. any Line) has an instance of this class
 */
class BridgeCall : public Runnable2 {
public:

    friend class Bridge;

    static const unsigned AUDIO_RATE = 48000;
    static const unsigned BLOCK_SIZE_8K = 160;
    static const unsigned BLOCK_SIZE_16K = 160 * 2;
    static const unsigned BLOCK_SIZE_48K = 160 * 6;
    static const unsigned BLOCK_PERIOD_MS = 20;
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
     * @param sink The message consumer used to request TTS and Network Tests.
     */
    void init(Bridge* bridge, Log* log, Log* traceLog, Clock* clock, 
        MessageConsumer* sink, 
        unsigned bridgeLineId, unsigned bridgeCallId, 
        unsigned ttsLineId, unsigned netTestLineId, const char* netTestBindAddr);

    /**
     * Used at the very beginning of a call.
     */
    void setup(unsigned lineId, unsigned callId, uint32_t startMs, CODECType codec,
        bool bypassJitterBuffer, bool echo, bool sourceAddrValidated, Mode initialMode,
        const char* remoteNodeNumber, bool permanent, bool useKerchunkFilter,
        unsigned kerchunkFilterEvaluationIntervalMs);

    void reset();
        
    bool isActive() const { return _active; }

    bool isNormal() const { return _mode == Mode::NORMAL; }

    /**
     * @returns true if this call has had input audio in the past few
     * seconds.
     */
    bool isInputActiveRecently() const { return _bridgeIn.isActiveRecently(); }

    bool equals(const BridgeCall& other) const { 
        return _active && _lineId == other._lineId && _callId == other._callId; 
    }

    bool hasInputAudio() const { return isNormal() && _stageInSet; }

    std::string getInputTalkerId() const { return _talkerId; }
    
    uint64_t getInputTalkerIdChangeMs() const { return _talkerIdChangeMs; }

    void setOutputTalkerId(const char* talkerId);

    bool belongsTo(const Message& msg) const {
        return _active && 
            ((msg.getSourceBusId() == _lineId && msg.getSourceCallId() == _callId) ||
             (msg.getDestBusId() == _bridgeLineId && msg.getDestCallId() == _bridgeCallId));
    }

    bool isEcho() const { return _echo; }

    std::string getRemoteNodeNumber() const { return _remoteNodeNumber; }

    /**
     * @returns true if this call was recently the source of commands
     * such as DTMF, remote signaling, etc. This is often used to 
     * determine whether state change announcement should be directed
     * to the call.
     */
    bool isRecentCommander() const;

    /**
     * Requests a text-to-speech announcement to be sent to the call.
     */
    void requestTTS(const char* prompt);

    /**
     * This extracts the call's contribution (if any) to the audio frame for the 
     * designated tick interval.
     * 
     * @param calls The total number of calls that are being combined. This
     *   is used to scale this call's contribution appropriately.
     * @param tickMs The start of the time interval for which this frame is applicable.
     */
    void extractInputAudio(int16_t* pcmBlock, unsigned blockSize, int calls, uint32_t tickMs);    

    /**
     * Clear the call's contribution so that we never use it again.
     */
    void clearInputAudio();

    /**
     * This provides the call with the mixed audio frame for the designated tick interval.
     * Tells the call to generate its audio output for the designated tick interval,
     * taking into account all of the various sources.
     * 
     * @param tickMs The start of the time interval for which this frame is applicable.
     * @param mixCount The number of conference talkers that are included in the pcmBlock.
     */
    void setConferenceOutput(const int16_t* pcmBlock, unsigned blockSize, uint32_t tickMs,
        unsigned mixCount);  

    /**
     * @returns Effective time of the status document for this call. Used
     * to track changes.
     */
    uint64_t getStatusDocStampMs() const;

    /**
     * @returns The latest live status document in JSON format. Generally
     * used for UI display.
     */
    json getStatusDoc() const;

    /**
     * @returns True if this call has audio levels to contribute.
     */
    bool hasAudioLevels() const;

    /**
     * @returns The latest audio levels in JSON format. Generally used for 
     * UI display.
     */
    json getLevelsDoc() const;

    /**
     * @return The latest link report for this call.
     */
    std::string getLinkReport() const { return _linkReport; }

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

    bool _active = false;
    unsigned _lineId = 0;
    unsigned _callId = 0;
    std::string _remoteNodeNumber;

    // These are the IDs for the call from the perspective of the Bridge
    // and are used when communicating with things like the TTS or the
    // network tester.
    unsigned _bridgeLineId = 0;
    unsigned _bridgeCallId = 0;

    // #### TODO: MOVE TO BRIDGE
    unsigned _ttsLineId = 0;
    unsigned _netTestLineId = 0;
    std::string _netTestBindAddr;
    // #### TODO: MOVE TO BRIDGE

    bool _echo = false;
    bool _sourceAddrValidated = false;
    Mode _mode = Mode::NORMAL;
    bool _permanent = false;

    BridgeIn _bridgeIn;
    BridgeOut _bridgeOut;

    // The audio waiting to be sent to the caller in PCM16 48K format.
    std::queue<PCM16Frame> _playQueue;

    // Used to gather DTMF symbols from the peer
    std::string _dtmfAccumulator;
    // The last time a DTMF symbol was received
    uint64_t _lastDtmfRxMs;

    // The latest list of linked nodes received from this call's peer.
    std::string _linkReport;
    // The last time the link report changed
    uint64_t _linkReportChangeMs;

    // The latest input talker ID asserted by this call's peer.
    std::string _talkerId;
    // The last time the input talker ID changed
    uint64_t _talkerIdChangeMs;

    // The most recent output talker ID that has been asserted.
    std::string _outputTalkerId;

    // The latest keyed node number reported by this call's peer.
    std::string _keyedNode;
    // The last time the keyed node changed
    uint64_t _keyedNodeChangeMs;

    // Audio levels
    int _rx0Db = 0;
    int _rx1Db = 0;
    int _tx0Db = 0;
    int _tx1Db = 0;

    void _processTTSAudio(const Message& msg);

    void _signalTalker();

    // ----- Normal Mode Related ----------------------------------------------

    void _processDtmfCommand(const std::string& cmd);

    void _processNormalAudio(const Message& msg);

    // This is the call's contribution to the conference when in normal mode.
    // IMPORTANT: All of the signaling has been handled ahead of this point
    // so _stageIn will either be silence or audio.  
    int16_t _stageIn[BLOCK_SIZE_48K];
    // Indicates whether any input was provided during this tick
    bool _stageInSet = false;

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

    void _enterParrotMode();
    void _processParrotAudio(const Message& msg);
    void _parrotAudioRateTick(uint32_t tickMs);
    void _processParrotTTSAudio(const Message& msg);

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

    // The start of the parrot session, used to manage session timeout
    uint64_t _parrotStartMs = 0;

    // Last time the parrot received audio. Used to detect end of 
    // transmission.
    uint32_t _lastAudioRxMs = 0;

    // The audio captured from the caller
    std::queue<PCM16Frame> _captureQueue;
    unsigned _captureQueueDepth = 0;

    enum ParrotState {
        NONE,
        ACTIVE,
        CONNECTED,
        WAITING_FOR_NET_TEST,
        GREETING_0,
        TTS_GREETING_0,
        PLAYING_GREETING_0,
        GREETING_1,
        TTS_GREETING_1,
        PLAYING_GREETING_1,
        WAITING_FOR_RECORD,
        RECORDING,
        PAUSE_AFTER_RECORD,
        TTS_AFTER_RECORD,
        PLAYING_AFTER_RECORD,
        TTS_GOODBYE,
        PLAYING_GOODBYE,
        TIMEDOUT
    };

    ParrotState _parrotState = ParrotState::NONE;
    uint32_t _parrotStateStartMs = 0;
    uint32_t _lastUnkeyProcessedMs = 0;
    Poker::Result _netTestResult;
    static constexpr const char* PARROT_TALKER_ID = "ASL Parrot";
    // The talker ID is captured at the end of the recording period
    std::string _recordedTalkerId;
};

    }
}
