import math

def hex_str_to_int(hex_str):
    """16进制字符串转10进制整数"""
    try:
        return int(hex_str.strip(), 16)
    except ValueError:
        raise ValueError(f"无效的16进制字符串：{hex_str}")

def hex_to_int16(low_hex, high_hex):
    """低字节在前，转换为int16有符号整数"""
    low = hex_str_to_int(low_hex)
    high = hex_str_to_int(high_hex)
    val = (high << 8) | low
    if val > 32767: 
        val -= 65536
    return val

def hex_to_uint16(low_hex, high_hex):
    """低字节在前，转换为uint16无符号整数"""
    low = hex_str_to_int(low_hex)
    high = hex_str_to_int(high_hex)
    return (high << 8) | low  

def hex_to_int32(b0_hex, b1_hex, b2_hex, b3_hex):
    """低字节在前，转换为int32有符号整数"""
    b0 = hex_str_to_int(b0_hex)
    b1 = hex_str_to_int(b1_hex)
    b2 = hex_str_to_int(b2_hex)
    b3 = hex_str_to_int(b3_hex)
    val = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0
    if val > 2147483647:
        val -= 4294967296
    return val

def calculate_checksum(frame_bytes):
    """计算帧的校验和"""
    sum_val = sum(frame_bytes)
    return sum_val % 256

def split_raw_data(raw_hex_str):
    """将连续的16进制字符串拆分为独立帧（按0x55 0x55帧头）"""
    hex_vals = [hex_str_to_int(s) for s in raw_hex_str.strip().split() if s]
    frames = []
    frame_start = -1
    
    for i in range(len(hex_vals)-1):
        if hex_vals[i] == 0x55 and hex_vals[i+1] == 0x55:
            if frame_start != -1:
                frames.append(hex_vals[frame_start:i])
            frame_start = i
    
    if frame_start != -1:
        frames.append(hex_vals[frame_start:])
    
    valid_frames = [f for f in frames if len(f) >= 7]
    return valid_frames


def parse_attitude_frame(frame_data):
    """
    解析姿态角帧（ID=0x01，数据长度=0x06）
    :param frame_data: 数据段的16进制字符串列表
    :return: 姿态角字典（单位：°）
    """
    if len(frame_data) != 6:
        raise ValueError(f"姿态角帧数据段长度错误，需6字节，实际{len(frame_data)}字节")
    
    roll = hex_to_int16(frame_data[0], frame_data[1]) / 32768 * 180
    pitch = hex_to_int16(frame_data[2], frame_data[3]) / 32768 * 180
    yaw = hex_to_int16(frame_data[4], frame_data[5]) / 32768 * 180
    
    return {
        "帧类型": "姿态角",
        "Roll(横滚)(°)": round(roll, 2),
        "Pitch(俯仰)(°)": round(pitch, 2),
        "Yaw(航向)(°)": round(yaw, 2)
    }

def parse_quaternion_frame(frame_data):
    """
    解析四元数帧（ID=0x02，数据长度=0x08）
    :param frame_data: 数据段的16进制字符串列表
    :return: 四元数字典
    """
    if len(frame_data) != 8:
        raise ValueError(f"四元数帧数据段长度错误，需8字节，实际{len(frame_data)}字节")
    
    q0 = hex_to_int16(frame_data[0], frame_data[1]) / 32768
    q1 = hex_to_int16(frame_data[2], frame_data[3]) / 32768
    q2 = hex_to_int16(frame_data[4], frame_data[5]) / 32768
    q3 = hex_to_int16(frame_data[6], frame_data[7]) / 32768
    
    return {
        "帧类型": "四元数",
        "q0": round(q0, 4),
        "q1": round(q1, 4),
        "q2": round(q2, 4),
        "q3": round(q3, 4)
    }

