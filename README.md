# Automated Impedance Matcher

Teensy 4.1 control loop firmware, custom PCB/CAD hardware, and Python telemetry logging/plotting tools.

## Layout

```
├── firmware/
│   ├── impedance_matcher/                 # Tuning loop, local UI, & motor controls
│   ├── headless_matcher/                  # Serial-only sketch
│   ├── live.py                            # Serial reader, live plots, CSV logger
│   └── plot.py                            # Offline plots & Mermaid export
└── hardware/
    ├── CAD/                               # Mechanical design & enclosure models
    └── PCB/                               # KiCad project & manufacturing files
        ├── impedance_matcher.kicad_pro    # KiCad project main file
        ├── spice/                         # SPICE verification of analog support circuits
        └── production/                    # JLCPCB manufacturing & assembly
            ├── imp_match.zip              # Gerbers
            ├── imp_match_bom.csv          # SMT BOM
            └── imp_match_positions.csv    # SMT CPL
```

**Arduino libraries:** Adafruit SSD1306, Adafruit GFX, and TMCStepper (GUI build); TMCStepper only for headless.

## Firmware

- **GUI:** Gradient-style matching toward lower VSWR; loop runs matching, encoder poll, then OLED refresh.
- **Headless:** Same matching; control via serial (`stream on`, `help`, etc.).

Serial is **500000** baud unless you change it in code and in `live.py`.

## Live telemetry

- Default CSV path is `data/csv/latest.csv`.
- Enable **`VSWR_CSV`** lines from the device: **USB log** in settings, or `stream on` in headless.

```bash
# From repo root; adjust --port to your Teensy
python firmware/live.py --port /dev/cu.usbmodemXXXX --window-seconds 20
```

## Offline plots

- Loads the 8-column telemetry CSV, drops bad/outlier rows, then plots VSWR, motors, and match state.
- **Matplotlib** is the default windowed plot.
- **Plotly:** `--interactive` (browser) or `--html`.
- **Mermaid:** `--mermaid` writes `data/mermaid/<csv_stem>_{vswr,motor1,motor2,match}.mmd`.
```bash
python firmware/plot.py path/to/run.csv
python firmware/plot.py path/to/run.csv --minutes --sample-interval 0.25
python firmware/plot.py path/to/run.csv --interactive
python firmware/plot.py path/to/run.csv --html
python firmware/plot.py path/to/run.csv --mermaid
```

## Telemetry CSV format

Written by `live.py` (and compatible with `plot.py`):

`host_time_s,device_millis,vswr,forward_v,reverse_v,motor1_pos_rad,motor2_pos_rad,at_match`

## Hardware notes

The analog support circuits (SWR sensing, input protection) were checked in ngspice.
See `hardware/PCB/spice/` for results and testbenches.

## Authors

- Miguel Salvacion
- William Gao
