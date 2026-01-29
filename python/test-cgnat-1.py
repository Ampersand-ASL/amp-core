import socket 

UDP_IP = "0.0.0.0" 
UDP_PORT = 4570
BUFFER_SIZE = 1024

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind the socket to the address and port
sock.bind((UDP_IP, UDP_PORT))

print(f"UDP server listening on {UDP_IP}:{UDP_PORT}")

# Ping out to establish path

target_addr = "52.8.197.124"
target_port = 4570
message  = b"Hello, Izzy!"
sock.sendto(message, (target_addr, target_port))

# Listen for incoming datagrams
while True:

    # recvfrom() returns both the data and the address of the sender
    data, addr = sock.recvfrom(BUFFER_SIZE) 
    
    # Decode the message for printing (assuming UTF-8 encoding)
    message = data.decode('utf-8')

    print(f"Received message: {message!r} from {addr}")
 