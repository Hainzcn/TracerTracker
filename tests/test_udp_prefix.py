import socket
import time
import math

UDP_IP = "127.0.0.1"
UDP_PORT = 8888

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Starting UDP sender on {UDP_IP}:{UDP_PORT} with prefix 'G:'")

t = 0
try:
    while True:
        # Send normal data (no prefix) for Point 1 & 2
        # x1, y1, z1, x2, y2, z2
        x1 = 10 * math.cos(t)
        y1 = 10 * math.sin(t)
        z1 = t
        
        x2 = 5 * math.cos(t * 2)
        y2 = 5 * math.sin(t * 2)
        z2 = t + 5
        
        msg_normal = f"{x1:.2f}, {y1:.2f}, {z1:.2f}, {x2:.2f}, {y2:.2f}, {z2:.2f}"
        sock.sendto(msg_normal.encode(), (UDP_IP, UDP_PORT))
        
        # Send prefixed data for Point G
        # G:x, y, z
        xg = 15 * math.cos(t * 0.5)
        yg = 15 * math.sin(t * 0.5)
        zg = -t
        
        msg_prefixed = f"G:{xg:.2f}, {yg:.2f}, {zg:.2f}"
        sock.sendto(msg_prefixed.encode(), (UDP_IP, UDP_PORT))
        
        print(f"Sent: [Normal] {msg_normal} | [Prefixed] {msg_prefixed}")
        
        t += 0.1
        time.sleep(0.05)

except KeyboardInterrupt:
    print("Stopped by user")
finally:
    sock.close()
