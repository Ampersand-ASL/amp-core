/**
 * Copyright (C) 2025, Bruce MacKinnon KC1FSZ, All Rights Reserved
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

class Log;

    namespace amp {

/**
 * This abstract interface defines the output side of the Sequencing
 * Buffer. Whenever the SequencingBuffer is ready to emit a frame (or 
 * request an interpolation) it will do so by calling the appropriate 
 * method on this interface.
 */
template <class T> class SequencingBufferSink {
public:

    /**
     * Called to play a signalling frame.
     * 
     * @param localMs The local clock that the signal applies to. This is provided 
     * for reference purposes and normally would be ignored - just process the signal
     * immediately on this method call.
     */
    virtual void playSignal(const T& frame,  uint32_t localMs) = 0;

    /**
     * Called to play a voice frame.
     * 
     * @param frame The voice content
     * @param localMs The local clock that the voice frame applies to. This is 
     * provided for reference purposes and normally would be ignored - just process 
     * the frame immediately on this method call.
     */
    virtual void playVoice(const T& frame, uint32_t localMs) = 0;

    /**
     * Called when voice interpolation is needed. 
     * @param localMs Same as for playVoice().
     * @param durationMs The duration of the interpolation needed in milliseconds.
     */
    virtual void interpolateVoice(uint32_t localMs, uint32_t durationMs) = 0;
};

/**
 * This is a C++ "concept" which will be used to place some requirements
 * on the types that can be used in the SequencingBuffer template below.
 * Basically, the objects that are controlled by the sequencing buffer
 * must support three methods.
 */        
template <typename T>
concept HasFrameTimes = requires (const T b) {
    {b.isVoice()} -> std::same_as<bool>; 
    {b.getOrigMs()} -> std::same_as<uint32_t>; 
    {b.getRxMs()} -> std::same_as<uint32_t>; 
};

/**
 * An abstract interface of a SequencingBuffer, often called a "jitter buffer."
 * This interface defines the way that an application interacts with the buffer.
 */
template<HasFrameTimes T> class SequencingBuffer {
public:

    /**
     * Clears all state and returns statistical parameters to initial condition.
     * This would typically be called at the beginning of a call.
     */
    virtual void reset() = 0;

    /**
     * Called when a frame is received.
     * 
     * @return true if the message was consumed, false if it was ignored and can be 
     * discarded (i.e. all full)
     */
    virtual bool consume(Log& log, const T& payload) = 0;

    /**
     * Should be called periodically (precisely on the audio tick interval) to ask the 
     * buffer to produce any outgoing frames that are due at the specified localMs.
     * On each call we would expect (a) zero or more signalling frames and (b) EITHER 
     * one voice frame or one interpolation request.
     * 
     * @param localMs 
     * @param sink Where the frames should be sent.
     */
    virtual void playOut(Log& log, uint32_t localMs, SequencingBufferSink<T>* sink) = 0;

    /**
     * @returns true if a talkspurt is actively being played.
     */
    virtual bool inTalkspurt() const = 0;
};
    }
}
