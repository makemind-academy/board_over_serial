#!/usr/bin/env python3
"""board-over-serial: two real boards hand over their own screens through two links; the player draws them and a real LED answers.

There is no simulator behind this sample. With no board attached it stops and says where it looked.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
from appplayer import AppPlayer  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CAP = os.path.join(HERE, "captures")
serial = os.environ.get("BOARD_SERIAL", "")
tcp = os.environ.get("BOARD_TCP", "")
if not serial and not tcp:
    raise SystemExit("no board found — set BOARD_SERIAL=/dev/cu.usbmodemXXXX or BOARD_TCP=host:port")

ap = AppPlayer()
boards = []
if serial:
    ap.register_server("com.makemind.sample.board.serial", "Board (USB)", cwd=os.path.join(HERE, "serial_bridge"),
                       command=os.path.join(HERE, "serial_bridge", "serial_bridge"), args=[serial, "115200"])
    boards.append(("com.makemind.sample.board.serial", "01_serial_board.png",
                   os.environ.get("BOARD_SERIAL_NAME", "")))
if tcp:
    host, port = tcp.split(":")
    ap.register_server("com.makemind.sample.board.tcp", "Board (Wi-Fi)", cwd=os.path.join(HERE, "tcp_bridge"),
                       command=os.path.join(HERE, "tcp_bridge", "tcp_bridge"), args=[host, port])
    boards.append(("com.makemind.sample.board.tcp", "02_tcp_board.png",
                   os.environ.get("BOARD_TCP_NAME", "")))
for sid, shot, name in boards:
    ap.restart()
    ap.open_server(sid)
    # The board's own name in the title bar is the proof its screen arrived;
    # what the screen says below that is the board's business, not this
    # sample's. A board that offers an LED gets its button pressed.
    ap.wait_text(name or "LED")
    ap.tap("LED on") if ap.has_text("LED on") else None
    time.sleep(1)
    ap.shot(f"{CAP}/{shot}")
print(f"board-over-serial: {len(boards)} board(s) drew their own screen in the player")