def parse_gyro_acc_frame(frame_data, acc_fsr=4, gyro_fsr=2000):
    """
    解析陀螺仪+加速度计帧（ID=0x03，数据长度=0x0C）
    :param frame_data: 数据段的16进制字符串列表（12字节）
    :param acc_fsr: 加速度计满量程（默认4G）
    :param gyro_fsr: 陀螺仪满量程（默认2000°/s）
    :return: 陀螺仪+加速度计字典（加速度：m/s²，陀螺仪：°/s）
    """
    if len(frame_data) != 12:
        raise ValueError(f"陀螺仪+加速度帧数据段长度错误，需12字节，实际{len(frame_data)}字节")
    
    ax = hex_to_int16(frame_data[0], frame_data[1]) / 32768 * acc_fsr * 9.8
    ay = hex_to_int16(frame_data[2], frame_data[3]) / 32768 * acc_fsr * 9.8
    az = hex_to_int16(frame_data[4], frame_data[5]) / 32768 * acc_fsr * 9.8
    
    gx = hex_to_int16(frame_data[6], frame_data[7]) / 32768 * gyro_fsr
    gy = hex_to_int16(frame_data[8], frame_data[9]) / 32768 * gyro_fsr
    gz = hex_to_int16(frame_data[10], frame_data[11]) / 32768 * gyro_fsr
    
    return {
        "帧类型": "陀螺仪+加速度计",
        "加速度计": {
            "Ax(m/s²)": round(ax, 4),
            "Ay(m/s²)": round(ay, 4),
            "Az(m/s²)": round(az, 4)
        },
        "陀螺仪": {
            "Gx(°/s)": round(gx, 4),
            "Gy(°/s)": round(gy, 4),
            "Gz(°/s)": round(gz, 4)
        }
    }

def parse_mag_temp_frame(frame_data):
    """
    解析磁力计+温度帧（ID=0x04，数据长度=0x08）
    :param frame_data: 数据段的16进制字符串列表（8字节）
    :return: 磁力计+温度字典（磁力计：原始值，温度：℃）
    """
    if len(frame_data) != 8:
        raise ValueError(f"磁力计+温度帧数据段长度错误，需8字节，实际{len(frame_data)}字节")
    
    mx = hex_to_int16(frame_data[0], frame_data[1])
    my = hex_to_int16(frame_data[2], frame_data[3])
    mz = hex_to_int16(frame_data[4], frame_data[5])
    
    temp = hex_to_int16(frame_data[6], frame_data[7]) / 100
    
    return {
        "帧类型": "磁力计+温度",
        "磁力计": {
            "Mx(原始值)": mx,
            "My(原始值)": my,
            "Mz(原始值)": mz
        },
        "温度(℃)": round(temp, 2)
    }

def parse_baro_alt_temp_frame(frame_data):
    """
    解析气压计+海拔+温度帧（ID=0x05，数据长度=0x0A）
    :param frame_data: 数据段的16进制字符串列表（10字节）
    :return: 气压计+海拔+温度字典（气压：Pa，海拔：m，温度：℃）
    """
    if len(frame_data) != 10:
        raise ValueError(f"气压计+海拔+温度帧数据段长度错误，需10字节，实际{len(frame_data)}字节")
    
    pressure = hex_to_int32(frame_data[0], frame_data[1], frame_data[2], frame_data[3])
    
    altitude_cm = hex_to_int32(frame_data[4], frame_data[5], frame_data[6], frame_data[7])
    altitude_m = altitude_cm / 100
    
    temp = hex_to_int16(frame_data[8], frame_data[9]) / 100
    
    return {
        "帧类型": "气压计+海拔+温度",
        "气压(Pa)": pressure,
        "海拔(m)": round(altitude_m, 2),
        "温度(℃)": round(temp, 2)
    }

