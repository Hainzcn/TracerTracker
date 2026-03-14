# TracerTracker

A Python-based 3D path visualization tool.

## Features
- 3D Coordinate System Rendering
- Interactive Camera Controls:
  - **Left Click + Drag**: Rotate (Orbit)
  - **Right Click + Drag**: Pan (Follow Mouse)
  - **Middle Click**: Smooth Reset to Default View
  - **Scroll Wheel**: Zoom
- High-end Dark Theme
- **Data Reception**:
  - Supports UDP (Localhost) and Serial Port simultaneously
  - Configurable via `config.json`
  - Parses comma-separated values (CSV)
  - **Flexible Data Parsing**:
    - Configure multiple tracked points from different sources (UDP/Serial)
    - Custom index mapping for X, Y, Z coordinates
    - Apply multipliers and sign inversion per coordinate
    - Customizable color and size for each point
- **Real-time Status Monitoring**:
  - Visual indicators for UDP and Serial connection status
  - Displays received data count and activity

## Installation

1. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

## Usage

Run the application:
```bash
python src/main.py
```
