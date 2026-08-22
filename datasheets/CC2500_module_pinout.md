# CC2500 Module Pinout

Two module boards have been verified with this project:

- [small AliExpress pad module](https://de.aliexpress.com/item/1005005501196142.html), sold as
  "CC2500 module 2.4G", with a 26.000 MHz crystal and PCB folded-dipole antenna;
- [TinyTronics CC2500 Wireless RF Module 2.4 GHz](https://www.tinytronics.nl/en/communication-and-signals/wireless/rf/modules/cc2500-wireless-rf-module-2.4ghz),
  a larger board with fitted header pins that can be connected with female Dupont jumpers.

Files here:

- [`CC2500_module_photo.jpeg`](CC2500_module_photo.jpeg) — board drawing, 12 × 19.125 mm,
  with pads numbered PIN1…PIN9.
- [`CC2500_module_schematic.jpeg`](CC2500_module_schematic.jpeg) — the CC2500 application
  circuit the module implements.

**Soldering depends on the board.** The small module has nine pads on a **1.27 mm** pitch, so a
standard 2.54 mm header does not fit; solder six wires directly (`SI`, `SCLK`, `SO`, `CSn`, `VDD`,
`VSS`). The TinyTronics board needs no soldering when used with female Dupont jumpers. Its header
order differs from the pad numbering below, so wire by the labels printed on that board.

Full CC2500 datasheet: <https://github.com/SergeyZh/emotion-control-re/tree/main/datasheets>.

The pin numbers below apply to the small pad module only:

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
