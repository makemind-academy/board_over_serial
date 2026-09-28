#!/bin/bash
# Probe real boards over two different links and render the screen each serves.
#
# This sample has no simulator behind it. If no board answers it does not
# quietly pass — it says where it looked and stops. A hardware article that can
# be "verified" without hardware is not verified.
set -euo pipefail
cd "$(dirname "$0")"

echo "   [1/4] bridges (C)"
( cd serial_bridge && cc -O2 -o serial_bridge serial_bridge.c )
( cd tcp_bridge && cc -O2 -o tcp_bridge tcp_bridge.c )

echo "   [2/4] serial board?"
SERIAL="${BOARD_SERIAL:-}"
SERIAL_NAME=""
if [ -z "$SERIAL" ]; then
  for cand in /dev/cu.usbmodem* /dev/cu.usbserial*; do
    [ -e "$cand" ] || continue
    LINE=$({ printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"1"}}}\n'; sleep 2; } \
      | ./serial_bridge/serial_bridge "$cand" 115200 2>/dev/null | head -1 || true)
    if echo "$LINE" | grep -q '"serverInfo"'; then
      SERIAL="$cand"
      SERIAL_NAME=$(echo "$LINE" | sed 's/.*"name":"\([^"]*\)".*/\1/')
      echo "   $cand -> $SERIAL_NAME"
      break
    fi
  done
fi
[ -n "$SERIAL" ] || echo "   none found on /dev/cu.usbmodem* or /dev/cu.usbserial*"

echo "   [3/4] network board? (mDNS _mcp._tcp)"
TCP="${BOARD_TCP:-}"
RESOLVED=""
if [ -z "$TCP" ] && command -v dns-sd >/dev/null 2>&1; then
  # Discovery is the point here: nothing is hardcoded, the board says where it
  # is and which protocol it speaks.
  # dns-sd prints a varying number of header lines before the answers, so
  # answers are found by their "Add" column. The browse can also return an
  # instance the OS still remembers from a board that has since been
  # renamed or reflashed, so every name is resolved and the first that
  # actually answers wins.
  NAMES=$(timeout 6 dns-sd -B _mcp._tcp 2>/dev/null | awk '$2=="Add" {for(i=7;i<=NF;i++) printf "%s ", $i; print ""}' | sed 's/ *$//' | sort -u || true)
  while IFS= read -r NAME; do
    [ -n "$NAME" ] || continue
    DETAIL=$(timeout 6 dns-sd -L "$NAME" _mcp._tcp 2>/dev/null | grep "can be reached at" | head -1 || true)
    HOSTPORT=$(echo "$DETAIL" | sed -n 's/.*can be reached at \([^ ]*\).*/\1/p' | sed 's/\.$//;s/\.:/:/')
    if [ -n "$HOSTPORT" ] && nc -z -w2 "${HOSTPORT%:*}" "${HOSTPORT##*:}" 2>/dev/null; then
      TCP="$HOSTPORT"; RESOLVED="$NAME"
      echo "   discovered \"$RESOLVED\" at $TCP"
      break
    fi
  done <<< "$NAMES"
fi
[ -n "$TCP" ] || echo "   none advertised (set BOARD_TCP=host:port to skip discovery)"

if [ -z "$SERIAL" ] && [ -z "$TCP" ]; then
  echo "   no board on either link. Not skipping — this sample is about real"
  echo "   hardware and cannot be verified without it."
  exit 1
fi

echo "   [4/4] open each board's screen in AppPlayer"
# A bridge is an MCP server on stdio: AppPlayer runs it like any other. The
# screen it draws is the one the board serves — nothing on this side knows
# what the board looks like.
rm -f captures/*.png
BOARD_SERIAL="$SERIAL" BOARD_SERIAL_NAME="${SERIAL_NAME:-}" BOARD_TCP="${TCP:-}" BOARD_TCP_NAME="${RESOLVED:-}" python3 verify.py
