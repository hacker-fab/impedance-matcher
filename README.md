# Automatic Impedance Matcher

Teensy 4.1 firmware and Python telemetry tools for an automatic RF impedance matcher.

## Components

- `firmware/impedance_matcher/impedance_matcher.ino`: UI-enabled firmware entrypoint
- `firmware/headless_matcher/headless_matcher.ino`: headless firmware entrypoint (no OLED/encoder UI loop)
- `firmware/live.py`: live serial monitor + CSV logger + real-time plots
- `firmware/plot.py`: offline plotting for saved CSV runs (Matplotlib or Plotly)
- `hardware/`: CAD and PCB manufacturing files

## Firmware (UI Mode: `firmware/impedance_matcher/impedance_matcher.ino`)

- Runs on Teensy 4.1 and starts motors, encoder, UART, and OLED display.
- Uses a gradient-descent style matcher to move motors toward lower VSWR.
- Main loop does three things: update matching logic, read encoder input, then update/draw UI.
- Required Arduino libraries: Adafruit SSD1306, Adafruit GFX, TMCStepper.

## Firmware (Headless Mode: `firmware/headless_matcher/headless_matcher.ino`)

- Runs on Teensy 4.1 without the OLED/encoder-driven menu interface.
- Keeps the automatic matching + telemetry pipeline for bench/remote operation.
- Useful when only serial telemetry/control is needed.
- Shares the same serial data workflow used by `firmware/live.py` and `firmware/plot.py`.

## Live Telemetry (`firmware/live.py`)

- Connects to the board over serial (`500000` baud by default).
- Reads `VSWR_CSV,...` telemetry lines from firmware.
- Shows live plots (recent VSWR, full VSWR history, and motor positions).
- Saves all valid samples to CSV (`data/csv/latest.csv` by default).

Example:

```bash
python firmware/live.py --port /dev/cu.usbmodemXXXX --baud 500000 --window-seconds 20 --csv firmware/data/csv/latest.csv
```

## Offline Analysis (`firmware/plot.py`)

- Opens a saved telemetry CSV and cleans bad rows/outliers.
- Default output is a static Matplotlib plot.
- Optional interactive Plotly view (`--interactive`) or HTML export (`--html`).
- Shows VSWR, motor traces, and match state.

Examples:

```bash
python firmware/plot.py firmware/data/csv/latest.csv
python firmware/plot.py firmware/data/csv/latest.csv --max-plot-points 4000 --minutes
python firmware/plot.py firmware/data/csv/latest.csv --interactive
python firmware/plot.py firmware/data/csv/latest.csv --html
```

## Data Format

CSV header written by `firmware/live.py`:

`host_time_s,device_millis,vswr,forward_v,reverse_v,motor1_pos_rad,motor2_pos_rad,at_match`

## Hardware Folder

- `hardware/CAD/`: mechanical design assets for the matcher assembly.
- `hardware/PCB/schematic.pdf`: PCB schematic export.
- `hardware/PCB/Gerbers/Gerbers.zip`: fabrication Gerber package.
- `hardware/PCB/Gerbers/JLC_bom.csv`: JLCPCB bill of materials export.
- `hardware/PCB/Gerbers/JLC_cpl.csv`: JLCPCB component placement file.

## Authors

- Miguel Salvacion
- William Gao
