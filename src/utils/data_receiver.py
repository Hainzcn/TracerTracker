import socket
import threading
import time
import serial
from PySide6.QtCore import QObject, Signal

class DataReceiver(QObject):
    """
    Handles data reception from UDP and Serial sources.
    Emits 'data_received' signal with (source, prefix, parsed_data).
    """
    data_received = Signal(str, str, list)
    # Emits raw data: (source, raw_string)
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
        """Start receiver threads based on configuration."""
        self.running = True
        
        udp_config = self.config_loader.get_udp_config()
        if udp_config.get('enabled', False):
            self.udp_thread = threading.Thread(target=self._udp_loop, args=(udp_config,), daemon=True)
            self.udp_thread.start()
            print(f"UDP Receiver started on {udp_config['ip']}:{udp_config['port']}")
            
        serial_config = self.config_loader.get_serial_config()
        if serial_config.get('enabled', False):
            self.serial_thread = threading.Thread(target=self._serial_loop, args=(serial_config,), daemon=True)
            self.serial_thread.start()
            print(f"Serial Receiver started on {serial_config['port']} @ {serial_config['baudrate']}")

    def stop(self):
        """Stop all receiver threads."""
        self.running = False
        
        if self.udp_socket:
            self.udp_socket.close()
            
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            
        if self.udp_thread:
            self.udp_thread.join(timeout=1.0)
            
        if self.serial_thread:
            self.serial_thread.join(timeout=1.0)
            
        print("Data receivers stopped.")

    def _parse_data(self, raw_data):
        """
        Parse comma-separated values from string.
        Supports optional prefix ending with colon (e.g., "G:1,2,3").
        Returns tuple (prefix, values_list) or None if parsing fails.
        """
        try:
            # Decode if bytes
            if isinstance(raw_data, bytes):
                text = raw_data.decode('utf-8').strip()
            else:
                text = raw_data.strip()
                
            if not text:
                return None
            
            prefix = None
            csv_part = text
            
            # Check for prefix (e.g. "G:...")
            if ':' in text:
                parts = text.split(':', 1)
                prefix = parts[0].strip()
                csv_part = parts[1].strip()
                
            # Parse CSV
            if not csv_part:
                return None
                
            values = [float(x.strip()) for x in csv_part.split(',')]
            return (prefix, values)
        except Exception as e:
            # print(f"Error parsing data '{raw_data}': {e}")
            return None

    def _udp_loop(self, config):
        """Loop for receiving UDP data."""
        ip = config.get('ip', '127.0.0.1')
        port = config.get('port', 5005)
        
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.bind((ip, port))
            self.udp_socket.settimeout(1.0) # 1 second timeout to allow checking self.running
            
            while self.running:
                try:
                    data, addr = self.udp_socket.recvfrom(4096) # Increased buffer size
                    
                    # Decode and emit raw data
                    try:
                        raw_text = data.decode('utf-8').strip()
                        if raw_text:
                            self.raw_data_received.emit("udp", raw_text)
                    except Exception:
                        pass # Ignore decode errors for raw log or handle differently
                        
                    result = self._parse_data(data)
                    if result:
                        prefix, parsed = result
                        self.data_received.emit("udp", prefix, parsed)
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"UDP Error: {e}")
                        time.sleep(1) # Wait before retrying
                        
        except Exception as e:
            print(f"Failed to bind UDP socket: {e}")
        finally:
            if self.udp_socket:
                self.udp_socket.close()

    def _serial_loop(self, config):
        """Loop for receiving Serial data."""
        port = config.get('port', 'COM3')
        baudrate = config.get('baudrate', 115200)
        timeout = config.get('timeout', 1)
        
        while self.running:
            try:
                self.serial_port = serial.Serial(port, baudrate, timeout=timeout)
                print(f"Serial port {port} opened successfully.")
                
                while self.running and self.serial_port.is_open:
                    try:
                        if self.serial_port.in_waiting > 0:
                            line = self.serial_port.readline()
                            
                            # Decode and emit raw data
                            try:
                                raw_text = line.decode('utf-8').strip()
                                if raw_text:
                                    self.raw_data_received.emit("serial", raw_text)
                            except Exception:
                                pass
                                
                            result = self._parse_data(line)
                            if result:
                                prefix, parsed = result
                                self.data_received.emit("serial", prefix, parsed)
                        else:
                            time.sleep(0.01) # Prevent high CPU usage
                    except Exception as e:
                        print(f"Serial Read Error: {e}")
                        break # Break inner loop to reconnect
                        
            except serial.SerialException as e:
                # print(f"Serial Connection Error ({port}): {e}")
                time.sleep(2) # Wait before retrying
            except Exception as e:
                print(f"Unexpected Serial Error: {e}")
                time.sleep(2)
            finally:
                if self.serial_port and self.serial_port.is_open:
                    self.serial_port.close()
