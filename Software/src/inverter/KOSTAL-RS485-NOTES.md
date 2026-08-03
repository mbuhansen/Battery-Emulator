# KOSTAL RS485 - protocol findings and open work

Working notes for `KOSTAL-RS485.cpp` / `.h`. Everything marked **confirmed** was verified
against a real Kostal inverter by reading its Modbus/SunSpec registers and comparing with
the bytes we send.

Reference log: `Kostal_byd_logs_Germanmann/Start Inverter after Battery on and charged from
98-100% and afterwards.txt`, line 302, from a **BYD Premium Box HVS 7.7** (3 modules of
2.56 kWh, 32 LFP cells each -> 3 * 32 * 3.65 V = 350.4 V max, 25 Ah).

## Critical gotcha: the arrays hold UNSTUFFED frames

`BATTERY_INFO[]` and `CYCLIC_DATA[]` must contain the frame **before** COBS null stuffing.
`null_stuffer()` replaces every real `0x00` with the distance to the next `0x00` at send
time, and `calculate_kostal_crc()` runs on the unstuffed bytes.

A sniffed log line is the **stuffed** frame. Copying it verbatim into the array turns the
stuffing pointers into payload: the frame still passes CRC (it is recomputed on send), so
the inverter accepts it, but every field that contained a zero byte is silently wrong.

To convert a sniffed frame back: `b[0]` is the offset to the first zero, the byte at that
position is the offset to the next, and so on until the terminating `0x00`. Every position
in that chain was originally `0x00`.

## Byte-order when reading the values back

Kostal's own Modbus map uses little-endian word order for 32-bit values. A client that
assumes big-endian shows the two 16-bit words swapped, i.e. the value multiplied by 65536.
SunSpec registers are big-endian and read correctly. Observed on four independent fields:

| Register | Client showed | Actual value |
| --- | --- | --- |
| Battery Firmware | `0x031A0000` | `0x0000031A` = 794 |
| Battery Model ID | 131072 | 2 |
| Battery Gross Capacity | 1638400 | 25 Ah |
| BMS Serial Number | 1123354181 | `0x064542F5` = 105202421 |

## BATTERY_INFO (40 bytes, unstuffed)

| Byte | Content | Status |
| --- | --- | --- |
| 0 | COBS pointer, filled by `null_stuffer()` | - |
| 1-5 | Frame header `E2 FF 02 FF 29` | - |
| 6-9 | Max voltage, float32 | dynamic (`max_design_voltage_dV / 10`) |
| 10-13 | Manufacture date, epoch uint32 | static |
| 14-17 | Serial number | **confirmed** -> "BMS Serial Number" |
| 18-21 | Nominal capacity Ah, float32 (25.0) | **static, should be dynamic** |
| 22-23 | Firmware, uint16 (`0x031A`) | **confirmed** -> "Battery Firmware" |
| 24 | `0x01`, unknown | static |
| 25 | `0x00`, unknown | static |
| 26-27 | Vendor id, `YB` = BYD, `YD` = Dyness | static |
| 28-29 | Unknown | static |
| 30-31 | Model ID, uint16 = 2 | **confirmed** -> "Battery Model ID" |
| 32-33 | Blocks in series, uint16 = 3 | **static, should be dynamic** |
| 34 | `0xA0`, unknown | static |
| 35-37 | `FF FF FF`, one of these is State of Health in % | **probe in progress** |
| 38 | CRC | recomputed on send |
| 39 | Frame terminator `0x00` | - |

The inverter derives **Nameplate Energy = nominal capacity (18-21) * max voltage (6-9)**.
Confirmed: 25 Ah * 395.0 V = 9875 Wh with a BMW i3 60Ah pack.

Note this overstates the real energy, because the capacity is multiplied by the *max*
voltage rather than the nominal one. The reference HVS log does send max voltage here, so
a real BYD behaves the same way. Decide later whether to match BYD or to report honestly.

## CYCLIC_DATA (64 bytes, unstuffed)

Field layout is documented inline in `KOSTAL-RS485.h`. Two notes:

- Bytes 30-33, "battery gross capacity Ah", is **static at 25.0** and read back both as
  Kostal's "Battery Gross Capacity" and SunSpec "Battery Nameplate Charge Capacity".
  It used to be computed from `total_capacity_Wh / nominal_voltage_dV * 10`, but commit
  `1e32d5f9` removed the only assignment to `nominal_voltage_dV`, leaving the branch dead.
  Commit `84341c12` deleted the dead code.
- Bytes 26-29 and 34-37 (max discharge / max charge current) are written twice in
  `update_values()`: once from the datalayer, then overwritten by the shunt/contactor
  logic further down. Only the second write reaches the inverter.

## Cross-reference against the BYD Battery-Box Premium label

The label on the physical box numbers the models, and the same numbering indexes the
usable-energy and operating-voltage tables:

| # | Model | Usable energy | Operating voltage |
| --- | --- | --- | --- |
| 1 | HVS 5.1 | 5.12 kWh | 160-230 V |
| 2 | HVS 7.7 | 7.68 kWh | 240-345 V |
| 3 | HVS 10.2 | 10.24 kWh | 320-460 V |
| 4 | HVS 12.8 | 12.8 kWh | 400-576 V |
| 5 | HVM 8.3 | 8.28 kWh | 120-173 V |
| 6 | HVM 11.0 | 11.04 kWh | 160-230 V |
| 7 | HVM 13.8 | 13.8 kWh | 200-288 V |
| 8 | HVM 16.6 | 16.56 kWh | 240-345 V |
| 9 | HVM 19.3 | 19.32 kWh | 280-403 V |
| 10 | HVM 22.1 | 22.08 kWh | 320-460 V |

