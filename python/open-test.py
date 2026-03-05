import socket 
import time
from enum import Enum
import base64
import struct 
import iax

def make_OPEN_frame(source_call: int, dest_call: int, timestamp: int, 
    out_seq: int, in_seq: int, target_node: str):
    result = iax.make_frame_header(source_call, dest_call, timestamp, out_seq, in_seq,
        6, 0x23)
    result += iax.encode_information_elements({ 0x1: target_node.encode("utf-8") })
    return result

def make_PONG_frame(source_call: int, dest_call: int, timestamp: int, 
    out_seq: int, in_seq: int, apparent_addr: str):
    result = iax.make_frame_header(source_call, dest_call, timestamp, out_seq, in_seq,
        6, 0x03)
    if apparent_addr:
        result += iax.encode_information_elements({ 0x12: apparent_addr.encode("utf-8")})
    return result

def make_POKE_frame(source_call: int, dest_call: int, timestamp: int, 
    out_seq: int, in_seq: int):
    result = iax.make_frame_header(source_call, dest_call, timestamp, out_seq, in_seq,
        6, 0x1e)
    return result

# -----------------------------------------------------------------------------

UDP_IP = "0.0.0.0" 
UDP_PORT = 4570
BUFFER_SIZE = 1024

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"UDP server listening on {UDP_IP}:{UDP_PORT}")

broker_addr = ("52.8.197.124", 4570 )

# Send an open request
m = make_OPEN_frame(0, 0, 9999, 0, 0, "672732")
print("Sending OPENREQ", m, "to", broker_addr)
sock.sendto(m, broker_addr)

# Listen for incoming datagrams
while True:

    # recvfrom() returns both the data and the address of the sender
    frame, addr = sock.recvfrom(BUFFER_SIZE) 
    
    print(f"Received message: {frame} from {addr}")

    # Normal POKE handling
    if iax.is_POKE_frame(frame):
        ies = iax.decode_information_elements(frame[12:])
        ts = iax.get_full_timestamp(frame)
        print("Got POKE", ts, ies)

        # Make up a PONG with the apparent address
        apparent_addr = addr[0] + ":" + str(addr[1])
        print("Providing apparent addr", apparent_addr)
        m = make_PONG_frame(0, 0, 9999, 0, 0, apparent_addr)
        # Move the TARGET ADDR 2 to TARGET ADDR if it's there
        if 0x22 in ies:
            m += iax.encode_information_elements({ 0x21: ies[ 0x22 ] })

        print("Sending", m, "to", addr)
        sock.sendto(m, addr)

    elif iax.is_PONG_frame(frame):
        ies = iax.decode_information_elements(frame[12:])
        ts = iax.get_full_timestamp(frame)
        print("Got PONG", ts, ies)

    # The OPENRES response from our original OPEN
    else:
        # Make an address/port
        tokens = frame.decode("utf-8").split(":")
        print(tokens)
        addr = (tokens[0], int(tokens[1]))

        # POKE immediately to see what happens
        m = make_POKE_frame(0, 0, 6666, 0, 0)
        print("Sending direct POKE", m, "to", addr)
        sock.sendto(m, addr)







