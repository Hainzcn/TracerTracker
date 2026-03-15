import numpy as np
from PySide6.QtCore import QObject, Signal
import time
import math

class PoseProcessor(QObject):
    # Signal emitted when position is updated: (name, x, y, z)
    position_updated = Signal(str, float, float, float)
    # Signal emitted for debug logging
    log_message = Signal(str)

    def __init__(self, config_loader):
        super().__init__()
        self.config_loader = config_loader
        self.reset()
        self.frame_count = 0

    def reset(self):
        self.velocity = np.zeros(3)
        self.position = np.zeros(3)
        self.last_time = time.time()
        self.initialized = False
        
        # Quaternion [w, x, y, z] representing rotation from Sensor Frame to Earth Frame
        self.q = np.array([1.0, 0.0, 0.0, 0.0])
        
        # Filter gain
        self.beta = 0.1
        
        # Gravity in Earth frame (m/s^2)
        self.gravity = 9.81

    def _initialize_orientation(self, acc, mag):
        """
        Calculate initial orientation (Quaternion) from Accelerometer and Magnetometer.
        """
        ax, ay, az = acc
        
        # 1. Calculate Pitch and Roll from Accelerometer
        # Roll (rotation around X-axis)
        roll = math.atan2(ay, az)
        
        # Pitch (rotation around Y-axis)
        # Note: atan2(-ax, ...) ensures correct quadrant
        pitch = math.atan2(-ax, math.sqrt(ay*ay + az*az))
        
        # 2. Calculate Yaw from Magnetometer (if available)
        yaw = 0.0
        if mag is not None:
            mx, my, mz = mag
            
            # Tilt compensation for Magnetometer
            # Rotate mag vector by roll and pitch to be horizontal
            # Hy = my * cos(roll) - mz * sin(roll)
            # Hx = mx * cos(pitch) + my * sin(pitch) * sin(roll) + mz * sin(pitch) * cos(roll)
            
            Hy = my * math.cos(roll) - mz * math.sin(roll)
            Hx = mx * math.cos(pitch) + my * math.sin(pitch) * math.sin(roll) + mz * math.sin(pitch) * math.cos(roll)
            
            yaw = math.atan2(-Hy, Hx)
            
        # 3. Convert Euler Angles to Quaternion
        # Order: Yaw (Z) -> Pitch (Y) -> Roll (X)
        cy = math.cos(yaw * 0.5)
        sy = math.sin(yaw * 0.5)
        cp = math.cos(pitch * 0.5)
        sp = math.sin(pitch * 0.5)
        cr = math.cos(roll * 0.5)
        sr = math.sin(roll * 0.5)

        w = cr * cp * cy + sr * sp * sy
        x = sr * cp * cy - cr * sp * sy
        y = cr * sp * cy + sr * cp * sy
        z = cr * cp * sy - sr * sp * cy

        self.q = np.array([w, x, y, z])
        # Normalize
        norm = np.linalg.norm(self.q)
        if norm > 0:
            self.q /= norm
            
        self.initialized = True
        self.log_message.emit(f"Initialized Orientation: Roll={math.degrees(roll):.1f}, Pitch={math.degrees(pitch):.1f}, Yaw={math.degrees(yaw):.1f}")

    def process(self, source, prefix, data):
        """
        Process incoming data packet.
        Identify if it contains ACC/GYR/MAG data and update pose.
        """
        # 1. Parse config to find data indices
        # We do this every time to support config reloading, or we could cache it.
        # Given the frequency, looking up a few keys is fast enough.
        
        points_config = self.config_loader.get("points", [])
        
        acc_vec = None
        gyr_vec = None
        mag_vec = None
        
        # Find the relevant points for this source/prefix
        for p in points_config:
            # Check source match
            p_source = p.get("source", "any")
            if p_source != "any" and p_source != source:
                continue
                
            # Check prefix match
            p_prefix = p.get("prefix")
            if p_prefix == "": p_prefix = None
            if p_prefix != prefix:
                continue
            
            purpose = p.get("purpose")
            if purpose == "accelerometer":
                acc_vec = self._extract_vector(p, data)
            elif purpose == "gyroscope":
                gyr_vec = self._extract_vector(p, data)
            elif purpose == "magnetic_field":
                mag_vec = self._extract_vector(p, data)

        # If we don't have at least accelerometer data, we can't calculate displacement
        if acc_vec is None:
            return

        current_time = time.time()
        dt = current_time - self.last_time
        self.last_time = current_time
        self.frame_count += 1
        
        # Limit dt to avoid huge jumps if data reception pauses
        if dt > 0.1:
            dt = 0.1
        elif dt < 0.001:
             # Too small time step, might be unstable or just skip
             pass

        # 2. Check if gravity needs to be removed
        # Heuristic: Check magnitude of acceleration
        acc_mag = np.linalg.norm(acc_vec)
        
        # If magnitude is close to 1g (9.8), it likely includes gravity.
        # If magnitude is small (near 0), it likely has gravity removed.
        has_gravity = False
        if abs(acc_mag - 9.8) < 2.0: # 1g +/- 2.0 m/s^2
            has_gravity = True
        elif abs(acc_mag) < 2.0:
            has_gravity = False # Already linear
        else:
            # Ambiguous case (e.g. high acceleration). 
            # Assume has_gravity if we haven't determined otherwise, 
            # or use previous state. For now, assume has_gravity for robustness 
            # as most raw sensors output gravity.
            has_gravity = True
            
        # Initialize orientation if this is the first frame with gravity
        if has_gravity and not self.initialized:
            self._initialize_orientation(acc_vec, mag_vec)
            # Skip the first frame integration to avoid jumps
            return

        linear_acc = acc_vec
        
        debug_msg = []
        
        # Only log every 10 frames to avoid spamming the UI
        should_log = (self.frame_count % 10 == 0)

        if should_log:
            debug_msg.append(f"DT: {dt*1000:.1f}ms | ACC: [{acc_vec[0]:.2f}, {acc_vec[1]:.2f}, {acc_vec[2]:.2f}] | MAG: {acc_mag:.2f}g")
            debug_msg.append(f"Gravity Removed: {'YES' if has_gravity else 'NO (Raw)'}")

        if has_gravity:
            # Update orientation if Gyro is available
            if gyr_vec is not None:
                # Convert gyro from deg/s to rad/s if necessary
                # Assuming config multiplier handles unit conversion to standard units.
                # Standard unit for Madgwick is rad/s.
                # If the user didn't specify, we assume rad/s.
                
                # Update quaternion
                if mag_vec is not None:
                    self.q = self._madgwick_update_9dof(self.q, gyr_vec, acc_vec, mag_vec, dt)
                    if should_log: debug_msg.append("Updated Q (9DOF)")
                else:
                    self.q = self._madgwick_update_6dof(self.q, gyr_vec, acc_vec, dt)
                    if should_log: debug_msg.append("Updated Q (6DOF)")
            
            # Rotate Acc (Body) to Earth Frame
            # a_earth = q * a_body * q_conj
            acc_earth = self._rotate_vector(acc_vec, self.q)
            
            # Remove gravity (assuming Earth frame Z is up, so gravity is [0, 0, -9.8] or [0, 0, 9.8]?)
            # Usually gravity points DOWN. So in Earth frame, accelerometer measures reaction force UP [0, 0, 9.8].
            # So we subtract [0, 0, 9.8].
            linear_acc = acc_earth - np.array([0.0, 0.0, 9.81])
            
            if should_log:
                debug_msg.append(f"Q: [{self.q[0]:.2f}, {self.q[1]:.2f}, {self.q[2]:.2f}, {self.q[3]:.2f}]")
                debug_msg.append(f"Acc Earth: [{acc_earth[0]:.2f}, {acc_earth[1]:.2f}, {acc_earth[2]:.2f}]")

        # 3. Integrate to get Velocity and Position
        # Simple Euler integration
        
        # Apply a simple drag/decay to velocity to prevent infinite drift (optional but recommended)
        # self.velocity *= 0.99 
        
        self.velocity += linear_acc * dt
        self.position += self.velocity * dt
        
        if should_log:
            debug_msg.append(f"Linear Acc: [{linear_acc[0]:.2f}, {linear_acc[1]:.2f}, {linear_acc[2]:.2f}]")
            debug_msg.append(f"Vel: [{self.velocity[0]:.2f}, {self.velocity[1]:.2f}, {self.velocity[2]:.2f}]")
            debug_msg.append(f"Pos: [{self.position[0]:.2f}, {self.position[1]:.2f}, {self.position[2]:.2f}]")
            self.log_message.emit(" | ".join(debug_msg))
        
        # Emit the new position
        # We name this point "Displacement Path"
        self.position_updated.emit("Displacement Path", self.position[0], self.position[1], self.position[2])

    def _extract_vector(self, config, data):
        try:
            x_cfg = config.get("x", {})
            y_cfg = config.get("y", {})
            z_cfg = config.get("z", {})
            
            x_idx = x_cfg.get("index", 0)
            y_idx = y_cfg.get("index", 1)
            z_idx = z_cfg.get("index", 2)
            
            x_mult = x_cfg.get("multiplier", 1.0)
            y_mult = y_cfg.get("multiplier", 1.0)
            z_mult = z_cfg.get("multiplier", 1.0)
            
            if len(data) > max(x_idx, y_idx, z_idx):
                return np.array([
                    float(data[x_idx]) * x_mult,
                    float(data[y_idx]) * y_mult,
                    float(data[z_idx]) * z_mult
                ])
        except Exception:
            pass
        return None

    def _rotate_vector(self, v, q):
        # Rotate vector v by quaternion q
        # v_rotated = q * v * q_conj
        # Optimized implementation
        w, x, y, z = q
        vx, vy, vz = v
        
        # Formula for rotation matrix from quaternion
        # R = [ 1-2y^2-2z^2, 2xy-2wz, 2xz+2wy ]
        #     [ 2xy+2wz, 1-2x^2-2z^2, 2yz-2wx ]
        #     [ 2xz-2wy, 2yz+2wx, 1-2x^2-2y^2 ]
        
        # Row 0
        rx = (1 - 2*y*y - 2*z*z) * vx + (2*x*y - 2*w*z) * vy + (2*x*z + 2*w*y) * vz
        # Row 1
        ry = (2*x*y + 2*w*z) * vx + (1 - 2*x*x - 2*z*z) * vy + (2*y*z - 2*w*x) * vz
        # Row 2
        rz = (2*x*z - 2*w*y) * vx + (2*y*z + 2*w*x) * vy + (1 - 2*x*x - 2*y*y) * vz
        
        return np.array([rx, ry, rz])

    def _madgwick_update_6dof(self, q, gyr, acc, dt):
        # Simplified Madgwick update for 6DOF (IMU)
        # q: current quaternion [w, x, y, z]
        # gyr: angular velocity [gx, gy, gz] (rad/s)
        # acc: acceleration [ax, ay, az] (normalized)
        # dt: time step
        
        q0, q1, q2, q3 = q
        gx, gy, gz = gyr
        ax, ay, az = acc
        
        # Normalize accelerometer measurement
        norm = math.sqrt(ax*ax + ay*ay + az*az)
        if norm == 0: return q
        ax /= norm
        ay /= norm
        az /= norm
        
        # Gradient decent algorithm corrective step
        _2q0 = 2.0 * q0
        _2q1 = 2.0 * q1
        _2q2 = 2.0 * q2
        _2q3 = 2.0 * q3
        _4q0 = 4.0 * q0
        _4q1 = 4.0 * q1
        _4q2 = 4.0 * q2
        _8q1 = 8.0 * q1
        _8q2 = 8.0 * q2
        q0q0 = q0 * q0
        q1q1 = q1 * q1
        q2q2 = q2 * q2
        q3q3 = q3 * q3

        # Objective function: f(q)
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0 * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az
        s2 = 4.0 * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az
        s3 = 4.0 * q1q1 * q3 - _2q1 * ax + 4.0 * q2q2 * q3 - _2q2 * ay
        
        # Normalize step magnitude
        norm = math.sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3)
        if norm > 0:
            s0 /= norm
            s1 /= norm
            s2 /= norm
            s3 /= norm

        # Compute rate of change of quaternion
        qDot0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz) - self.beta * s0
        qDot1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy) - self.beta * s1
        qDot2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx) - self.beta * s2
        qDot3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx) - self.beta * s3

        # Integrate to yield quaternion
        q0 += qDot0 * dt
        q1 += qDot1 * dt
        q2 += qDot2 * dt
        q3 += qDot3 * dt
        
        # Normalize quaternion
        norm = math.sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3)
        if norm > 0:
            q /= norm
            
        return np.array([q0, q1, q2, q3])

    def _madgwick_update_9dof(self, q, gyr, acc, mag, dt):
        # Madgwick update for 9DOF (MARG)
        q0, q1, q2, q3 = q
        gx, gy, gz = gyr
        ax, ay, az = acc
        mx, my, mz = mag

        # Normalize accelerometer measurement
        norm = math.sqrt(ax*ax + ay*ay + az*az)
        if norm == 0: return q
        ax /= norm
        ay /= norm
        az /= norm

        # Normalize magnetometer measurement
        norm = math.sqrt(mx*mx + my*my + mz*mz)
        if norm == 0: return self._madgwick_update_6dof(q, gyr, acc, dt)
        mx /= norm
        my /= norm
        mz /= norm

        # Auxiliary variables to avoid repeated arithmetic
        _2q0mx = 2.0 * q0 * mx
        _2q0my = 2.0 * q0 * my
        _2q0mz = 2.0 * q0 * mz
        _2q1mx = 2.0 * q1 * mx
        _2q0 = 2.0 * q0
        _2q1 = 2.0 * q1
        _2q2 = 2.0 * q2
        _2q3 = 2.0 * q3
        _2q0q2 = 2.0 * q0 * q2
        _2q2q3 = 2.0 * q2 * q3
        q0q0 = q0 * q0
        q0q1 = q0 * q1
        q0q2 = q0 * q2
        q0q3 = q0 * q3
        q1q1 = q1 * q1
        q1q2 = q1 * q2
        q1q3 = q1 * q3
        q2q2 = q2 * q2
        q2q3 = q2 * q3
        q3q3 = q3 * q3

        # Reference direction of Earth's magnetic field
        # h = q * m * q_conj (rotate mag to earth frame)
        hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3
        hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3
        hz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 - mz * q1q1 - mz * q2q2 + mz * q3q3
        
        # Normalize reference direction flux to have 0 vertical component in East, 
        # but here we assume North (x) and Vertical (z) components are non-zero.
        # Madgwick assumes East component of magnetic field is 0.
        bx = math.sqrt(hx * hx + hy * hy)
        bz = hz

        _2bx = 2.0 * bx
        _2bz = 2.0 * bz
        _4bx = 4.0 * bx
        _4bz = 4.0 * bz

        # Gradient decent algorithm corrective step
        s0 = -_2q2 * (2.0 * q1q3 - _2q0q2 - ax) + _2q1 * (2.0 * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
        s1 = _2q3 * (2.0 * q1q3 - _2q0q2 - ax) + _2q0 * (2.0 * q0q1 + _2q2q3 - ay) - 4.0 * q1 * (1 - 2.0 * q1q1 - 2.0 * q2q2 - az) + _2bz * q3 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
        s2 = -_2q0 * (2.0 * q1q3 - _2q0q2 - ax) + _2q3 * (2.0 * q0q1 + _2q2q3 - ay) - 4.0 * q2 * (1 - 2.0 * q1q1 - 2.0 * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
        s3 = _2q1 * (2.0 * q1q3 - _2q0q2 - ax) + _2q2 * (2.0 * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)

        norm = math.sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3)
        if norm > 0:
            s0 /= norm
            s1 /= norm
            s2 /= norm
            s3 /= norm

        # Compute rate of change of quaternion
        qDot0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz) - self.beta * s0
        qDot1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy) - self.beta * s1
        qDot2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx) - self.beta * s2
        qDot3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx) - self.beta * s3

        # Integrate
        q0 += qDot0 * dt
        q1 += qDot1 * dt
        q2 += qDot2 * dt
        q3 += qDot3 * dt
        
        # Normalize
        norm = math.sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3)
        if norm > 0:
            q /= norm
            
        return np.array([q0, q1, q2, q3])
