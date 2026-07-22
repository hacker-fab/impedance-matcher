#!/usr/bin/env python3
"""
live.py
Live VSWR plotter (serial)

Dependencies (pip):
  matplotlib, pyserial
"""

import argparse
from collections import deque
import csv
import os
import time

import matplotlib.pyplot as plt
import serial
from serial.tools import list_ports

import telemetry

DEFAULT_BAUD = telemetry.BAUD
DEFAULT_WINDOW_SECONDS = 20.0
DEFAULT_CSV_PATH = telemetry.DEFAULT_CSV_PATH

# Prevent macOS matplotlib windows from stealing focus on each redraw
plt.rcParams["figure.raise_window"] = False

def get_default_port():
    ports = list(list_ports.comports())
    # macOS / Linux USB CDC
    for p in ports:
        if "cu.usbmodem" in p.device:
            return p.device
    for p in ports:
        if "tty.usbmodem" in p.device:
            return p.device
    # Windows: match Teensy/USB serial by description, else first COM port
    for p in ports:
        text = " ".join(filter(None, [p.description, p.manufacturer])).lower()
        if "teensy" in text or "usb serial" in text:
            return p.device
    for p in ports:
        if p.device.upper().startswith("COM"):
            return p.device
    return "/dev/cu.usbmodem12345"


def parse_args():
    parser = argparse.ArgumentParser(description="Live plot VSWR telemetry from serial.")
    parser.add_argument("--port", default=get_default_port(), help="Serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate")
    parser.add_argument("--window-seconds", type=float, default=DEFAULT_WINDOW_SECONDS, help="Time window for recent plots")
    parser.add_argument(
        "--csv",
        default=DEFAULT_CSV_PATH,
        help=f"CSV log output path (default: {DEFAULT_CSV_PATH})",
    )
    return parser.parse_args()


def connect_serial(port, baud):
    while True:
        try:
            ser = serial.Serial(port, baud, timeout=1)
            print(f"Listening on {port} @ {baud} baud...")
            return ser
        except serial.SerialException as exc:
            print(f"Serial open failed: {exc}")
            print("Close Arduino/Teensy Serial Monitor (or other app using the port), then retrying in 1s...")
            time.sleep(1)


def main():
    args = parse_args()
    ser = connect_serial(args.port, args.baud)

    times = deque()
    vswr_vals = deque()
    all_times = []
    all_vswr_vals = []
    motor1_vals = deque()
    motor2_vals = deque()

    plt.ion()
    fig, (ax_vswr, ax_vswr_full, ax_motor) = plt.subplots(3, 1, sharex=False)
    line_vswr, = ax_vswr.plot([], [], label="VSWR")
    line_vswr_full, = ax_vswr_full.plot([], [], label="VSWR (Whole Run)")
    line_motor1, = ax_motor.plot([], [], label="Motor 1 Pos (rad)")
    line_motor2, = ax_motor.plot([], [], label="Motor 2 Pos (rad)")

    ax_vswr.set_ylabel("VSWR")
    ax_vswr_full.set_ylabel("VSWR")
    ax_motor.set_ylabel("Motor Pos (rad)")
    ax_motor.set_xlabel("Time (s)")
    ax_vswr.grid(True, alpha=0.3)
    ax_vswr_full.grid(True, alpha=0.3)
    ax_motor.grid(True, alpha=0.3)
    ax_vswr.legend(loc="upper right")
    ax_vswr_full.legend(loc="upper right")
    ax_motor.legend(loc="upper right")
    status_text = fig.text(0.02, 0.98, "Match: unknown", ha="left", va="top")
    plt.show(block=False)

    PLOT_INTERVAL = 0.1  # Redraw at 10 Hz regardless of data rate

    t0_host = None
    last_plot_time = 0.0
    at_match = False
    csv_dir = os.path.dirname(os.path.abspath(args.csv))
    if csv_dir:
        os.makedirs(csv_dir, exist_ok=True)
    csv_exists = os.path.exists(args.csv)
    with open(args.csv, "a", newline="") as csv_file:
        writer = csv.writer(csv_file)
        if not csv_exists or os.path.getsize(args.csv) == 0:
            writer.writerow(telemetry.CSV_HEADER)

        try:
            while True:
                # Drain all waiting serial lines before redrawing
                try:
                    raw = ser.readline().decode("utf-8", errors="ignore").strip()
                except serial.SerialException as exc:
                    print(f"Serial read failed: {exc}")
                    try:
                        ser.close()
                    except Exception:
                        pass
                    print("Attempting to reconnect...")
                    ser = connect_serial(args.port, args.baud)
                    continue

                sample = telemetry.parse_serial_line(raw)
                if sample is None:
                    continue

                t_ms = sample["device_millis"]
                vswr = sample["vswr"]
                forward_v = sample["forward_v"]
                reverse_v = sample["reverse_v"]
                motor1_pos = sample["motor1_pos"]
                motor2_pos = sample["motor2_pos"]
                at_match = sample["at_match"]

                # Plots use host wall clock
                # CSV column 0 is absolute host epoch for alignment with plot.py
                host_now = time.time()
                if t0_host is None:
                    t0_host = host_now

                t_s = host_now - t0_host
                times.append(t_s)
                vswr_vals.append(vswr)
                all_times.append(t_s)
                all_vswr_vals.append(vswr)
                motor1_vals.append(motor1_pos)
                motor2_vals.append(motor2_pos)

                while times and (t_s - times[0]) > args.window_seconds:
                    times.popleft()
                    vswr_vals.popleft()
                    motor1_vals.popleft()
                    motor2_vals.popleft()

                writer.writerow(
                    [host_now, t_ms, vswr, forward_v, reverse_v, motor1_pos, motor2_pos, int(at_match)]
                )
                csv_file.flush()

                # Redraw only at PLOT_INTERVAL
                if host_now - last_plot_time >= PLOT_INTERVAL:
                    last_plot_time = host_now

                    line_vswr.set_data(times, vswr_vals)
                    line_vswr_full.set_data(all_times, all_vswr_vals)
                    line_motor1.set_data(times, motor1_vals)
                    line_motor2.set_data(times, motor2_vals)
                    status_text.set_text(f"Match: {'YES' if at_match else 'NO'}")

                    ax_vswr.relim()
                    ax_vswr.autoscale_view()
                    ax_vswr_full.relim()
                    ax_vswr_full.autoscale_view()
                    ax_motor.relim()
                    ax_motor.autoscale_view()
                    fig.canvas.draw_idle()
                    fig.canvas.flush_events()
        except KeyboardInterrupt:
            print("\nStopping.")
        finally:
            ser.close()


if __name__ == "__main__":
    main()
