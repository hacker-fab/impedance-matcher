#!/usr/bin/env python3
"""Check that firmware pin constants agree with the KiCad schematic netlist.

Requires KiCad CLI.  This deliberately derives the observed PCB pins from the
schematic instead of repeating them as hard-coded expectations in C++ tests.
"""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCHEMATIC = ROOT / "hardware/PCB/impedance_matcher.kicad_sch"
CONFIG = ROOT / "firmware/impedance_matcher/include/config.h"
MATCHING = ROOT / "firmware/impedance_matcher/src/matching.cpp"


def kicad_cli():
    candidates = [os.environ.get("KICAD_CLI"), shutil.which("kicad-cli"),
                  "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"]
    return next((Path(path) for path in candidates if path and Path(path).exists()), None)


def balanced_forms(text, head):
    """Yield complete parenthesized forms whose text starts with *head*."""
    start = 0
    while True:
        start = text.find(head, start)
        if start < 0:
            return
        depth = 0
        for end in range(start, len(text)):
            if text[end] == "(":
                depth += 1
            elif text[end] == ")":
                depth -= 1
                if depth == 0:
                    yield text[start:end + 1]
                    start = end + 1
                    break
        else:
            raise AssertionError(f"unterminated KiCad form beginning {head!r}")


def net_nodes(netlist_text):
    nets = {}
    # ``(nets`` is the container around all net forms; match the newline after
    # the singular ``(net`` so it is not mistaken for one giant net.
    for form in balanced_forms(netlist_text, "(net\n"):
        name = re.search(r'\(name "([^"]+)"\)', form)
        if not name:
            continue
        nodes = set(re.findall(r'\(node\s+\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)', form))
        nets[name.group(1)] = nodes
    return nets


def config_defines():
    values = {}
    for line in CONFIG.read_text().splitlines():
        found = re.match(r"\s*#define\s+(\w+)\s+([^/\s]+)", line)
        if found:
            values[found.group(1)] = found.group(2)
    return values


def require(nodes, net, *expected):
    assert net in nodes, f"missing KiCad net {net}"
    for item in expected:
        assert item in nodes[net], f"{net} missing {item}; found {sorted(nodes[net])}"


def main():
    cli = kicad_cli()
    if not cli:
        raise SystemExit("FAIL: KiCad CLI not found; set KICAD_CLI or install KiCad.")
    with tempfile.TemporaryDirectory(prefix="matcher-netlist-") as temp:
        netlist = Path(temp) / "matcher.net"
        subprocess.run([str(cli), "sch", "export", "netlist", "-o", str(netlist), str(SCHEMATIC)], check=True)
        nodes = net_nodes(netlist.read_text())

    # These connections are observed from KiCad, not assumed from firmware.
    require(nodes, "/FWD_OUT", ("A1", "D24"))
    require(nodes, "/REV_OUT", ("A1", "D25"))
    require(nodes, "/STEP1", ("A1", "D2"), ("DRIVER1", "10"))
    require(nodes, "/DIR1", ("A1", "D3"), ("DRIVER1", "9"))
    require(nodes, "/STEP2", ("A1", "D9"), ("DRIVER2", "10"))
    require(nodes, "/DIR2", ("A1", "D10"), ("DRIVER2", "9"))
    require(nodes, "/FAN", ("A1", "D23"))
    require(nodes, "/TRANSMIT", ("A1", "D31"))
    require(nodes, "/RX1", ("A1", "D7"), ("DRIVER1", "13"))
    require(nodes, "/TX1", ("A1", "D8"))
    require(nodes, "/RX2", ("A1", "D28"), ("DRIVER2", "13"))
    require(nodes, "/TX2", ("A1", "D29"))
    require(nodes, "/CLK", ("A1", "D38"), ("ROTARY1", "5"))
    require(nodes, "/DT", ("A1", "D39"), ("ROTARY1", "4"))
    require(nodes, "/SW", ("A1", "D40"), ("ROTARY1", "3"))
    require(nodes, "/SDA", ("A1", "D18"), ("OLED1", "3"), ("I2C1", "2"))
    require(nodes, "/SCL", ("A1", "D19"), ("OLED1", "4"), ("I2C1", "1"))

    defines = config_defines()
    expected = {
        "FWD_PIN": "24", "REV_PIN": "25", "STEP_PIN_1": "2", "DIR_PIN_1": "3",
        "STEP_PIN_2": "9", "DIR_PIN_2": "10", "FAN_PIN": "23", "TRANSMIT_PIN": "31",
        "DRIVER_RX_PIN_1": "7", "DRIVER_TX_PIN_1": "8",
        "DRIVER_RX_PIN_2": "28", "DRIVER_TX_PIN_2": "29",
        "ENC_A": "38", "ENC_B": "39", "ENC_BTN": "40",
        "MATCHING_SERIAL_PORT_1": "Serial2", "MATCHING_SERIAL_PORT_2": "Serial7",
    }
    for key, value in expected.items():
        assert defines.get(key) == value, f"{key} is {defines.get(key)!r}, expected {value!r}"
    assert "Wire.begin()" in MATCHING.with_name("ui.cpp").read_text(), "OLED/I2C firmware does not initialize Wire"
    print("PASS: KiCad nets agree with ADC, motor, fan, transmit, UART, rotary, and I2C firmware contract")


if __name__ == "__main__":
    main()
