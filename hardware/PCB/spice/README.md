# Analog support-circuit calculations and SPICE benches

These benches model selected DC behavior of the sensor buffers, input protection,
reverse-polarity circuit, and analog supply. They do not validate motor supplies,
switching ripple, transient protection, temperature, or RF behavior.

## Current divider and regulator load

The 10 October 2026 blocker fixes change both buffered sensor dividers to
**3.3 kohm / 1 kohm**: R10/R13 for forward and R11/R12 for reverse.

- Nominal gain: `1000 / 4300 = 0.232558`.
- Independent 1% resistor maximum: `1010 / (3267 + 1010) = 0.236147` (+1.543%).
- Minimum: `990 / (3333 + 990) = 0.229008` (-1.527%).
- Conservative buffer-at-rail ceiling at the L7812C's 12.6 V upper specification:
  `12.6 * 0.236147 = 2.97545 V`, about 325 mV below nominal 3.3 V.
- An 8 V sensor input produces about 1.8605 V at the ADC nominally.

R16 is a **2.2 kohm, 1%, 125 mW** preload from 12 V to ground. At 11.4 V with
+1% resistance it draws 5.13 mA, meeting the regulator's specified 5 mA minimum
load without relying on op-amp current. Worst calculated dissipation at 12.6 V
and -1% resistance is 72.9 mW; apply the part's temperature derating when
qualifying the enclosure. The rail-chain bench's RRAIL now represents R16.

These calculations replace the previous 2.7 kohm-divider ceiling claim, which
omitted regulator tolerance and understated combined resistor tolerance. The
new ceiling applies to the specified regulated rail. Sensor faults, startup,
power-off injection and ADC supply variation still require verification.

Sources: [ST L78](https://www.st.com/resource/en/datasheet/l78.pdf),
[TI TLV2372](https://www.ti.com/lit/ds/symlink/tlv2372.pdf),
[PJRC Teensy 4.1](https://www.pjrc.com/store/teensy41.html).

## Model limitations

The user confirmed raw SW-112 detector taps. R4/R6 are now **100 Mohm, 5%,
0805 Yageo RC0805JR-07100ML / C137480**; D2 is **Nexperia BAV199S-QZ**, a pin-compatible
low-leakage replacement for BAV99S. For the guide's approximate 1 Mohm source,
the input bias load retains `100M/(1M+100M) = 99.0099%` of the open-circuit
signal, versus 9.0909% with the old 100 kohm bias. The 5% resistor range gives
98.9583–99.0566% retention for that fixed source model. This is a loading
estimate, not a calibrated detector transfer function.

D2 is specified at at most 5 nA per diode at 75 V/25 °C and 80 nA at
75 V/150 °C. Two adverse leakage contributions could therefore cause about
9.9 mV at 25 °C or 158 mV at 150 °C through a 1 Mohm source in parallel with
100 Mohm. These are conservative estimates using the specified 75 V test;
they do not predict actual leakage at this board's 12 V or operating temperature.
Op-amp bias, flux residue, humidity, connector leakage and a disconnected sensor
also affect the reading. Clean the input area and measure offsets, settling and
both-channel calibration across the intended temperature and power range.

Sources: [Hacker Fab guide](https://docs.hackerfab.org/home/fab-toolkit/deposition/diy-rf-sputtering-chamber-cmu/add-ons-and-wip/automated-impedance-matching),
[Yageo resistor](https://www.lcsc.com/product-detail/C137480.html),
[Nexperia diode](https://assets.nexperia.com/documents/data-sheet/BAV199S-Q.pdf).

`tlv2372.lib` is a local simplified behavioral model, not a TI vendor macromodel.
It omits device supply current, realistic input protection, output-current
limits, and other important behavior. `l7812.lib` omits output tolerance,
minimum-load behavior, thermal/current limits, and realistic rail injection.
`rail_chain_dc.cir` uses a stiff behavioral boost source at 17.509 V, rather than
simulating the MT3608. The generic PMOS in `prot_revpol2.cir` does not predict
DMP6180 voltage drop or temperature under motor load.

R14/R15 and D2 clamp the buffer inputs relative to 12 V/GND, not the ADC relative
to 3.3 V. A positive fault can raise the 12 V rail; ideal supply sources in the
benches hide this because they can sink current. A 30 V fault gives approximately
17 mA through 1 kohm, so input-clamp current sharing, resistor dissipation and
power-off behavior need analysis against the actual fault envelope.

## Benches and execution

Run `ngspice -b <file>.cir` in this directory when an ngspice executable is
available. Printed or generated outputs are ignored by git. Historical results
used ngspice-46; the updated benches have not been rerun for this change.

- `swr_opamp_chain.cir`: approximate raw detector loading, idealized buffer and
  current ADC divider/filter sweep; leakage is omitted.
- `opamp_fidelity.cir`: simplified TLV2372 model and current divider.
- `parasitic_ceiling.cir`: divider plus previously extracted trace parasitics.
- `rail_chain_dc.cir`: idealized supply chain, R16 and sensor buffer.
- `prot_gateclamp.cir`, `prot_revpol2.cir`: supply protection topology.
- `opamp_input_ov.cir`, `opamp_input_protect.cir`: limited input-fault studies.
- `ldo_reg.cir`: idealized regulator DC behavior only.

Use the explicit worst-case calculations above and powered measurements for
signoff. Neither old nor new DC outputs establish switching, thermal or RF
performance.
