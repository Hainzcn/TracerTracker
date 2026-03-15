import json
import os

class ConfigLoader:
    _instance = None
    _config = {}
    
    # Default configuration
    DEFAULT_CONFIG = {
        "gravity_reference": 10.00,
        "udp": {
            "enabled": True,
            "ip": "127.0.0.1",
            "port": 5005
        },
        "serial": {
            "enabled": True,
            "port": "COM3",
            "baudrate": 115200,
            "timeout": 1
        },
        "render_debug": {
            "enabled": False,
            "verbose_point_updates": False
        },
        "points": [
            {
                "name": "ACC",
                "source": "serial",
                "purpose": "accelerometer",
                "x": { "index": 0, "multiplier": 1.0 },
                "y": { "index": 1, "multiplier": 1.0 },
                "z": { "index": 2, "multiplier": 1.0 }
            },
            {
                "name": "GYR",
                "source": "serial",
                "purpose": "gyroscope",
                "x": { "index": 3, "multiplier": 1.0 },
                "y": { "index": 4, "multiplier": 1.0 },
                "z": { "index": 5, "multiplier": 1.0 }
            },
            {
                "name": "MAG",
                "source": "serial",
                "purpose": "magnetic_field",
                "x": { "index": 6, "multiplier": 1.0 },
                "y": { "index": 7, "multiplier": 1.0 },
                "z": { "index": 8, "multiplier": 1.0 }
            }
        ]
    }

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(ConfigLoader, cls).__new__(cls)
            cls._instance.load_config()
        return cls._instance

    def load_config(self):
        """Load configuration from config.json or use defaults."""
        config_path = os.path.join(os.getcwd(), 'config.json')
        
        if os.path.exists(config_path):
            try:
                with open(config_path, 'r', encoding='utf-8') as f:
                    self._config = json.load(f)
                    print(f"Configuration loaded from {config_path}")
                    # Ensure gravity_reference is present if not in file
                    if "gravity_reference" not in self._config:
                        self._config["gravity_reference"] = self.DEFAULT_CONFIG["gravity_reference"]
            except Exception as e:
                print(f"Error loading config: {e}. Using defaults.")
                self._config = self.DEFAULT_CONFIG.copy()
        else:
            print("Config file not found. Creating default config.")
            self._config = self.DEFAULT_CONFIG.copy()
            self.save_config()

    def save_config(self):
        """Save current configuration to config.json."""
        config_path = os.path.join(os.getcwd(), 'config.json')
        try:
            with open(config_path, 'w', encoding='utf-8') as f:
                json.dump(self._config, f, indent=4)
                print(f"Configuration saved to {config_path}")
        except Exception as e:
            print(f"Error saving config: {e}")

    def get(self, key, default=None):
        """Get a configuration value."""
        return self._config.get(key, default)
    
    def get_udp_config(self):
        return self._config.get('udp', self.DEFAULT_CONFIG['udp'])
        
    def get_serial_config(self):
        return self._config.get('serial', self.DEFAULT_CONFIG['serial'])

    def get_render_debug_config(self):
        return self._config.get('render_debug', self.DEFAULT_CONFIG['render_debug'])
