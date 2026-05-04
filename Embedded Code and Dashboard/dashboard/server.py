"""
UWB Positioning Dashboard — Serial → WebSocket Bridge
Reads the Hub's Serial output and broadcasts parsed JSON over WebSocket.

Requirements:
    pip install pyserial websockets

Usage:
    python server.py --port COM3          (adjust COM port to match your Hub)
    python server.py --port COM3 --baud 115200
"""

import asyncio
import json
import re
import serial
import serial.tools.list_ports
import websockets
import argparse
import time
import threading
from datetime import datetime

# ─── Defaults ──────────────────────────────────────────────────────────────
DEFAULT_BAUD   = 115200
WS_HOST        = "0.0.0.0"
WS_PORT        = 8765

# ─── Room geometry (dynamic, updated from Hub) ─────────────────────────────
room_config = {
    "name": "C201-6B",
    "x_max": 7.85,
    "y_max": 10.5,
    "a1_z": 2.20, "a2_z": 2.35, "hub_z": 2.20,
    "hub_x": 0.00, "hub_y": 9.50,
    "a1_x": 0.00, "a1_y": 0.00,
    "a2_x": 7.85, "a2_y": 0.00
}

# ─── Shared state ───────────────────────────────────────────────────────────
latest_data = {
    "x": None, "y": None, "z": None,
    "d_hub": None, "d_a1": None, "d_a2": None,
    "outside": False,
    "signal": False,          # True when positions are coming in
    "timestamp": None,
    "raw": "",
    "alert": "",
    "room_changed": False
}
connected_clients = set()
data_lock = threading.Lock()

# ─── Regex patterns ─────────────────────────────────────────────────────────
RE_DIST  = re.compile(
    r"D_hub:\s*([\d.]+)\s*m\s*\|\s*D_A1:\s*([\d.]+)\s*m\s*\|\s*D_A2:\s*([\d.]+)\s*m",
    re.IGNORECASE
)
RE_POS   = re.compile(
    r"==>\s*X:\s*([-\d.]+)\s*m\s*Y:\s*([-\d.]+)\s*m\s*Z:\s*([-\d.]+)\s*m",
    re.IGNORECASE
)
RE_ALERT = re.compile(r"\[ALERT\]", re.IGNORECASE)
RE_WAIT  = re.compile(r"Waiting for all distances", re.IGNORECASE)
RE_ROOM  = re.compile(r"\[ROOM_CONFIG\]\s*(\{.*?\})")


def parse_line(line: str):
    """Parse a single Hub serial line and update latest_data."""
    global latest_data
    with data_lock:
        latest_data["raw"] = line.strip()
        latest_data["timestamp"] = datetime.now().isoformat()

        m_room = RE_ROOM.search(line)
        if m_room:
            try:
                new_room = json.loads(m_room.group(1))
                if new_room.get("name") != room_config.get("name"):
                    room_config.update(new_room)
                    latest_data["room_changed"] = True
            except Exception as e:
                print(f"[Serial] Error parsing room config: {e}")

        m_dist = RE_DIST.search(line)
        if m_dist:
            latest_data["d_hub"] = float(m_dist.group(1))
            latest_data["d_a1"]  = float(m_dist.group(2))
            latest_data["d_a2"]  = float(m_dist.group(3))

        m_pos = RE_POS.search(line)
        if m_pos:
            latest_data["x"]      = float(m_pos.group(1))
            latest_data["y"]      = float(m_pos.group(2))
            latest_data["z"]      = float(m_pos.group(3))
            latest_data["signal"] = True
            latest_data["outside"] = bool(RE_ALERT.search(line))
            if latest_data["outside"]:
                latest_data["alert"] = "⚠️  Tag is OUTSIDE the room boundary!"
            else:
                latest_data["alert"] = ""
        elif RE_WAIT.search(line):
            latest_data["signal"] = False
            latest_data["outside"] = False
            latest_data["alert"]  = "Waiting for all anchor distances…"


