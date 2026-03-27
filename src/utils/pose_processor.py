import math
import time

import numpy as np
from PySide6.QtCore import QObject, Signal

from src.utils.ins_math import (
    initialize_orientation,
    rotate_vector,
    quat_multiply,
    madgwick_update_6dof,
    madgwick_update_9dof,
    mahony_update_6dof,
    mahony_update_9dof,
    LowPassFilter,
    VerticalKalmanFilter,
    ZUPTDetector,
)


class PoseProcessor(QObject):
    position_updated = Signal(str, float, float, float)
    log_message = Signal(str)
    # (source, prefix, linear_acc, gyr, mag)  –  gyr/mag may be None
    parsed_data_updated = Signal(str, str, list, object, object)
    velocity_updated = Signal(float, float, float)
    # (madgwick_q: list[4], mahony_q: list[4])
    filter_quaternions_updated = Signal(list, list)

    def __init__(self, config_loader):
        super().__init__()
        self.config_loader = config_loader
        self.reset()
        self.frame_count = 0

    def reset(self):
        self.velocity = np.zeros(3)
        self.position = np.zeros(3)
        self.last_time = time.perf_counter()
        self.initialized = False

        self.q = np.array([1.0, 0.0, 0.0, 0.0])
        self.q_madgwick = np.array([1.0, 0.0, 0.0, 0.0])
        self.q_mahony = np.array([1.0, 0.0, 0.0, 0.0])
        self.mahony_integral_fb = np.zeros(3)
        self.gravity = self.config_loader.get("gravity_reference", 9.81)

        ins_cfg = self.config_loader.get_ins_config()

        madgwick_cfg = ins_cfg["madgwick"]
        self.beta = madgwick_cfg.get("beta", 0.1)

        mahony_cfg = ins_cfg["mahony"]
        self.mahony_kp = mahony_cfg.get("kp", 1.0)
        self.mahony_ki = mahony_cfg.get("ki", 0.0)

        yaw_offset = ins_cfg.get("filter_yaw_offset_deg", 90.0)
        half_yaw = math.radians(yaw_offset) / 2.0
        self._q_yaw_corr = np.array([
            math.cos(half_yaw), 0.0, 0.0, math.sin(half_yaw)
        ])

        kf_cfg = ins_cfg["kalman"]
        self.kf_enabled = kf_cfg.get("enabled", True)
        self.vkf = VerticalKalmanFilter(
            R=kf_cfg.get("measurement_noise_R", 0.5),
            sigma_a=kf_cfg.get("process_noise_sigma", 0.5),
        )

        zupt_cfg = ins_cfg["zupt"]
        self.zupt_enabled = zupt_cfg.get("enabled", True)
        self.zupt = ZUPTDetector(
            acc_threshold=zupt_cfg.get("acc_variance_threshold", 0.1),
            gyro_threshold=zupt_cfg.get("gyro_variance_threshold", 0.01),
            window_size=zupt_cfg.get("window_size", 20),
        )

        self.baro_lpf = LowPassFilter(
            alpha=ins_cfg.get("baro_lpf_alpha", 0.05),
        )
        self._baro_ref = None

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
        baro_alt = None

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
            elif purpose == "barometer":
                baro_alt = self._extract_barometer(p, data)

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

        current_time = time.perf_counter()
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
            self.q_madgwick = q.copy()
            self.q_mahony = q.copy()
            self.mahony_integral_fb[:] = 0.0
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

            # 独立运行 Madgwick / Mahony 滤波器用于姿态对比展示
            if gyr_vec is not None:
                if mag_vec is not None:
                    self.q_madgwick = madgwick_update_9dof(
                        self.q_madgwick, gyr_vec, acc_vec, mag_vec, dt, self.beta
                    )
                    self.q_mahony = mahony_update_9dof(
                        self.q_mahony, gyr_vec, acc_vec, mag_vec, dt,
                        kp=self.mahony_kp, ki=self.mahony_ki,
                        integral_fb=self.mahony_integral_fb,
                    )
                else:
                    self.q_madgwick = madgwick_update_6dof(
                        self.q_madgwick, gyr_vec, acc_vec, dt, self.beta
                    )
                    self.q_mahony = mahony_update_6dof(
                        self.q_mahony, gyr_vec, acc_vec, dt,
                        kp=self.mahony_kp, ki=self.mahony_ki,
                        integral_fb=self.mahony_integral_fb,
                    )

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

        # ---- ZUPT 零速检测 ----
        is_stationary = False
        if self.zupt_enabled and gyr_vec is not None:
            acc_norm_sq = float(acc_vec[0] ** 2 + acc_vec[1] ** 2 + acc_vec[2] ** 2)
            gyro_norm = float(math.sqrt(
                gyr_vec[0] ** 2 + gyr_vec[1] ** 2 + gyr_vec[2] ** 2
            ))
            is_stationary = self.zupt.update(acc_norm_sq, gyro_norm)

        # ---- 垂直通道：卡尔曼滤波 ----
        if self.kf_enabled:
            a_z = float(linear_acc[2])
            self.vkf.predict(dt, a_z)

            if baro_alt is not None:
                baro_filtered = self.baro_lpf.update(baro_alt)
                if self._baro_ref is None:
                    self._baro_ref = baro_filtered
                delta_h = baro_filtered - self._baro_ref
                self.vkf.update(delta_h)

            if is_stationary:
                self.vkf.apply_zupt()

            self.position[2] = self.vkf.x[0]
            self.velocity[2] = self.vkf.x[1]
        else:
            self.velocity[2] += linear_acc[2] * dt
            self.position[2] += self.velocity[2] * dt

        # ---- 水平通道：积分 + ZUPT ----
        self.velocity[0] += linear_acc[0] * dt
        self.velocity[1] += linear_acc[1] * dt

        if is_stationary:
            self.velocity[0] = 0.0
            self.velocity[1] = 0.0
            if not self.kf_enabled:
                self.velocity[2] = 0.0
            if should_log:
                debug_msg.append("ZUPT: stationary detected, velocity zeroed")

        self.position[0] += self.velocity[0] * dt
        self.position[1] += self.velocity[1] * dt

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
            if self.kf_enabled:
                debug_msg.append(
                    f"KF h={self.vkf.x[0]:.3f} v={self.vkf.x[1]:.3f} "
                    f"a={self.vkf.x[2]:.3f}"
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
        self.filter_quaternions_updated.emit(
            quat_multiply(self._q_yaw_corr, self.q_madgwick).tolist(),
            quat_multiply(self._q_yaw_corr, self.q_mahony).tolist(),
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

    def _extract_barometer(self, config, data):
        """从数据中提取气压计高度值 (m)。"""
        try:
            alt_cfg = config.get("altitude", {})
            alt_idx = alt_cfg.get("index", 0)
            alt_mult = alt_cfg.get("multiplier", 1.0)
            if len(data) > alt_idx:
                val = float(data[alt_idx]) * alt_mult
                if val != 0.0:
                    return val
        except (IndexError, ValueError, TypeError, KeyError) as e:
            self.log_message.emit(
                f"Error extracting barometer for {config.get('name')}: {e}"
            )
        return None
