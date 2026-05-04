@echo off
echo ====================================
echo  UWB Dashboard Quick-Start
echo ====================================
echo.

REM 1. Install Python dependencies
echo [1] Installing Python dependencies...
pip install -r requirements.txt

echo.
echo [2] Listing available serial ports...
python -c "import serial.tools.list_ports; [print(' ',p.device,'-',p.description) for p in serial.tools.list_ports.comports()] or print('  No ports found')"

echo.
set /p PORT="Enter your Hub COM port (e.g. COM3): "

echo.
echo [3] Starting WebSocket bridge on ws://localhost:8765 ...
echo     Open index.html in your browser and click Connect.
echo     Press Ctrl+C to stop.
echo.
python server.py --port %PORT%
pause
