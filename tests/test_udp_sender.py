import socket
import time
import math

def send_test_data():
    UDP_IP = "127.0.0.1"
    UDP_PORT = 8888 # Updated to match config.json
    
    print(f"Starting UDP sender on {UDP_IP}:{UDP_PORT}")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    t = 0
    while True:
        try:
            # Generate a spiral path
            x = math.sin(t) * 10
            y = math.cos(t) * 10
            z = t * 2
            
            # Send 6 values: x, y, z, r, g, b (example)
            # We will use indices 0, 1, 2 for point 1, and 3, 4, 5 for point 2
            
            x2 = math.sin(t + math.pi) * 15
            y2 = math.cos(t + math.pi) * 15
            z2 = t * 1.5
            
            message = f"{x:.2f}, {y:.2f}, {z:.2f}, {x2:.2f}, {y2:.2f}, {z2:.2f}"
            
            sock.sendto(message.encode(), (UDP_IP, UDP_PORT))
            print(f"Sent: {message}")
            
            t += 0.05
            if t > 20: t = 0
            
            time.sleep(0.05)
            
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")
            break

if __name__ == "__main__":
    send_test_data()
