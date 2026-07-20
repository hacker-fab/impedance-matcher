# SPICE Verification

ngspice checks on the board's analog support circuits: the SWR sense chain and
the input protection. MCU, steppers, OLED, fan, and opto are not simulated.

SWR path: `FWD_IN -> U1A TLV2372 buffer (12 V rail) -> R10 2.7k / R13 1k divider
(x0.27027) -> ADC`. REV path is identical (U1B, R11/R12). Goal was to confirm the
op-amp output never exceeds the Teensy's 3.3 V ADC range.

## Results

### SWR -> ADC ceiling
Worst case is the buffer saturating near the 12 V rail. Ceiling lands at ~3.24 V
across every model, ~55 mV under the 3.3 V full scale. No clipping.

| Model                     | Ceiling  |
|---------------------------|----------|
| Ideal buffer              | 3.230 V  |
| Real TLV2372 macromodel   | 3.243 V  |
| + PCB trace parasitics    | 3.241 V  |

### Resistor tolerance (Monte Carlo, 200 runs, 1% parts)
- Realistic (1% = 3 sigma): 3.201 - 3.259 V. All safe.
- Pessimistic (1% = 1 sigma): 3.145 - 3.317 V. ~17 mV over 3.3 V only when rail
  saturation and worst-case tolerances stack together.

### Protection
- Gate clamp (CR1 BZX84C5V6): off below 5 V; breaks down at 5.5 V and holds Vgs
  near -5.6 V up to 16 V input. Without it the gate would see -16 V.
- Reverse polarity (DMP6180 P-FET): reverse input blocked (rail ~nV, ~pA). Forward
  conducts with ~5 mV drop, ~120 mA at 12 V.

### PCB parasitics
SWR trace resistance is 0.05 - 0.10 ohm, via inductance ~0.7 nH (irrelevant at DC).
Shifts the ceiling by ~2.4 mV.

## Testbenches

Run from this folder with `ngspice -b <file>.cir`. Batch mode writes results to a
file (`wrdata`/`echo`), not stdout, so check the `.txt`/`.out` each one names.

| File                      | Tests                                          |
|---------------------------|------------------------------------------------|
| `swr_opamp_chain.cir`     | SWR buffer + divider sweep (ideal op-amp)      |
| `opamp_fidelity.cir`      | Same chain, real TLV2372 macromodel            |
| `parasitic_ceiling.cir`   | Chain with extracted PCB trace parasitics      |
| `mc_swr_divider.cir`      | Monte Carlo, 1% = 3 sigma                      |
| `mc_swr_divider_3sig.cir` | Monte Carlo, 1% = 1 sigma                      |
| `prot_gateclamp.cir`      | Zener gate clamp sweep                         |
| `prot_revpol2.cir`        | Reverse-polarity P-FET blocking + forward drop |
| `tlv2372.lib`             | TLV2372 op-amp macromodel (included above)     |

Outputs (`.txt`, `.out`, `.log`) are gitignored; re-run to regenerate.
