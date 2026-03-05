import base64
import struct 

# -----------------------------------------------------------------------------
# Utility functions for manipulating IAX frames

def is_full_frame(frame):
    return frame[0] & 0b10000000 == 0b10000000

def is_mini_voice_packet(frame):
    return frame[0] & 0b10000000 == 0b00000000

def get_full_source_call(frame):
    return ((frame[0] & 0b01111111) << 8) | frame[1]

def get_full_r_bit(frame):
    return frame[2] & 0b10000000 == 0b10000000

def get_full_dest_call(frame):
    return ((frame[2] & 0b01111111) << 8) | frame[3]

def get_full_timestamp(frame):
    return (frame[4] << 24) | (frame[5] << 16) | (frame[6] << 8) | frame[7]

def get_full_outseq(frame):
    return frame[8]

def get_full_inseq(frame):
    return frame[9]

def get_full_type(frame):
    return frame[10]

def get_full_subclass_c_bit(frame):
    return frame[11] & 0b10000000 == 0b10000000

def get_full_subclass(frame):
    return frame[11] & 0b01111111

# @param content In bytes
def make_information_element(id: int, content):
    result = bytearray()
    result += id.to_bytes(1, byteorder='big')
    result += len(content).to_bytes(1, byteorder='big')
    result += content
    return result

def encode_information_elements(ie_map: dict): 
    result = bytearray()
    for key in ie_map.keys():
        if not isinstance(key, int):
            raise Exception("Type error")
        result += make_information_element(key, ie_map[key])
    return result

def decode_information_elements(data: bytes):
    """
    Takes a byte array containing zero or more information elements
    and unpacks it into a dictionary. The key of the dictionary is 
    the integer element ID and the value of the dictionary is a byte
    array with the content of the element.
    """
    result = dict()
    state = 0
    working_id = 0
    working_length = 0
    working_data = None
    # Cycle across all data
    for b in data:
        if state == 0:
            working_id = b
            state = 1
        elif state == 1:
            working_length = b 
            working_data = bytearray()
            if working_length == 0:
                result[working_id] = working_data
                state = 0
            else:
                state = 2
        elif state == 2:
            working_data.append(b)
            if len(working_data) == working_length:
                result[working_id] = working_data
                state = 0
        else:
            raise Exception()
    # Sanity check - we should end in the zero state
    if state != 0:
        raise Exception("Data format error")
    return result

def is_POKE_frame(frame):
    return is_full_frame(frame) and \
        get_full_type(frame) == 6 and \
        get_full_subclass_c_bit(frame) == False and \
        get_full_subclass(frame) == 0x1e

def is_PONG_frame(frame):
    return is_full_frame(frame) and \
        get_full_type(frame) == 6 and \
        get_full_subclass_c_bit(frame) == False and \
        get_full_subclass(frame) == 0x03

# NOTE: Not in RFC!
def is_OPEN_frame(frame):
    return is_full_frame(frame) and \
        get_full_type(frame) == 6 and \
        get_full_subclass_c_bit(frame) == False and \
        get_full_subclass(frame) == 0x23

def make_frame_header(source_call: int, dest_call: int, timestamp: int, 
    out_seq: int, in_seq: int, frame_type: int, frame_subclass: int):
    result = bytearray()
    result += source_call.to_bytes(2, byteorder='big')
    result[0] = result[0] | 0b10000000
    result += dest_call.to_bytes(2, byteorder='big')
    result[2] = result[2] & 0b01111111
    result += timestamp.to_bytes(4, byteorder='big')
    result += out_seq.to_bytes(1, byteorder='big')
    result += in_seq.to_bytes(1, byteorder='big')
    # Type
    result += int(frame_type).to_bytes(1, byteorder='big')
    # Subclass
    result += int(frame_subclass).to_bytes(1, byteorder='big')
    return result
