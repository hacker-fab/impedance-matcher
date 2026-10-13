# Plug-in module assemblies

The PCB and local module footprints show female sockets soldered to the carrier
PCB, with the existing male headers on the removable modules seated in them:

- Teensy A1: two 1x24 sockets, 2.54 mm pitch, 15.24 mm row spacing.
- TMC2209 DRIVER1 and DRIVER2: two 1x08 sockets per driver, 2.54 mm pitch,
  12.70 mm row spacing.

The socket models have 8.5 mm-tall housings and 3.1 mm solder tails. These are
generic visualization dimensions; use the actual purchased socket dimensions
for enclosure clearances. The Teensy remains on the bottom of the carrier PCB
and the drivers remain on top.

The Teensy model uses a Z offset of 8.5 mm. The TMC2209 model uses 11.5 mm because
its male-header housing extends 3 mm below its module PCB datum. The driver's
small XY correction (-0.087765, -0.164636 mm) aligns the imported model's pin rows
with the footprint pads. These settings affect only the 3D models.

Both `PinSocket_*_P2.54mm_Vertical.step` files are unmodified KiCad library models
copied from `Connector_PinSocket_2.54mm.3dshapes`. Their CC-BY-SA 4.0 license,
electronic-design exception, and copyright attribution are included in each
STEP file's header. See the
[KiCad 3D library license](https://gitlab.com/kicad/libraries/kicad-packages3D/-/blob/master/LICENSE.md).
