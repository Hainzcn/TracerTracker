import socket
import time
import math

UDP_IP = "127.0.0.1"
UDP_PORT = 8888

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Starting comprehensive UDP test sender on {UDP_IP}:{UDP_PORT}")
print("This script sends simulated data to test:")
print("1. 3D Trajectory (Path tracking)")
print("2. Attitude Widget (Quaternions/Euler angles)")
print("3. Sensor Charts (ACC, RPY, ALT)")

t = 0
try:
    while True:
        # Simulate sensor data
        # According to MainWindow::updateOverlays and updateSensorCharts:
        # data[0..2]   : ACC (x, y, z)
        # data[3..5]   : GYRO (x, y, z)
        # data[6..9]   : Quaternion (w, x, y, z) - if configured
        # data[10..12] : MAG (x, y, z)
        # data[13]     : Temperature
        # data[14..16] : Euler RPY (Roll, Pitch, Yaw) in degrees
        # data[17]     : Pressure (Pa)
        # data[18]     : Altitude (m)
        
        # 1. Trajectory (using prefix 'P:' for position or just normal data if configured)
        # We will send a spiral trajectory
        px = 10 * math.cos(t)
        py = 10 * math.sin(t)
        pz = t * 2
        
        # 3. Euler Angles (RPY)
        roll = 45 * math.sin(t)
        pitch = 30 * math.cos(t * 0.5)
        yaw = (t * 20) % 360 - 180
        
        # 4. Quaternions (convert Euler to Quat for the attitude widget)
        cr = math.cos(math.radians(roll) * 0.5)
        sr = math.sin(math.radians(roll) * 0.5)
        cp = math.cos(math.radians(pitch) * 0.5)
        sp = math.sin(math.radians(pitch) * 0.5)
        cy = math.cos(math.radians(yaw) * 0.5)
        sy = math.sin(math.radians(yaw) * 0.5)
        
        qw = cr * cp * cy + sr * sp * sy
        qx = sr * cp * cy - cr * sp * sy
        qy = cr * sp * cy + sr * cp * sy
        qz = cr * cp * sy - sr * sp * cy
        
        # 2. ACC & GYR
        # To avoid Madgwick/Mahony filter jitter, we need to provide physically consistent ACC and GYR data.
        # Gyroscope: angular velocity in rad/s (approximate derivatives of Euler angles)
        gyr_x = math.radians(45 * math.cos(t))
        gyr_y = math.radians(-15 * math.sin(t * 0.5))
        gyr_z = math.radians(20)
        
        # Accelerometer: Gravity vector [0, 0, 1] rotated into body frame
        acc_x = 9.8 * 2.0 * (qx * qz - qw * qy)
        acc_y = 9.8 * 2.0 * (qy * qz + qw * qx)
        acc_z = 9.8 * (qw * qw - qx * qx - qy * qy + qz * qz)
        
        # 5. Altitude / Pressure
        alt = 100 + 50 * math.sin(t * 0.2)
        pressure = 101325 * math.pow(1 - 2.25577e-5 * alt, 5.25588)
        
        # Construct the 19-element data array
        # Format: ACC(3), GYR(3), QUAT(4), MAG(3), TEMP(1), RPY(3), PRESS(1), ALT(1)
        data = [
            acc_x, acc_y, acc_z,           # 0-2: ACC
            gyr_x, gyr_y, gyr_z,           # 3-5: GYR (consistent with rotation)
            qw, qx, qy, qz,                # 6-9: QUAT
            0.0, 0.0, 0.0,                 # 10-12: MAG (dummy)
            25.0,                          # 13: TEMP
            roll, pitch, yaw,              # 14-16: RPY
            pressure, alt                  # 17-18: PRESS, ALT
        ]
        
        # Format as CSV string
        msg_full = ", ".join(f"{v:.4f}" for v in data)
        # To trigger the MainWindow::updateOverlays and updateSensorCharts, 
        # we need to send it without prefix (or with a prefix that the app expects for sensors).
        # We will send it with prefix 'S:' and also configure the app to accept it, or just send it raw.
        # Let's send it raw (no prefix) so MainWindow::onDataReceived gets it as data.
        sock.sendto(msg_full.encode(), (UDP_IP, UDP_PORT))
        
        # Also send a prefixed position message to test trajectory drawing
        # According to config.json, prefix 'G' maps to Point G
        msg_pos = f"G:{px:.2f}, {py:.2f}, {pz:.2f}"
        sock.sendto(msg_pos.encode(), (UDP_IP, UDP_PORT))
        
        if int(t * 10) % 10 == 0:
            print(f"Sent t={t:.1f} | RPY: {roll:.1f}, {pitch:.1f}, {yaw:.1f} | ALT: {alt:.1f}")
            
        t += 0.05
        time.sleep(0.05) # 20Hz update rate

except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    sock.close()