def parse_ext_port_frame(frame_data, port_mode="analog"):
    """
    解析扩展端口状态帧（ID=0x06，数据长度=0x08）
    :param frame_data: 数据段的16进制字符串列表（8字节）
    :param port_mode: 端口模式（analog=模拟输入，digital=数字输入/输出，pwm=PWM输出）
    :return: 扩展端口字典（模拟：V，数字：电平，PWM：μs）
    """
    if len(frame_data) != 8:
        raise ValueError(f"扩展端口帧数据段长度错误，需8字节，实际{len(frame_data)}字节")
    
    d0 = hex_to_uint16(frame_data[0], frame_data[1])
    d1 = hex_to_uint16(frame_data[2], frame_data[3])
    d2 = hex_to_uint16(frame_data[4], frame_data[5])
    d3 = hex_to_uint16(frame_data[6], frame_data[7])
    
    port_data = {"帧类型": "扩展端口", "端口模式": port_mode}
    if port_mode == "analog":
        port_data["D0(V)"] = round(d0 / 4095 * 3.3, 4)
        port_data["D1(V)"] = round(d1 / 4095 * 3.3, 4)
        port_data["D2(V)"] = round(d2 / 4095 * 3.3, 4)
        port_data["D3(V)"] = round(d3 / 4095 * 3.3, 4)
    elif port_mode == "digital":
        port_data["D0(电平)"] = 1 if d0 > 0 else 0
        port_data["D1(电平)"] = 1 if d1 > 0 else 0
        port_data["D2(电平)"] = 1 if d2 > 0 else 0
        port_data["D3(电平)"] = 1 if d3 > 0 else 0
    elif port_mode == "pwm":
        port_data["D0(μs)"] = d0
        port_data["D1(μs)"] = d1
        port_data["D2(μs)"] = d2
        port_data["D3(μs)"] = d3
    else:
        raise ValueError(f"不支持的端口模式：{port_mode}，可选：analog/digital/pwm")
    
    return port_data

def parse_ms901m_raw_data(raw_hex_str, ext_port_mode="analog"):
    """
    解析ATK-MS901M原始16进制数据流（统一入口，自动识别帧类型）
    :param raw_hex_str: 连续的16进制字符串（如用户提供的"55 55 01 06 39 1D ..."）
    :param ext_port_mode: 扩展端口模式（默认模拟输入）
    :return: 解析结果列表（每一项为一个帧的解析结果，含校验状态）
    """
    frames = split_raw_data(raw_hex_str)
    parse_results = []
    
    for idx, frame in enumerate(frames):
        try:
            frame_head1 = frame[0]
            frame_head2 = frame[1]
            frame_id = frame[2]
            data_len = frame[3]
            checksum = frame[-1]
            data_segment = frame[4:-1]
            
            if frame_head1 != 0x55 or frame_head2 != 0x55:
                parse_results.append({"帧序号": idx+1, "状态": "无效", "原因": "帧头错误"})
                continue
            if len(data_segment) != data_len:
                parse_results.append({"帧序号": idx+1, "状态": "无效", "原因": f"数据段长度不匹配，需{data_len}字节，实际{len(data_segment)}字节"})
                continue
            
            checksum_calc = calculate_checksum(frame[:-1])
            if checksum_calc != checksum:
                parse_results.append({
                    "帧序号": idx+1, "状态": "无效", "原因": f"校验和错误，计算值={hex(checksum_calc)}, 帧内值={hex(checksum)}"
                })
                continue
            
            data_segment_hex = [hex(val)[2:].zfill(2).upper() for val in data_segment]
            
            if frame_id == 0x01:
                result = parse_attitude_frame(data_segment_hex)
            elif frame_id == 0x02:
                result = parse_quaternion_frame(data_segment_hex)
            elif frame_id == 0x03:
                result = parse_gyro_acc_frame(data_segment_hex)
            elif frame_id == 0x04:
                result = parse_mag_temp_frame(data_segment_hex)
            elif frame_id == 0x05:
                result = parse_baro_alt_temp_frame(data_segment_hex)
            elif frame_id == 0x06:
                result = parse_ext_port_frame(data_segment_hex, ext_port_mode)
            else:
                result = {"帧类型": "未知帧", "ID": hex(frame_id)}
            
            result["帧序号"] = idx+1
            result["状态"] = "有效"
            parse_results.append(result)
        
        except Exception as e:
            parse_results.append({"帧序号": idx+1, "状态": "解析失败", "原因": str(e)})
    
    return parse_results


# ---------------------------------------------------------------------------
# ATK-MS901M 实时二进制流解析器
# ---------------------------------------------------------------------------

