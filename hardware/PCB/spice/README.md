# SPICE Verification

ngspice checks on the board's analog support circuits: the SWR sense chain, the
input protection, and the 5 V -> 18 V -> 12 V power rail (DC). MCU, steppers,
OLED, fan, and opto are not simulated.

SWR path: `FWD_IN -> U1A buffer -> R10 2.7k / R13 1k divider -> ADC`, gain x0.27027
(TLV2372 on 12 V rail). REV path is identical (U1B, R11/R12).

## Results

### SWR -> ADC ceiling
Worst case is the buffer saturating near the 12 V rail. Ceiling lands at ~3.24 V
across every model, ~55 mV under the 3.3 V full scale. No clipping.

| Model                     | Ceiling  |
|---------------------------|----------|
| Ideal buffer              | 3.230 V  |
| Real TLV2372 macromodel   | 3.243 V  |
| + PCB trace parasitics    | 3.241 V  |

### Resistor tolerance (1% parts)
Worst-case divider ratio is +/-1.0% (R13 contributes (1-k)*1% = 0.73%, R10
contributes k*1% = 0.27%) -> ceiling ~3.27 V, still under 3.3 V.

### Protection
- Gate clamp (CR1 BZX84C5V6): off below 5 V; breaks down at 5.5 V and holds Vgs
  near -5.6 V up to 16 V input. Without it the gate would see -16 V.
- Reverse polarity (DMP6180 P-FET): reverse input blocked (rail ~nV, ~pA). Forward
  conducts with ~5 mV drop, ~120 mA at 12 V.

### PCB parasitics
SWR trace resistance is 0.05 - 0.10 ohm, via inductance ~0.7 nH (irrelevant at DC).
Shifts the ceiling by ~2.4 mV.

### SWR input protection
Each input has a 1k series resistor (R14/R15) plus clamp diodes to VCC/GND
(D2 BAV99S). This caps the TLV2372 input current on a fault, since
`I_clamp ~= (Vfault - Vrail - Vf) / 1k`. Abs-max is +/-10 mA.

| Fault on `FWD_IN` | unprotected | with R + clamp | vs +/-10 mA |
|-------------------|-------------|----------------|-------------|
| 18 V              | 1.02 A      | 5.2 mA         | safe        |
| 30 V              | 3.41 A      | 17.1 mA        | still over  |
| -5 V              | 0.82 A      | 4.2 mA         | safe        |

Normal operation is unaffected (8 V in -> 8.00 V pin) since the input draws no
DC. 30 V would need 2.2k (~7.9 mA).

### Power rail, DC
Rail is `5 V (USB) -> MT3608 boost -> ~17.5 V -> L7812 -> 12 V`. DC operating point
only; MT3608 switching ripple is out of scope.

- **LDO** (`ldo_reg.cir`): 18 -> 12 V holds 12.00 V across 0 - 1.2 A. `Pd =
  (Vin-Vout)*Iload` = 7.2 W at 1.2 A -- needs a heatsink (bare TO-220 ~2 W).
- **Chain** (`rail_chain_dc.cir`): `5 -> 17.51 -> 12.00 V` (R8 62k / R7 2.2k set the
  boost setpoint), lands 12.00 V on the TLV2372 VCC pin; SWR ADC reads 2.16 V for
  8 V `FWD_IN`.

Caveat: `l7812.lib` is behavioral (fixed 2 V dropout, no current limit); above
1.5 A is un-modeled.

## Testbenches

Run from this folder with `ngspice -b <file>.cir`. Batch mode writes sweep data to
the `.txt`/`.out` each file names (`wrdata`); DC-point benches `print` to stdout.

Install ngspice, or use the `ngspice` in KiCad's `bin/`. Results here are from
ngspice-46 (KiCad 10.0).

| File                      | Tests                                          |
|---------------------------|------------------------------------------------|
| `swr_opamp_chain.cir`     | SWR buffer + divider sweep (ideal op-amp)      |
| `opamp_fidelity.cir`      | Same chain, real TLV2372 macromodel            |
| `parasitic_ceiling.cir`   | Chain with extracted PCB trace parasitics      |
| `prot_gateclamp.cir`      | Zener gate clamp sweep                         |
| `prot_revpol2.cir`        | Reverse-polarity P-FET blocking + forward drop |
| `opamp_input_ov.cir`      | SWR input overvoltage, pin clamp current       |
| `opamp_input_protect.cir` | Series R + clamp fix, fault current <10mA      |
| `ldo_reg.cir`             | L7812 18->12V regulation, dropout, dissipation |
| `rail_chain_dc.cir`       | End-to-end 5->18->12V DC, TLV2372 VCC          |
| `l7812.lib`               | L7812 behavioral reg model (included above)    |
| `tlv2372.lib`             | TLV2372 op-amp macromodel (included above)     |

Outputs (`.txt`, `.out`, `.log`) are gitignored; re-run to regenerate.
