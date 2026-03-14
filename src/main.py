import sys
import os

# Add the project root directory to sys.path to ensure imports work correctly
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if project_root not in sys.path:
    sys.path.insert(0, project_root)

from PySide6.QtWidgets import QApplication
from src.ui.main_window import MainWindow

def main():
    # Set up environment for high-DPI scaling
    os.environ["QT_AUTO_SCREEN_SCALE_FACTOR"] = "1"
    
    app = QApplication(sys.argv)
    app.setApplicationName("TracerTracker")
    
    # Create and show the main window
    window = MainWindow()
    window.show()
    
    try:
        sys.exit(app.exec())
    except KeyboardInterrupt:
        print("Interrupted by user. Exiting...")
        sys.exit(0)

if __name__ == "__main__":
    main()