class MS901MStreamParser:
    """
    ATK-MS901M 二进制 UART 协议的状态字节流解析器。
    通过 feed() 接收原始字节，提取并验证帧，汇总每种帧类型的最新数据，
    并在收到 0x03 帧（陀螺仪+加速度）时输出合并的浮点列表快照。

    合并输出索引映射：
      [0-2]   ax, ay, az       (m/s², 来自 0x03 帧)
      [3-5]   gx, gy, gz       (rad/s, 来自 0x03 帧)
      [6-9]   q0, q1, q2, q3   (来自 0x02 帧)
      [10-12] mx, my, mz       (原始值, 来自 0x04 帧)
      [13]    温度             (°C, 来自 0x04 帧)
      [14-16] roll, pitch, yaw (°, 来自 0x01 帧)
      [17]    气压             (Pa, 来自 0x05 帧)
      [18]    海拔             (m, 来自 0x05 帧)
    """

    HEADER = bytes([0x55, 0x55])

    def __init__(self, acc_fsr=4, gyro_fsr=2000):
        self.acc_fsr = acc_fsr
        self.gyro_fsr = gyro_fsr
        self._buffer = bytearray()
        self._latest = {}

    @staticmethod
    def _to_int16(low, high):
        val = (high << 8) | low
        if val > 32767:
            val -= 65536
        return val

    @staticmethod
    def _to_uint16(low, high):
        return (high << 8) | low

    @staticmethod
    def _to_int32(b0, b1, b2, b3):
        val = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0
        if val > 2147483647:
            val -= 4294967296
        return val

    def feed(self, data):
        """
        将 *data* (bytes / bytearray) 追加到内部缓冲区，解析所有完整帧，
        并返回合并后的快照浮点列表（每收到一个 0x03 触发帧返回一个）。
        """
        self._buffer.extend(data)
        snapshots = []

        while True:
            result = self._try_extract_frame()
            if result is None:
                break

            frame_id, payload = result
            parsed = self._parse_frame(frame_id, payload)
            if parsed is not None:
                self._latest[frame_id] = parsed
                if frame_id == 0x03:
                    snap = self._build_snapshot()
                    if snap is not None:
                        snapshots.append(snap)

        if len(self._buffer) > 4096:
            self._buffer = self._buffer[-2048:]

        return snapshots

    @staticmethod
    def format_debug(snapshot):
        """返回快照列表的人类可读摘要。"""
        if snapshot is None or len(snapshot) < 19:
            return "无效快照"
        return (
            f"ACC({snapshot[0]:.2f},{snapshot[1]:.2f},{snapshot[2]:.2f}) "
            f"GYR({snapshot[3]:.2f},{snapshot[4]:.2f},{snapshot[5]:.2f}) "
            f"Q({snapshot[6]:.3f},{snapshot[7]:.3f},{snapshot[8]:.3f},{snapshot[9]:.3f}) "
            f"MAG({snapshot[10]:.0f},{snapshot[11]:.0f},{snapshot[12]:.0f}) "
            f"T={snapshot[13]:.1f}C "
            f"RPY({snapshot[14]:.1f},{snapshot[15]:.1f},{snapshot[16]:.1f}) "
            f"P={snapshot[17]:.0f}Pa Alt={snapshot[18]:.2f}m"
        )

    def _try_extract_frame(self):
        """
        扫描缓冲区以查找下一个完整且校验和有效的帧。
        返回 (frame_id, data_bytes) 或 None。
        """
        while True:
            idx = self._buffer.find(self.HEADER)
            if idx < 0:
                if len(self._buffer) > 1:
                    self._buffer = self._buffer[-1:]
                return None

            if idx > 0:
                self._buffer = self._buffer[idx:]

            if len(self._buffer) < 4:
                return None

            frame_id = self._buffer[2]
            data_len = self._buffer[3]
            total_len = 4 + data_len + 1

            if data_len > 64:
                self._buffer = self._buffer[2:]
                continue

            if len(self._buffer) < total_len:
                return None

            frame_bytes = self._buffer[:total_len]
            expected_sum = sum(frame_bytes[:-1]) & 0xFF

            if expected_sum != frame_bytes[-1]:
                self._buffer = self._buffer[2:]
                continue

            self._buffer = self._buffer[total_len:]
            return (frame_id, bytes(frame_bytes[4:4 + data_len]))

    def _parse_frame(self, frame_id, d):
        try:
            if frame_id == 0x01:
                return self._parse_attitude(d)
            elif frame_id == 0x02:
                return self._parse_quaternion(d)
            elif frame_id == 0x03:
                return self._parse_gyro_acc(d)
            elif frame_id == 0x04:
                return self._parse_mag_temp(d)
            elif frame_id == 0x05:
                return self._parse_baro_alt(d)
        except (IndexError, ValueError):
            pass
        return None

    def _parse_attitude(self, d):
        if len(d) != 6:
            return None
        roll  = self._to_int16(d[0], d[1]) / 32768.0 * 180.0
        pitch = self._to_int16(d[2], d[3]) / 32768.0 * 180.0
        yaw   = self._to_int16(d[4], d[5]) / 32768.0 * 180.0
        return {"roll": roll, "pitch": pitch, "yaw": yaw}

    def _parse_quaternion(self, d):
        if len(d) != 8:
            return None
        q0 = self._to_int16(d[0], d[1]) / 32768.0
        q1 = self._to_int16(d[2], d[3]) / 32768.0
        q2 = self._to_int16(d[4], d[5]) / 32768.0
        q3 = self._to_int16(d[6], d[7]) / 32768.0
        return {"q0": q0, "q1": q1, "q2": q2, "q3": q3}

    def _parse_gyro_acc(self, d):
        if len(d) != 12:
            return None
        fsr_a = self.acc_fsr
        fsr_g = self.gyro_fsr
        ax = self._to_int16(d[0],  d[1])  / 32768.0 * fsr_a * 9.8
        ay = self._to_int16(d[2],  d[3])  / 32768.0 * fsr_a * 9.8
        az = self._to_int16(d[4],  d[5])  / 32768.0 * fsr_a * 9.8
        gx = self._to_int16(d[6],  d[7])  / 32768.0 * fsr_g * (math.pi / 180.0)
        gy = self._to_int16(d[8],  d[9])  / 32768.0 * fsr_g * (math.pi / 180.0)
        gz = self._to_int16(d[10], d[11]) / 32768.0 * fsr_g * (math.pi / 180.0)
        return {"ax": ax, "ay": ay, "az": az, "gx": gx, "gy": gy, "gz": gz}

    def _parse_mag_temp(self, d):
        if len(d) != 8:
            return None
        mx   = self._to_int16(d[0], d[1])
        my   = self._to_int16(d[2], d[3])
        mz   = self._to_int16(d[4], d[5])
        temp = self._to_int16(d[6], d[7]) / 100.0
        return {"mx": mx, "my": my, "mz": mz, "temp": temp}

    def _parse_baro_alt(self, d):
        if len(d) != 10:
            return None
        pressure = self._to_int32(d[0], d[1], d[2], d[3])
        altitude = self._to_int32(d[4], d[5], d[6], d[7]) / 100.0
        temp     = self._to_int16(d[8], d[9]) / 100.0
        return {"pressure": pressure, "altitude": altitude, "temp": temp}

    def _build_snapshot(self):
        """
        合并来自所有帧类型的最新解析数据。至少需要 0x03 帧。
        """
        imu = self._latest.get(0x03)
        if imu is None:
            return None

        quat = self._latest.get(0x02)
        mag  = self._latest.get(0x04)
        att  = self._latest.get(0x01)
        baro = self._latest.get(0x05)

        return [
            imu["ax"], imu["ay"], imu["az"],
            imu["gx"], imu["gy"], imu["gz"],
            quat["q0"] if quat else 1.0,
            quat["q1"] if quat else 0.0,
            quat["q2"] if quat else 0.0,
            quat["q3"] if quat else 0.0,
            float(mag["mx"]) if mag else 0.0,
            float(mag["my"]) if mag else 0.0,
            float(mag["mz"]) if mag else 0.0,
            mag["temp"]      if mag else 0.0,
            att["roll"]      if att else 0.0,
            att["pitch"]     if att else 0.0,
            att["yaw"]       if att else 0.0,
            float(baro["pressure"]) if baro else 0.0,
            baro["altitude"]        if baro else 0.0,
        ]
