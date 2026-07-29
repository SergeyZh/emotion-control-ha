# CC2500 Module Pinout

The module used and verified in this project:
<https://de.aliexpress.com/item/1005005501196142.html> (sold as "CC2500 module 2.4G",
26.000 MHz crystal, PCB folded-dipole antenna).

Files here:

- [`CC2500_module_photo.jpeg`](CC2500_module_photo.jpeg) — board drawing, 12 × 19.125 mm,
  with pads numbered PIN1…PIN9.
- [`CC2500_module_schematic.jpeg`](CC2500_module_schematic.jpeg) — the CC2500 application
  circuit the module implements.

**Soldering is required.** Every CC2500 module found on the market, this one included, ships
without a pin header: the nine pads sit along the bottom edge on a **1.27 mm** pitch, so a
standard 2.54 mm header does not fit. Solder wires straight to the pads — six of them are
needed (`SI`, `SCLK`, `SO`, `CSn`, `VDD`, `VSS`).

Full CC2500 datasheet: <https://github.com/SergeyZh/emotion-control-re/tree/main/datasheets>.

| Pin | Name | Pin function | Description |
|---:|---|---|---|
| 1 | VDD | Power | Power Supply (+1.9–3.6 V DC) |
| 2 | SI | Digital Input | SPI Slave Data Input |
| 3 | SCLK | Digital Input | SPI Clock |
| 4 | SO | Digital Output | SPI Slave Data Output, with tri-state option |
| 5 | GDO2 | Digital Output | Digital output pin for general use |
| 6 | VSS | Power | Ground (0 V) |
| 7 | GDO0 | Digital In/Output | Digital GPIO pin for general use |
| 8 | CSn | Digital Input | Serial configuration interface, chip select |
| 9 | OSCO | Analog output | Crystal output |
