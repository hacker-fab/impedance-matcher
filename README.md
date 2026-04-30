# Automatic Impedance Matcher

Teensy 4.1 firmware and Python telemetry tools for an automatic RF impedance matcher.

## Components

- `impedance_matcher/impedance_matcher.ino`: main firmware entrypoint
- `live.py`: live serial monitor + CSV logger + real-time plots
- `plot.py`: offline plotting for saved CSV runs (Matplotlib or Plotly)

## Firmware (`impedance_matcher/impedance_matcher.ino`)

- Runs on Teensy 4.1 and starts motors, encoder, UART, and OLED display.
- Uses a gradient-descent style matcher to move motors toward lower VSWR.
- Main loop does three things: update matching logic, read encoder input, then update/draw UI.
- Required Arduino libraries: Adafruit SSD1306, Adafruit GFX, TMCStepper.

## Live Telemetry (`live.py`)

- Connects to the board over serial (`500000` baud by default).
- Reads `VSWR_CSV,...` telemetry lines from firmware.
- Shows live plots (recent VSWR, full VSWR history, and motor positions).
- Saves all valid samples to CSV (`data/csv/latest.csv` by default).

Example:

```bash
python live.py --port /dev/cu.usbmodemXXXX --baud 500000 --window-seconds 20 --csv data/csv/latest.csv
```

## Offline Analysis (`plot.py`)

- Opens a saved telemetry CSV and cleans bad rows/outliers.
- Default output is a static Matplotlib plot.
- Optional interactive Plotly view (`--interactive`) or HTML export (`--html`).
- Shows VSWR, motor traces, and match state.

Examples:

```bash
python plot.py latest.csv
python plot.py data/csv/latest.csv --max-plot-points 4000 --minutes
python plot.py data/csv/latest.csv --interactive
python plot.py data/csv/latest.csv --html
```

## Data Format

CSV header written by `live.py`:

`host_time_s,device_millis,vswr,forward_v,reverse_v,motor1_pos_rad,motor2_pos_rad,at_match`

## Authors

- Miguel Salvacion
- William Gao
