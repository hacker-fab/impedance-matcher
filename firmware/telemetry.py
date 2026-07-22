"""
telemetry.py
Shared telemetry contract for live.py and plot.py

Two schemas live here:
  1. Serial line emitted by firmware: "VSWR_CSV," + 8 fields
  2. CSV file written by live.py and read by plot.py: 8 columns
"""

import os

# Serial link
BAUD = 500000

# Serial-line schema (firmware -> host). Line is SERIAL_TAG + 8 comma parts:
#   tag, millis, vswr, fwd_v, rev_v, motor1_pos, motor2_pos, at_match
SERIAL_TAG = "VSWR_CSV,"
SERIAL_FIELD_COUNT = 8

# CSV-file schema (live.py -> file -> plot.py)
CSV_HEADER = [
    "host_time_s",
    "device_millis",
    "vswr",
    "forward_v",
    "reverse_v",
    "motor1_pos_rad",
    "motor2_pos_rad",
    "at_match",
]
CSV_COLUMN_COUNT = 8

# CSV column indices used by plot.py
COL_EPOCH = 0
COL_DEVICE_MILLIS = 1
COL_VSWR = 2
COL_FORWARD_V = 3
COL_REVERSE_V = 4
COL_MOTOR1 = 5
COL_MOTOR2 = 6
COL_MATCH = 7

# Data locations
DATA_CSV_DIR = os.path.join("data", "csv")
DEFAULT_CSV_PATH = os.path.join(DATA_CSV_DIR, "latest.csv")

# VSWR thresholds
VSWR_MATCH = 1.2    # firmware declares match below this
VSWR_UNMATCH = 1.4  # firmware drops match above this
VSWR_MAX = 4.0      # plot.py outlier ceiling


def parse_serial_line(raw):
    """Parse one firmware serial line into a dict, or None if not a valid telemetry line.

    Returns keys: device_millis, vswr, forward_v, reverse_v, motor1_pos, motor2_pos, at_match.
    """
    if not raw.startswith(SERIAL_TAG):
        return None

    parts = raw.split(",")
    if len(parts) != SERIAL_FIELD_COUNT:
        return None

    _, millis_s, vswr_s, fwd_v_s, rev_v_s, motor1_s, motor2_s, at_match_s = parts
    try:
        return {
            "device_millis": int(millis_s),
            "vswr": float(vswr_s),
            "forward_v": float(fwd_v_s),
            "reverse_v": float(rev_v_s),
            "motor1_pos": float(motor1_s),
            "motor2_pos": float(motor2_s),
            "at_match": int(at_match_s) != 0,
        }
    except ValueError:
        return None
