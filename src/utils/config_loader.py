import json
import logging
import os

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
            "acc_variance_threshold": 0.1,
            "gyro_variance_threshold": 0.01,
            "window_size": 20,
        },
        "baro_lpf_alpha": 0.05,
        "madgwick": {
            "beta": 0.1,
        },
        "mahony": {
            "kp": 1.0,
            "ki": 0.0,
        },
        "filter_yaw_offset_deg": 90.0,
    }

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
            "timeout": 1,
            "protocol": "csv"
        },
        "render_debug": {
            "enabled": False,
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
        """从 config.json 加载配置，或者使用默认配置"""
        config_path = os.path.join(os.getcwd(), 'config.json')
        
        if os.path.exists(config_path):
            try:
                with open(config_path, 'r', encoding='utf-8') as f:
                    self._config = json.load(f)
                    logger.info("Configuration loaded from %s", config_path)
                    # 如果文件中没有 gravity_reference，确保其存在
                    if "gravity_reference" not in self._config:
                        self._config["gravity_reference"] = self.DEFAULT_CONFIG["gravity_reference"]
            except Exception as e:
                logger.error("Error loading config: %s. Using defaults.", e)
                self._config = self.DEFAULT_CONFIG.copy()
        else:
            logger.info("Config file not found. Creating default config.")
            self._config = self.DEFAULT_CONFIG.copy()
            self.save_config()

    def save_config(self):
        """将当前配置保存到 config.json"""
        config_path = os.path.join(os.getcwd(), 'config.json')
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
        return self._config.get('udp', self.DEFAULT_CONFIG['udp'])
        
    def get_serial_config(self):
        return self._config.get('serial', self.DEFAULT_CONFIG['serial'])

    def get_render_debug_config(self):
        return self._config.get('render_debug', self.DEFAULT_CONFIG['render_debug'])

    def get_ins_config(self):
        """获取惯性导航系统配置，缺失字段回退到默认值。"""
        cfg = self._config.get('ins', {})
        default = self.DEFAULT_INS_CONFIG

        kalman_default = default["kalman"]
        kalman = cfg.get("kalman", {})
        merged_kalman = {k: kalman.get(k, v) for k, v in kalman_default.items()}

        zupt_default = default["zupt"]
        zupt = cfg.get("zupt", {})
        merged_zupt = {k: zupt.get(k, v) for k, v in zupt_default.items()}

        madgwick_default = default["madgwick"]
        madgwick = cfg.get("madgwick", {})
        merged_madgwick = {k: madgwick.get(k, v) for k, v in madgwick_default.items()}

        mahony_default = default["mahony"]
        mahony = cfg.get("mahony", {})
        merged_mahony = {k: mahony.get(k, v) for k, v in mahony_default.items()}

        return {
            "kalman": merged_kalman,
            "zupt": merged_zupt,
            "baro_lpf_alpha": cfg.get("baro_lpf_alpha", default["baro_lpf_alpha"]),
            "madgwick": merged_madgwick,
            "mahony": merged_mahony,
            "filter_yaw_offset_deg": cfg.get("filter_yaw_offset_deg", default["filter_yaw_offset_deg"]),
        }
