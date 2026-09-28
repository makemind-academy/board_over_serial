# board-over-serial

Two real boards hand over their own screens through two links; the player draws them and a real LED answers.

Article: [boards-hand-over-their-screens](https://makemind.dev/en/build/boards-hand-over-their-screens)

## What is here

- `serial_bridge/` — C program standing in for the hardware, built by `verify.sh`.
- `tcp_bridge/` — C program standing in for the hardware, built by `verify.sh`.
- `captures/` — screenshots taken from AppPlayer by `verify.py`.
- `verify.py`, `verify.sh` — the check.

## Open it in AppPlayer

Build the bridges (`verify.sh` does), attach the boards, then add a server app per bridge with the bridge binary as the command. Without a board the bridges stop and say where they looked.

## Verify

```bash
bash verify.sh
```

Needs AppPlayer with the debug MCP on (see `tools/README.md`). The script builds what needs building, drives the player through the screens above, asserts the claim at the top of this file, and writes `captures/`.
