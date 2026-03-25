import numpy as np
from PySide6.QtCore import QObject, Signal
import time

from src.utils.ins_math import (
    initialize_orientation,
    rotate_vector,
    madgwick_update_6dof,
    madgwick_update_9dof,
)


class PoseProcessor(QObject):
    position_updated = Signal(str, float, float, float)
    log_message = Signal(str)
    # (source, prefix, linear_acc, gyr, mag)  –  gyr/mag may be None
    parsed_data_updated = Signal(str, str, list, object, object)
    velocity_updated = Signal(float, float, float)

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

        # 四元数 [w, x, y, z]: 传感器 -> 地球
        self.q = np.array([1.0, 0.0, 0.0, 0.0])

        self.beta = 0.1

        self.gravity = self.config_loader.get("gravity_reference", 9.81)

    # ------------------------------------------------------------------
    # 主入口点
    # ------------------------------------------------------------------

    def process(self, source, prefix, data):
        """处理输入数据包并更新位姿估计。"""
        points_config = self.config_loader.get("points", [])

        acc_vec = None
        gyr_vec = None
        mag_vec = None
        quat_vec = None

        matched_points = 0
        for p in points_config:
            p_source = p.get("source", "any")
            if p_source != "any" and p_source != source:
                continue

            p_prefix = p.get("prefix") or None
            current_prefix = prefix or None

            if p_prefix != current_prefix:
                continue

            matched_points += 1
            purpose = p.get("purpose")
            if purpose == "accelerometer":
                acc_vec = self._extract_vector(p, data)
            elif purpose == "gyroscope":
                gyr_vec = self._extract_vector(p, data)
            elif purpose == "magnetic_field":
                mag_vec = self._extract_vector(p, data)
            elif purpose == "quaternion":
                quat_vec = self._extract_quaternion(p, data)

        if acc_vec is None:
            if self.frame_count == 0:
                self.log_message.emit(
                    f"Warning: Received data from {source} (len={len(data)}) "
                    "but failed to extract Accelerometer vector."
                )
                if matched_points == 0:
                    self.log_message.emit(
                        f"  -> No config points matched source='{source}' and "
                        f"prefix='{prefix}' (normalized: "
                        f"'{'None' if not prefix else prefix}'). Check config."
                    )
                else:
                    self.log_message.emit(
                        f"  -> Matched {matched_points} config points, but "
                        "extraction failed. Check indices vs data length."
                    )
            return

        current_time = time.time()
        dt = current_time - self.last_time
        self.last_time = current_time
        self.frame_count += 1

        if dt > 0.1:
            dt = 0.1

        acc_mag = np.linalg.norm(acc_vec)

        has_gravity = False
        if abs(acc_mag - self.gravity) < 2.0:
            has_gravity = True
        elif abs(acc_mag) < 2.0:
            has_gravity = False
        else:
            has_gravity = True

        if has_gravity and not self.initialized and quat_vec is None:
            q, roll, pitch, yaw = initialize_orientation(acc_vec, mag_vec)
            self.q = q
            self.initialized = True
            self.log_message.emit(
                f"Initialized Orientation: Roll={roll:.1f}, "
                f"Pitch={pitch:.1f}, Yaw={yaw:.1f}"
            )
            return

        linear_acc = acc_vec

        debug_msg = []
        should_log = (self.frame_count % 10 == 0)

        if should_log:
            debug_msg.append(
                f"DT: {dt*1000:.1f}ms | ACC: [{acc_vec[0]:.2f}, "
                f"{acc_vec[1]:.2f}, {acc_vec[2]:.2f}] | MAG: {acc_mag:.2f}g"
            )
            debug_msg.append(
                f"Gravity Removed: {'YES' if has_gravity else 'NO (Raw)'}"
            )

        if has_gravity:
            if quat_vec is not None:
                self.q = quat_vec
                self.initialized = True
                if should_log:
                    debug_msg.append("Q from module (direct)")
            elif gyr_vec is not None:
                if mag_vec is not None:
                    self.q = madgwick_update_9dof(
                        self.q, gyr_vec, acc_vec, mag_vec, dt, self.beta
                    )
                    if should_log:
                        debug_msg.append("Updated Q (9DOF)")
                else:
                    self.q = madgwick_update_6dof(
                        self.q, gyr_vec, acc_vec, dt, self.beta
                    )
                    if should_log:
                        debug_msg.append("Updated Q (6DOF)")

            acc_earth = rotate_vector(acc_vec, self.q)
            linear_acc = acc_earth - np.array([0.0, 0.0, self.gravity])

            if should_log:
                debug_msg.append(
                    f"Q: [{self.q[0]:.2f}, {self.q[1]:.2f}, "
                    f"{self.q[2]:.2f}, {self.q[3]:.2f}]"
                )
                debug_msg.append(
                    f"Acc Earth: [{acc_earth[0]:.2f}, {acc_earth[1]:.2f}, "
                    f"{acc_earth[2]:.2f}]"
                )

        self.parsed_data_updated.emit(
            source,
            prefix if prefix else "",
            linear_acc.tolist(),
            gyr_vec.tolist() if gyr_vec is not None else None,
            mag_vec.tolist() if mag_vec is not None else None,
        )

        self.velocity += linear_acc * dt
        self.position += self.velocity * dt

        if should_log:
            debug_msg.append(
                f"Linear Acc: [{linear_acc[0]:.2f}, {linear_acc[1]:.2f}, "
                f"{linear_acc[2]:.2f}]"
            )
            debug_msg.append(
                f"Vel: [{self.velocity[0]:.2f}, {self.velocity[1]:.2f}, "
                f"{self.velocity[2]:.2f}]"
            )
            debug_msg.append(
                f"Pos: [{self.position[0]:.2f}, {self.position[1]:.2f}, "
                f"{self.position[2]:.2f}]"
            )
            self.log_message.emit(" | ".join(debug_msg))

        self.velocity_updated.emit(
            float(self.velocity[0]),
            float(self.velocity[1]),
            float(self.velocity[2]),
        )
        self.position_updated.emit(
            "Displacement Path",
            self.position[0],
            self.position[1],
            self.position[2],
        )

    # ------------------------------------------------------------------
    # Data extraction helpers
    # ------------------------------------------------------------------

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

            max_idx = max(x_idx, y_idx, z_idx)
            if len(data) > max_idx:
                return np.array([
                    float(data[x_idx]) * x_mult,
                    float(data[y_idx]) * y_mult,
                    float(data[z_idx]) * z_mult,
                ])
        except (IndexError, ValueError, TypeError, KeyError) as e:
            self.log_message.emit(
                f"Error extracting vector for {config.get('name')}: {e}"
            )
        return None

    def _extract_quaternion(self, config, data):
        """从数据中根据配置索引提取四元数 [w, x, y, z]。"""
        try:
            w_cfg = config.get("w", {})
            x_cfg = config.get("x", {})
            y_cfg = config.get("y", {})
            z_cfg = config.get("z", {})

            w_idx = w_cfg.get("index", 0)
            x_idx = x_cfg.get("index", 1)
            y_idx = y_cfg.get("index", 2)
            z_idx = z_cfg.get("index", 3)

            w_mult = w_cfg.get("multiplier", 1.0)
            x_mult = x_cfg.get("multiplier", 1.0)
            y_mult = y_cfg.get("multiplier", 1.0)
            z_mult = z_cfg.get("multiplier", 1.0)

            max_idx = max(w_idx, x_idx, y_idx, z_idx)
            if len(data) > max_idx:
                q = np.array([
                    float(data[w_idx]) * w_mult,
                    float(data[x_idx]) * x_mult,
                    float(data[y_idx]) * y_mult,
                    float(data[z_idx]) * z_mult,
                ])
                norm = np.linalg.norm(q)
                if norm > 0:
                    q /= norm
                return q
        except (IndexError, ValueError, TypeError, KeyError) as e:
            self.log_message.emit(
                f"Error extracting quaternion for {config.get('name')}: {e}"
            )
        return None