def serial_reader(port: str, baud: int):
    """Background thread: continuously read lines from Hub serial."""
    global latest_data
    print(f"[Serial] Opening {port} @ {baud} baud …")
    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                print(f"[Serial] Connected to {port}")
                while True:
                    raw = ser.readline()
                    if raw:
                        try:
                            line = raw.decode("utf-8", errors="replace")
                            parse_line(line)
                        except Exception as e:
                            print(f"[Serial] Parse error: {e}")
        except serial.SerialException as e:
            print(f"[Serial] Error: {e} — retrying in 3 s …")
            with data_lock:
                latest_data["signal"] = False
            time.sleep(3)


# ─── WebSocket handler ───────────────────────────────────────────────────────
async def ws_handler(websocket):
    connected_clients.add(websocket)
    print(f"[WS] Client connected  ({len(connected_clients)} total)")
    try:
        # Send room geometry once on connect
        with data_lock:
            room_info = {
                "type": "room",
                "room": {"x_max": room_config["x_max"], "y_max": room_config["y_max"], "name": room_config.get("name", "Unknown")},
                "anchors": [
                    {"id": "Hub",     "x": room_config["hub_x"], "y": room_config["hub_y"], "z": room_config["hub_z"]},
                    {"id": "Anchor1", "x": room_config["a1_x"], "y": room_config["a1_y"], "z": room_config["a1_z"]},
                    {"id": "Anchor2", "x": room_config["a2_x"], "y": room_config["a2_y"], "z": room_config["a2_z"]}
                ]
            }
        await websocket.send(json.dumps(room_info))

        # Keep connection alive until client disconnects
        await websocket.wait_closed()
    except Exception:
        pass
    finally:
        connected_clients.discard(websocket)
        print(f"[WS] Client disconnected ({len(connected_clients)} total)")


async def broadcast_loop():
    """Push latest data to all connected clients at ~10 Hz."""
    global connected_clients
    while True:
        if connected_clients:
            room_changed = False
            with data_lock:
                if latest_data.get("room_changed"):
                    room_changed = True
                    latest_data["room_changed"] = False
                    room_payload = {
                        "type": "room",
                        "room": {"x_max": room_config["x_max"], "y_max": room_config["y_max"], "name": room_config.get("name", "Unknown")},
                        "anchors": [
                            {"id": "Hub",     "x": room_config["hub_x"], "y": room_config["hub_y"], "z": room_config["hub_z"]},
                            {"id": "Anchor1", "x": room_config["a1_x"], "y": room_config["a1_y"], "z": room_config["a1_z"]},
                            {"id": "Anchor2", "x": room_config["a2_x"], "y": room_config["a2_y"], "z": room_config["a2_z"]}
                        ]
                    }
                payload = dict(latest_data)

            if room_changed:
                msg_room = json.dumps(room_payload)
                dead = set()
                for ws in list(connected_clients):
                    try:
                        await ws.send(msg_room)
                    except Exception:
                        dead.add(ws)
                connected_clients -= dead

            payload["type"] = "position"
            msg = json.dumps(payload)
            dead = set()
            for ws in list(connected_clients):
                try:
                    await ws.send(msg)
                except Exception:
                    dead.add(ws)
            connected_clients -= dead
        await asyncio.sleep(0.1)   # 10 Hz


async def main(port: str, baud: int):
    # Serial reader in a daemon thread
    t = threading.Thread(target=serial_reader, args=(port, baud), daemon=True)
    t.start()

    # WebSocket server + broadcast loop together
    print(f"[WS] Server starting on ws://{WS_HOST}:{WS_PORT}")
    async with websockets.serve(ws_handler, WS_HOST, WS_PORT):
        await broadcast_loop()


# ─── Entry point ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UWB Hub Serial → WebSocket bridge")
    parser.add_argument("--port", default=None,
                        help="Serial COM port (e.g. COM3 or /dev/ttyUSB0). "
                             "If omitted, lists available ports.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help=f"Baud rate (default {DEFAULT_BAUD})")
    args = parser.parse_args()

    if args.port is None:
        ports = serial.tools.list_ports.comports()
        if ports:
            print("Available serial ports:")
            for p in ports:
                print(f"  {p.device}  —  {p.description}")
            print("\nRe-run with:  python server.py --port <PORT>")
        else:
            print("No serial ports found. Connect the Hub and re-run.")
    else:
        asyncio.run(main(args.port, args.baud))
