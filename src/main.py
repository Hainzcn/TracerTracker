import sys
import os
import logging

# 设置项目根目录到 sys.path，确保模块导入正常
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if project_root not in sys.path:
    sys.path.insert(0, project_root)

from PySide6.QtWidgets import QApplication
from src.ui.main_window import MainWindow

logger = logging.getLogger(__name__)

def main():
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    os.environ["QT_AUTO_SCREEN_SCALE_FACTOR"] = "1"
    
    app = QApplication(sys.argv)
    app.setApplicationName("TracerTracker")
    
    try:
        window = MainWindow()
        window.show()
        sys.exit(app.exec())
    except KeyboardInterrupt:
        logger.info("Interrupted by user. Exiting...")
        sys.exit(0)
    except Exception:
        logger.critical("Unhandled exception", exc_info=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
