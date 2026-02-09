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
#include <cstring>
#include <cassert>
#include "Message.h"

namespace kc1fsz {

Message::Message()
:   _type(Type::NONE),
    _format(0),
    _size(0),
    _origMs(0),
    _rxMs(0),
    _sourceBusId(0),
    _sourceCallId(0),
    _destBusId(0),
    _destCallId(0) {    
}

Message::Message(const Message& other) 
:   _type(other._type),
    _format(other._format),
    _size(other._size),
    _origMs(other._origMs),
    _rxMs(other._rxMs),
    _sourceBusId(other._sourceBusId),
    _sourceCallId(other._sourceCallId),
    _destBusId(other._destBusId),
    _destCallId(other._destCallId) {
}

Message& Message::operator=(const Message& other) {
    _type = other._type;
    _format = other._format;
    _size = other._size;
    _origMs = other._origMs;
    _rxMs = other._rxMs;
    _sourceBusId = other._sourceBusId;
    _sourceCallId = other._sourceCallId;
    _destBusId = other._destBusId;
    _destCallId = other._destCallId;
    return *this;
}

void Message::clear() {
    _type = Type::NONE;
    _format = 0;
    _size = 0;
    _origMs = 0;
    _rxMs = 0;
    _sourceBusId = 0;
    _sourceCallId = 0;
    _destBusId = 0;
    _destCallId = 0;
}

Message::Message(Type type, unsigned format, unsigned size, uint32_t origMs, uint32_t rxMs) 
:   _type(type),
    _format(format),
    _size(size),
    _origMs(origMs),
    _rxMs(rxMs),
    _sourceBusId(0),
    _sourceCallId(0),
    _destBusId(0),
    _destCallId(0) {  
}

MessageCarrier::MessageCarrier() 
:   Message() {
}

MessageCarrier::MessageCarrier(Type type, unsigned format, unsigned size, const uint8_t* body,
    uint32_t origMs, uint32_t rxMs) 
:   Message(type, format, size, origMs, rxMs) {
    if (size) {
        assert(size <= MAX_SIZE);
        // #### ADDED
        //_body = (uint8_t*)malloc(size);
        memcpy(_body, body, size);
    }
}

MessageCarrier::MessageCarrier(const MessageCarrier& other) 
:   Message(other) {
    if (other.size()) {
        assert(other.size() <= MAX_SIZE);
        // #### ADDED
        //_body = (uint8_t*)malloc(other.size());
        memcpy(_body, other.body(), other.size());
    }
}

MessageCarrier::MessageCarrier(const Message& other)
:   Message(other) {
    assert(other.size() <= MAX_SIZE);
    if (other.size()) {
        // #### ADDED
        //_body = (uint8_t*)malloc(other.size());
        memcpy(_body, other.body(), other.size());
    }
}

MessageCarrier::~MessageCarrier() {
    // #### ADDED
    //if (_body) {
    //    free(_body);
    //}
}

MessageCarrier& MessageCarrier::operator=(const MessageCarrier& other) {
    Message::operator=(other);
    // #### ADDED
    //if (_body) {
    //    free(_body);
    //    _body = 0;
    //}
    if (other.size()) {
        assert(other.size() <= MAX_SIZE);
        // #### ADDED
        //_body = (uint8_t*)malloc(other.size());
        memcpy(_body, other.body(), other.size());
    }
    return *this;
}

void MessageCarrier::clear() {
    Message::clear();
}

MessageWrapper::MessageWrapper(Type type, unsigned format, unsigned size, const uint8_t* body,
    uint32_t origMs, uint32_t rxMs) 
:   Message(type, format, size, origMs, rxMs),
    _body(body) { 
}

MessageEmpty::MessageEmpty() 
: Message() {
}

MessageEmpty::MessageEmpty(Type type, unsigned format, uint32_t origMs, uint32_t rxMs) 
:   Message(type, format, 0, origMs, rxMs) {
}

MessageEmpty::MessageEmpty(const MessageEmpty& other) 
:   Message(other) { }

}
