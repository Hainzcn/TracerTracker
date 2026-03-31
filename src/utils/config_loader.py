import copy
import json
import logging
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

class ConfigLoader:
    _instance = None
    _config = {}
    
    DEFAULT_INS_CONFIG = {
        "kalman": {
            "enabled": True,
            "process_noise_sigma": 0.5,
            "measurement_noise_R": 0.5,
        },
        "zupt": {
            "enabled": True,
            "acc_variance_threshold": 0.5,
            "gyro_variance_threshold": 0.1,
            "window_size": 40,
        },
        "baro_lpf_alpha": 0.1,
        "madgwick": {
            "beta": 0.05,
        },
        "mahony": {
            "kp": 1.0,
            "ki": 0.0,
        },
        "filter_yaw_offset_deg": 90.0,
    }

    DEFAULT_CONFIG = {
        "gravity_reference": 9.80,
        "udp": {
            "enabled": True,
            "ip": "127.0.0.1",
            "port": 8888
        },
        "serial": {
            "enabled": True,
            "port": "COM5",
            "baudrate": 115200,
            "timeout": 1,
            "protocol": "atkms901m",
            "acc_fsr": 4,
            "gyro_fsr": 2000,
        },
        "render_debug": {
            "enabled": True,
            "verbose_point_updates": False
        },
        "ins": DEFAULT_INS_CONFIG,
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
                "x": {"index": 10, "multiplier": 1.0},
                "y": {"index": 11, "multiplier": 1.0},
                "z": {"index": 12, "multiplier": 1.0}
            },
            {
                "name": "QUAT",
                "source": "serial",
                "purpose": "quaternion",
                "w": {"index": 6, "multiplier": 1.0},
                "x": {"index": 7, "multiplier": 1.0},
                "y": {"index": 8, "multiplier": 1.0},
                "z": {"index": 9, "multiplier": 1.0}
            },
            {
                "name": "BARO",
                "source": "serial",
                "purpose": "barometer",
                "altitude": {"index": 18, "multiplier": 1.0},
                "pressure": {"index": 17, "multiplier": 1.0}
            },
            {
                "name": "Point G (Prefixed)",
                "source": "udp",
                "prefix": "G",
                "color": [0, 0, 255, 255],
                "size": 15,
                "x": {"index": 0, "multiplier": 1.0},
                "y": {"index": 1, "multiplier": -1.0},
                "z": {"index": 2, "multiplier": -1.0}
            },
            {
                "name": "Point H (Prefixed)",
                "source": "udp",
                "prefix": "H",
                "color": [255, 0, 0, 255],
                "size": 15,
                "x": {"index": 3, "multiplier": 100.0},
                "y": {"index": 1, "multiplier": -100.0},
                "z": {"index": 2, "multiplier": -100.0}
            }
        ]
    }

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(ConfigLoader, cls).__new__(cls)
            cls._instance.load_config()
        return cls._instance

    @classmethod
    def _clone_default_config(cls):
        return copy.deepcopy(cls.DEFAULT_CONFIG)

    @staticmethod
    def _config_path():
        if getattr(sys, "frozen", False):
            base_dir = Path(sys.executable).resolve().parent
        else:
            base_dir = Path(__file__).resolve().parents[2]
        return base_dir / "config.json"

    @classmethod
    def _merge_value(cls, current_value, default_value):
        if isinstance(default_value, dict):
            current_dict = current_value if isinstance(current_value, dict) else {}
            merged = {}
            for key, value in default_value.items():
                merged[key] = cls._merge_value(current_dict.get(key), value)
            for key, value in current_dict.items():
                if key not in merged:
                    merged[key] = copy.deepcopy(value)
            return merged

        if isinstance(default_value, list):
            if current_value is None:
                return copy.deepcopy(default_value)
            return copy.deepcopy(current_value)

        return copy.deepcopy(default_value if current_value is None else current_value)

    @classmethod
    def _merge_with_defaults(cls, config):
        return cls._merge_value(config, cls.DEFAULT_CONFIG)

    def load_config(self):
        """从 config.json 加载配置，或者使用默认配置"""
        config_path = self._config_path()
        
        if config_path.exists():
            try:
                with open(config_path, 'r', encoding='utf-8') as f:
                    loaded_config = json.load(f)
                self._config = self._merge_with_defaults(loaded_config)
                logger.info("Configuration loaded from %s", config_path)
                if self._config != loaded_config:
                    logger.info("Configuration was missing default fields. Saving merged config.")
                    self.save_config()
            except Exception as e:
                logger.error("Error loading config: %s. Using defaults.", e)
                self._config = self._clone_default_config()
        else:
            logger.info("Config file not found. Creating default config.")
            self._config = self._clone_default_config()
            self.save_config()

    def save_config(self):
        """将当前配置保存到 config.json"""
        config_path = self._config_path()
        try:
            with open(config_path, 'w', encoding='utf-8') as f:
                json.dump(self._config, f, indent=4)
                logger.info("Configuration saved to %s", config_path)
        except Exception as e:
            logger.error("Error saving config: %s", e)

    def get(self, key, default=None):
        """获取配置值"""
        return self._config.get(key, default)
    
    def get_udp_config(self):
        return self._merge_value(
            self._config.get('udp'),
            self.DEFAULT_CONFIG['udp'],
        )
        
    def get_serial_config(self):
        return self._merge_value(
            self._config.get('serial'),
            self.DEFAULT_CONFIG['serial'],
        )

    def get_render_debug_config(self):
        return self._merge_value(
            self._config.get('render_debug'),
            self.DEFAULT_CONFIG['render_debug'],
        )

    def get_ins_config(self):
        """获取惯性导航系统配置，缺失字段回退到默认值。"""
        return self._merge_value(self._config.get('ins'), self.DEFAULT_INS_CONFIG)
