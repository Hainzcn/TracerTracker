import logging
import socket
import threading
import time
import serial
from PySide6.QtCore import QObject, Signal
from src.utils.atkms901m_resolver import MS901MStreamParser

logger = logging.getLogger(__name__)

class DataReceiver(QObject):
    """
    处理来自 UDP 和串口的数据接收。
    发出 'data_received' 信号，携带 (source, prefix, parsed_data)。
    """
    data_received = Signal(str, str, list)
    raw_data_received = Signal(str, str)
    
    def __init__(self, config_loader):
        super().__init__()
        self.config_loader = config_loader
        self.running = False
        self.udp_thread = None
        self.serial_thread = None
        self.udp_socket = None
        self.serial_port = None

    def start(self):
        """根据配置启动接收线程"""
        self.running = True
        
        udp_config = self.config_loader.get_udp_config()
        if udp_config.get('enabled', False):
            self.udp_thread = threading.Thread(target=self._udp_loop, args=(udp_config,), daemon=True)
            self.udp_thread.start()
            logger.info("UDP Receiver started on %s:%s", udp_config['ip'], udp_config['port'])
            
        serial_config = self.config_loader.get_serial_config()
        if serial_config.get('enabled', False):
            protocol = serial_config.get('protocol', 'csv')
            if protocol == 'atkms901m':
                target = self._serial_binary_loop
            else:
                target = self._serial_loop
            self.serial_thread = threading.Thread(target=target, args=(serial_config,), daemon=True)
            self.serial_thread.start()
            logger.info("Serial Receiver started on %s @ %s (protocol=%s)", serial_config['port'], serial_config['baudrate'], protocol)

    def stop(self):
        """停止所有接收线程"""
        self.running = False
        
        if self.udp_socket:
            self.udp_socket.close()
            
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            
        if self.udp_thread:
            self.udp_thread.join(timeout=1.0)
            
        if self.serial_thread:
            self.serial_thread.join(timeout=1.0)
            
        logger.info("Data receivers stopped.")

    def _parse_data(self, raw_data):
        """
        解析字符串中的逗号分隔值。
        支持可选的冒号结尾前缀（例如 "G:1,2,3"）。
        返回元组 (prefix, values_list)，如果解析失败则返回 None。
        """
        try:
            # 如果是字节流则进行解码
            if isinstance(raw_data, bytes):
                text = raw_data.decode('utf-8').strip()
            else:
                text = raw_data.strip()
                
            if not text:
                return None
            
            prefix = None
            csv_part = text
            
            # 检查前缀（例如 "G:..."）
            if ':' in text:
                parts = text.split(':', 1)
                prefix_candidate = parts[0].strip()
                # 将空字符串前缀视为 None
                if prefix_candidate:
                    prefix = prefix_candidate
                else:
                    prefix = None
                csv_part = parts[1].strip()
            else:
                prefix = None # 如果没有找到冒号，显式设置为 None
                
            # 解析 CSV
            if not csv_part:
                return None
                
            values = [float(x.strip()) for x in csv_part.split(',')]
            return (prefix, values)
        except (ValueError, UnicodeDecodeError, IndexError) as e:
            logger.debug("Error parsing data: %s", e)
            return None

    def _udp_loop(self, config):
        """接收 UDP 数据的循环"""
        ip = config.get('ip', '127.0.0.1')
        port = config.get('port', 5005)
        
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.bind((ip, port))
            self.udp_socket.settimeout(1.0) # 1 秒超时以允许检查 self.running
            
            while self.running:
                try:
                    data, addr = self.udp_socket.recvfrom(4096) # 增加缓冲区大小
                    
                    # 解码并发出原始数据
                    try:
                        raw_text = data.decode('utf-8').strip()
                        if raw_text:
                            self.raw_data_received.emit("udp", raw_text)
                    except UnicodeDecodeError:
                        logger.debug("UDP raw data decode failed")
                        
                    result = self._parse_data(data)
                    if result:
                        prefix, parsed = result
                        self.data_received.emit("udp", prefix, parsed)
                except socket.timeout:
                    continue
                except OSError as e:
                    if self.running:
                        logger.warning("UDP Error: %s", e)
                        time.sleep(1)
                        
        except OSError as e:
            logger.error("Failed to bind UDP socket: %s", e)
        finally:
            if self.udp_socket:
                self.udp_socket.close()

    def _serial_loop(self, config):
        """接收串口数据的循环"""
        port = config.get('port', 'COM3')
        baudrate = config.get('baudrate', 115200)
        timeout = config.get('timeout', 1)
        
        while self.running:
            try:
                self.serial_port = serial.Serial(port, baudrate, timeout=timeout)
                logger.info("Serial port %s opened successfully.", port)
                
                while self.running and self.serial_port.is_open:
                    try:
                        if self.serial_port.in_waiting > 0:
                            line = self.serial_port.readline()
                            
                            # 解码并发出原始数据
                            try:
                                raw_text = line.decode('utf-8').strip()
                                if raw_text:
                                    self.raw_data_received.emit("serial", raw_text)
                            except UnicodeDecodeError:
                                logger.debug("Serial raw data decode failed")
                                
                            result = self._parse_data(line)
                            if result:
                                prefix, parsed = result
                                self.data_received.emit("serial", prefix, parsed)
                        else:
                            time.sleep(0.01) # 防止 CPU 占用过高
                    except (serial.SerialException, OSError) as e:
                        logger.warning("Serial Read Error: %s", e)
                        break
                        
            except serial.SerialException as e:
                logger.warning("Serial Connection Error (%s): %s", port, e)
                time.sleep(2)
            except OSError as e:
                logger.error("Unexpected Serial Error: %s", e)
                time.sleep(2)
            finally:
                if self.serial_port and self.serial_port.is_open:
                    self.serial_port.close()

    def _serial_binary_loop(self, config):
        """接收 ATK-MS901M 二进制串口数据的循环"""
        port = config.get('port', 'COM3')
        baudrate = config.get('baudrate', 115200)
        timeout = config.get('timeout', 1)
        acc_fsr = config.get('acc_fsr', 4)
        gyro_fsr = config.get('gyro_fsr', 2000)

        parser = MS901MStreamParser(acc_fsr=acc_fsr, gyro_fsr=gyro_fsr)

        while self.running:
            try:
                self.serial_port = serial.Serial(port, baudrate, timeout=timeout)
                logger.info("Serial port %s opened (ATK-MS901M binary mode).", port)

                while self.running and self.serial_port.is_open:
                    try:
                        waiting = self.serial_port.in_waiting
                        if waiting > 0:
                            raw = self.serial_port.read(waiting)
                        else:
                            raw = self.serial_port.read(1)
                            if not raw:
                                continue

                        self.raw_data_received.emit(
                            "serial",
                            raw.hex(' ')
                        )

                        snapshots = parser.feed(raw)
                        for snap in snapshots:
                            self.raw_data_received.emit(
                                "serial",
                                MS901MStreamParser.format_debug(snap)
                            )
                            self.data_received.emit("serial", None, snap)

                    except (serial.SerialException, OSError) as e:
                        logger.warning("Serial Read Error (binary): %s", e)
                        break
                        
            except serial.SerialException as e:
                logger.warning("Serial Connection Error (%s): %s", port, e)
                time.sleep(2)
            except OSError as e:
                logger.error("Unexpected Serial Error: %s", e)
                time.sleep(2)
            finally:
                if self.serial_port and self.serial_port.is_open:
                    self.serial_port.close()
