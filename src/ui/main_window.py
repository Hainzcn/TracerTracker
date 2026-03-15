from PySide6.QtWidgets import QMainWindow, QVBoxLayout, QWidget, QLabel, QHBoxLayout, QSizePolicy, QTextEdit, QCheckBox, QSpinBox, QSplitter, QPushButton
from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QTextCursor
from src.ui.viewer_3d import Viewer3D
from src.utils.config_loader import ConfigLoader
from src.utils.data_receiver import DataReceiver
from src.utils.pose_processor import PoseProcessor

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        
        self.setWindowTitle("TracerTracker - 3D Path Visualizer")
        self.resize(1280, 720)
        
        # Initialize Config and Data Receiver
        self.config_loader = ConfigLoader()
        self.data_receiver = DataReceiver(self.config_loader)
        self.data_receiver.data_received.connect(self.on_data_received)
        self.data_receiver.raw_data_received.connect(self.on_raw_data_received)
        self.data_receiver.start()
        
        # Initialize Pose Processor
        self.pose_processor = PoseProcessor(self.config_loader)
        self.pose_processor.position_updated.connect(self.on_pose_updated)
        self.pose_processor.log_message.connect(self.on_pose_log)
        
        # Central widget and layout
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)
        
        # Add 3D Viewer
        self.viewer = Viewer3D()
        self.viewer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.viewer.log_message.connect(self.on_pose_log) # Reuse the same log handler
        self.layout.addWidget(self.viewer, 1) # Add stretch factor 1 to take available space
        render_debug_cfg = self.config_loader.get_render_debug_config()
        self.viewer.set_render_debug_options(
            enabled=render_debug_cfg.get("enabled", False),
            verbose_point_updates=render_debug_cfg.get("verbose_point_updates", False)
        )
        
        # Debug Console Area (Hidden by default)
        self.debug_splitter = QSplitter(Qt.Horizontal)
        self.debug_splitter.setFixedHeight(200)
        self.debug_splitter.hide()
        
        # Common Style for Consoles
        console_style = """
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #333;
                font-family: Consolas, monospace;
                font-size: 11px;
            }
        """
        
        # Left Console: Raw Data
        self.raw_data_console = QTextEdit()
        self.raw_data_console.setReadOnly(True)
        self.raw_data_console.setPlaceholderText("Raw Data Log...")
        self.raw_data_console.setStyleSheet(console_style)
        
        # Right Console: Debug Info
        self.debug_info_console = QTextEdit()
        self.debug_info_console.setReadOnly(True)
        self.debug_info_console.setPlaceholderText("Debug Info & Pose Processing Log...")
        self.debug_info_console.setStyleSheet(console_style)
        
        self.debug_splitter.addWidget(self.raw_data_console)
        self.debug_splitter.addWidget(self.debug_info_console)
        # Set initial sizes (50/50)
        self.debug_splitter.setSizes([640, 640])
        
        self.layout.addWidget(self.debug_splitter)
        
        # Status Bar Area (Custom Overlay or Bottom Bar)
        self.status_bar_widget = QWidget()
        self.status_bar_layout = QHBoxLayout(self.status_bar_widget)
        self.status_bar_layout.setContentsMargins(10, 5, 10, 5)
        self.status_bar_widget.setStyleSheet("background-color: #252526; border-top: 1px solid #333;")
        self.status_bar_widget.setFixedHeight(30) # Fixed height for status bar
        
        # Hold to View Parsed Button (Moved to Status Bar)
        self.parsed_view_btn = QPushButton("按住查看解析数据")
        self.parsed_view_btn.setFixedWidth(120)
        self.parsed_view_btn.setStyleSheet("""
            QPushButton {
                background-color: #333;
                color: #ccc;
                border: 1px solid #555;
                padding: 2px 4px;
                font-size: 11px;
                border-radius: 2px;
            }
            QPushButton:pressed {
                background-color: #555;
            }
        """)
        self.parsed_view_btn.pressed.connect(self.enable_parse_view)
        self.parsed_view_btn.released.connect(self.disable_parse_view)
        self.show_parsed_data = False
        
        self.status_bar_layout.addWidget(self.parsed_view_btn)
        self.status_bar_layout.addSpacing(10)
        
        # Status Labels
        self.udp_status_label = QLabel("UDP: Idle")
        self.udp_status_label.setStyleSheet("color: #888;")
        self.serial_status_label = QLabel("Serial: Idle")
        self.serial_status_label.setStyleSheet("color: #888;")
        
        self.status_bar_layout.addWidget(self.udp_status_label)
        self.status_bar_layout.addSpacing(20)
        self.status_bar_layout.addWidget(self.serial_status_label)
        self.status_bar_layout.addStretch()
        
        # Debug Toggle Checkbox
        self.debug_checkbox = QCheckBox("Debug Log")
        self.debug_checkbox.setStyleSheet("""
            QCheckBox {
                color: #888;
            }
            QCheckBox::indicator {
                width: 13px;
                height: 13px;
                border: 1px solid #555;
                background: #1e1e1e;
            }
            QCheckBox::indicator:checked {
                background: #4CAF50;
                border: 1px solid #4CAF50;
            }
        """)
        self.debug_checkbox.stateChanged.connect(self.toggle_debug_console)
        self.status_bar_layout.addWidget(self.debug_checkbox)

        self.full_path_checkbox = QCheckBox("全路径")
        self.full_path_checkbox.setStyleSheet(self.debug_checkbox.styleSheet())
        self.full_path_checkbox.toggled.connect(self.toggle_full_path_mode)
        self.status_bar_layout.addSpacing(12)
        self.status_bar_layout.addWidget(self.full_path_checkbox)

        self.trail_checkbox = QCheckBox("速度尾迹")
        self.trail_checkbox.setStyleSheet(self.debug_checkbox.styleSheet())
        self.trail_checkbox.toggled.connect(self.toggle_trail_mode)
        self.status_bar_layout.addSpacing(8)
        self.status_bar_layout.addWidget(self.trail_checkbox)

        self.trail_length_label = QLabel("长度")
        self.trail_length_label.setStyleSheet("color: #888;")
        self.status_bar_layout.addSpacing(4)
        self.status_bar_layout.addWidget(self.trail_length_label)

        self.trail_length_spinbox = QSpinBox()
        self.trail_length_spinbox.setRange(10, 5000)
        self.trail_length_spinbox.setValue(120)
        self.trail_length_spinbox.setFixedWidth(80)
        self.trail_length_spinbox.setStyleSheet("""
            QSpinBox {
                color: #d4d4d4;
                background-color: #1e1e1e;
                border: 1px solid #555;
                padding: 1px 4px;
            }
        """)
        self.trail_length_spinbox.valueChanged.connect(self.on_trail_length_changed)
        self.status_bar_layout.addWidget(self.trail_length_spinbox)
        self.trail_length_spinbox.setEnabled(False)
        self.trail_length_label.setEnabled(False)
        
        self.layout.addWidget(self.status_bar_widget)
        
        # Set a modern dark theme for the window
        self.setStyleSheet("""
            QMainWindow {
                background-color: #1e1e1e;
            }
        """)
        
        # Timer to reset status if no data received
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.check_status_timeout)
        self.status_timer.start(1000)
        
        self.last_udp_time = 0
        self.last_serial_time = 0
        self.viewer.set_trail_length(self.trail_length_spinbox.value())
        
    def toggle_debug_console(self, state):
        """Toggle the visibility of the debug console."""
        if state:
            self.debug_splitter.show()
        else:
            self.debug_splitter.hide()
            self.raw_data_console.clear()
            self.debug_info_console.clear()

    def toggle_full_path_mode(self, checked):
        self.viewer.set_full_path_mode(checked)

    def toggle_trail_mode(self, checked):
        self.viewer.set_trail_mode(checked)
        self.trail_length_spinbox.setEnabled(checked)
        self.trail_length_label.setEnabled(checked)

    def on_trail_length_changed(self, value):
        self.viewer.set_trail_length(value)

    def enable_parse_view(self):
        self.show_parsed_data = True
        
    def disable_parse_view(self):
        self.show_parsed_data = False

    def format_parsed_data(self, prefix, data):
        """
        Format parsed data into a readable string with labels from config.
        """
        points_config = self.config_loader.get("points", [])
        formatted_parts = []
        
        # Helper to format a single point config
        def format_point(cfg, values):
            try:
                name = cfg.get("name", "Unknown")
                x_idx = cfg.get("x", {}).get("index", -1)
                y_idx = cfg.get("y", {}).get("index", -1)
                z_idx = cfg.get("z", {}).get("index", -1)
                
                parts = []
                # Use raw index access, multipliers are handled in viewer/pose_processor, 
                # here we just show what indices mean.
                if 0 <= x_idx < len(values): parts.append(f"X:{values[x_idx]:.2f}")
                if 0 <= y_idx < len(values): parts.append(f"Y:{values[y_idx]:.2f}")
                if 0 <= z_idx < len(values): parts.append(f"Z:{values[z_idx]:.2f}")
                
                if parts:
                    return f"{name}({', '.join(parts)})"
            except:
                pass
            return None

        # Iterate config to find matching points
        for p in points_config:
            # Check prefix match
            p_prefix = p.get("prefix")
            if p_prefix == "": p_prefix = None
            
            # If both are None or match
            if p_prefix == prefix:
                res = format_point(p, data)
                if res:
                    formatted_parts.append(res)
        
        # If no config matched or partial match, also show raw list
        if formatted_parts:
            return " | ".join(formatted_parts)
        else:
            # Fallback to simple list display
            return f"List: {data}"

    def on_pose_updated(self, name, x, y, z):
        """Handle position updates from PoseProcessor."""
        # Cyan color for the computed path
        self.viewer.update_point(name, x, y, z, color=[0, 255, 255, 255], size=15)
        
    def on_pose_log(self, message):
        """Handle log messages from PoseProcessor."""
        if self.debug_splitter.isVisible():
            self.debug_info_console.moveCursor(QTextCursor.End)
            self.debug_info_console.insertPlainText(message + "\n")
            self.debug_info_console.moveCursor(QTextCursor.End)

    def on_raw_data_received(self, source, raw_text):
        """Handle raw data log."""
        if self.debug_splitter.isVisible() and not self.show_parsed_data:
            import time
            timestamp = time.strftime("%H:%M:%S", time.localtime(time.time()))
            log_msg = f"[{timestamp}] [{source.upper()}] {raw_text}"
            
            self.raw_data_console.moveCursor(QTextCursor.End)
            self.raw_data_console.insertPlainText(log_msg + "\n")
            self.raw_data_console.moveCursor(QTextCursor.End)

    def on_data_received(self, source, prefix, data):
        """Handle received data from UDP or Serial."""
        import time
        current_time = time.time()
        
        # If in parsed view mode, log parsed data
        if self.debug_splitter.isVisible() and self.show_parsed_data:
            timestamp = time.strftime("%H:%M:%S", time.localtime(current_time))
            parsed_str = self.format_parsed_data(prefix, data)
            log_msg = f"[{timestamp}] [{source.upper()}] [PARSED] {parsed_str}"
            
            self.raw_data_console.moveCursor(QTextCursor.End)
            self.raw_data_console.insertPlainText(log_msg + "\n")
            self.raw_data_console.moveCursor(QTextCursor.End)

        # Process data for pose estimation
        self.pose_processor.process(source, prefix, data)
        
        # Update status indicators
        status_text = f"Receiving ({len(data)} values)"
        if prefix:
            status_text += f" [{prefix}]"
            
        if source == "udp":
            self.last_udp_time = current_time
            self.udp_status_label.setText(f"UDP: {status_text}")
            self.udp_status_label.setStyleSheet("color: #4CAF50;") # Green
        elif source == "serial":
            self.last_serial_time = current_time
            self.serial_status_label.setText(f"Serial: {status_text}")
            self.serial_status_label.setStyleSheet("color: #4CAF50;") # Green
        
        points_config = self.config_loader.get("points", [])
        
        for point_cfg in points_config:
            # Skip special purpose points (they are handled by PoseProcessor)
            if point_cfg.get("purpose") in ["accelerometer", "gyroscope", "magnetic_field"]:
                continue

            # Check if this config applies to the current source
            cfg_source = point_cfg.get("source", "any")
            
            # Allow "any", "udp", "serial" or specific list match
            if cfg_source != "any" and cfg_source != source:
                continue
                
            # Check prefix match
            # If config has a prefix, it must match the received prefix
            # If config has no prefix, it matches only if received prefix is None
            cfg_prefix = point_cfg.get("prefix", None)
            if cfg_prefix == "": cfg_prefix = None
            
            if cfg_prefix != prefix:
                continue
                
            try:
                # Get indices and multipliers
                x_cfg = point_cfg.get("x", {})
                y_cfg = point_cfg.get("y", {})
                z_cfg = point_cfg.get("z", {})
                
                x_idx = x_cfg.get("index", 0)
                y_idx = y_cfg.get("index", 1)
                z_idx = z_cfg.get("index", 2)
                
                x_mult = x_cfg.get("multiplier", 1.0)
                y_mult = y_cfg.get("multiplier", 1.0)
                z_mult = z_cfg.get("multiplier", 1.0)
                
                # Check if data has enough elements
                required_len = max(x_idx, y_idx, z_idx) + 1
                if len(data) >= required_len:
                    x = float(data[x_idx]) * x_mult
                    y = float(data[y_idx]) * y_mult
                    z = float(data[z_idx]) * z_mult
                    
                    name = point_cfg.get("name", "Unknown")
                    # Use color from config or default red
                    color = point_cfg.get("color", [255, 0, 0, 255])
                    size = point_cfg.get("size", 10)
                    
                    self.viewer.update_point(name, x, y, z, color, size)
            except Exception as e:
                print(f"Error processing point {point_cfg.get('name')}: {e}")
        
    def check_status_timeout(self):
        """Reset status labels if no data received for a while."""
        import time
        current_time = time.time()
        timeout = 2.0 # Seconds
        
        if current_time - self.last_udp_time > timeout:
            self.udp_status_label.setText("UDP: Idle")
            self.udp_status_label.setStyleSheet("color: #888;") # Grey
            
        if current_time - self.last_serial_time > timeout:
            self.serial_status_label.setText("Serial: Idle")
            self.serial_status_label.setStyleSheet("color: #888;") # Grey

    def closeEvent(self, event):
        """Clean up resources on close."""
        self.data_receiver.stop()
        super().closeEvent(event)