Max continuous current: 25 A (HVS), 50 A (HVM).

The reference log is from an HVS 7.7 and sends **Model ID = 2**, which is exactly that
model's number in the list. Blocks in series = 3 matches its 3 modules independently.

The inverter clearly looks the model up: with the old frame the field was 0 and the
inverter identified the battery as an old BYD HV pack, with 2 it identifies an HVS 7.7.
Usable energy and the operating voltage window appear nowhere in the frame, so those must
come from the same table.

**The lookup does not clamp the reported voltage.** With model id 2 (window 240-345 V) the
inverter still reports our 395 V correctly over SunSpec, so live values are taken from our
frames rather than from the table.

**It does not limit current either.** With model id 2, rated 25 A max continuous, the
inverter used the 30 A we sent as max discharge current: it reported 10913 W at 363.77 V,
which is exactly 30 A times the live pack voltage. So the model id is identification only,
and there is no need to pick a model whose window matches the emulated pack.

Side effect confirmed: the inverter computes power itself as our current limit times the
live pack voltage from cyclic bytes 6-9.

Other label values that do map onto the frame: 7.68 kWh / 307.2 V nominal = 25 Ah, the
same 25 that the nominal capacity field carries, and also the same number as the 25 A max
continuous current because HVS is rated 1C.

## The inverter caches BATTERY_INFO

`BATTERY_INFO` is only sent when the inverter asks with code `0x84a`, which it does at
startup. Values from an old firmware build survive across our reflashes: firmware still
read `0x18C2` and nameplate energy still 8175 Wh long after we changed both.

**Testing any info-frame change requires a real inverter restart**, not just a reflash of
the emulator.

## TODO - make the values match the connected pack

1. **State of health.** Find which of bytes 35-37 is SoH, then feed it from
   `datalayer.battery.status.soh_pptt / 100`. Currently sends `0xFF` -> reads back 255 %.
   Probe: bytes 35/36/37 set to 100/80/60, whichever number appears identifies the byte.
2. ~~**Nominal capacity** (info 18-21) from the actual pack instead of 25.0 Ah.~~ Done.
3. ~~**Gross capacity** (cyclic 30-33) from the actual pack, equal to the info value.~~ Done.
4. **Blocks in series** (info 32-33) - decide what this should reflect for a non-BYD pack.
   The **model id** (info 30-31) needs no change: it only affects which battery the
   inverter names, not the voltage or current it allows.
5. Consider deriving serial number and firmware from something real instead of the BYD
   log values, now that we know both are read back.

### The two capacity fields are not the same in a real BYD

Decoded from the reference logs, both verified by CRC:

| Frame | Field | Value |
| --- | --- | --- |
| Info | bytes 18-21, nominal capacity | 25.0 Ah |
| Cyclic | bytes 30-33, gross capacity | 23.000002 Ah |

The cyclic value is identical at 98 % and at 100 % SoC, so it is **not** remaining charge.
With 1048 cycles on the pack (cyclic bytes 54-55), 23.0 / 25.0 = 92 % reads as the present
capacity after degradation against the nameplate one.

We currently send the same number in both, because `total_capacity_Wh` is already the
present capacity for the batteries we emulate - for a BMW i3 it is
`battery_energy_content_maximum_Wh` straight from the BMS. Reconstructing a nameplate
capacity would mean dividing by SoH, i.e. guessing at a number the datalayer does not have,
and it would make the inverter's nameplate energy a from-the-factory figure rather than the
real one.

Evidence so far says the inverter takes SoH from info bytes 35-37 rather than from the
ratio of these two fields: the two observed readings were 0 % and 255 %, while the ratio at
those times was 800 % and 100 %. If the SoH probe contradicts that, divide the info field
by `soh_pptt` instead of sending the same value twice.

### Why the capacity is referenced to the max voltage

The inverter shows the capacity we send and, separately, that capacity times the voltage in
info bytes 6-9. Only one of the two can be made to match the physical pack, unless the true
nominal voltage of the pack is known - and the datalayer only carries energy in Wh, not Ah,
so any Ah figure has to be derived from a voltage anyway.

Dividing the pack energy by the same max voltage we already send makes the **energy** exact
and the two readings mutually consistent, at the cost of an Ah figure that reads low: a
BMW i3 60Ah triple pack shows about 166 Ah instead of the physical 180 Ah. Energy was
prioritised because that is what drives the inverter's kWh and SOC display.

Sending the true Ah instead would overstate the energy by about 9 %. A real BYD does
exactly that: an HVS 7.7 reports 25 Ah and therefore 8760 Wh against 7680 Wh on the label.

### Picking the nominal voltage for Ah calculations

The old code used `min + (max - min) / 2`, which for a BMW i3 60Ah pack
(`MAX_PACK_VOLTAGE_60AH` 3950, `MIN_PACK_VOLTAGE_60AH` 2590) gives exactly 327.0 V. That
is the midpoint of the charge/discharge window, not the nominal voltage of a 96s NMC pack
(~353 V), so it inflates the Ah result: 65400 Wh / 327 V = 200 Ah reported against a real
3 x 60 Ah = 180 Ah. Using `number_of_cells * nominal cell voltage` would be closer.

Also avoid the old integer arithmetic `total_capacity_Wh / nominal_voltage_dV * 10`, which
truncates the division first and therefore quantises the result to 10 Ah steps.
