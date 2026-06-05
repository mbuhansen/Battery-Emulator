# BMW i3 Charging Termination Analysis

This document describes how a real BMW i3 Battery Management System (BMS) signals to the charger that charging is complete. The analysis is based on physical log data in [Software/end_charging_batt_car.log](Software/end_charging_batt_car.log) and decoded using equations from [Software/src/battery/BMW-I3-BATTERY.cpp](Software/src/battery/BMW-I3-BATTERY.cpp).

---

## Executive Summary: How Charging Ends

The car's BMS terminates charging through a precise sequence of three active signals rather than depending purely on the display State of Charge (SOC) to stop, as the display SOC reaches 100% nearly a minute before physical termination:

1. **Max Allowed Charging Current is set to 0.0 A (Message `0x2F5`, Bytes 2 & 3):**
   * **Formula:** `battery_max_charge_amperage = (((Byte3 << 8) | Byte2) - 8192) / 10.0`
   * During late-stage balancing, the limit is held at `1.2 A` (value `8204`).
   * At line **100295** (timestamp **23:59:09.291**), this drops to exactly `0.0 A` (value `8192` or `0x2000`).

2. **Display SOC is set to 100.0% (Message `0x432`, Byte 4):**
   * **Formula:** `battery_display_SOC = Byte4 / 2.0`
   * Only **0.06 seconds** after the current limit is set to 0.0 A, at line **100377** (timestamp **23:59:09.352**), the dashboard SOC is updated to `100.0%` (value `C8` or `200`).

3. **BMS Abort Charging Flag is activated (Message `0x431`, Byte 0):**
   * **Formula:** `battery_request_abort_charging = (Byte0 & 0x30) >> 4`
   * Byte 0 is normally `CA` (abort flag = `0`). 
   * Active termination is triggered at line **101675** (timestamp **23:59:10.292**) when Byte 0 shifts to `DA` (abort flag = `1`). This commands the charger to shut down and open contactors immediately.

---

## Chronological Progression of the Stop Sequence

The following timeline represents the exact progression of values extracted leading up to the end of charging:

| Log Line | Timestamp | Raw SOC (0-1023 Ticks) | Display SOC (%) | Max Current (A) | Max Power (W) | Abort Flag (0x431) | Status / Action |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **154** | 23:57:50.847 | 872 | 99.5 % | 1.2 A | 5829 W | 0 | Continuous late balancing |
| **5809** | 23:57:55.286 | 872 | 99.5 % | 1.1 A | 5814 W | 0 | Balancing current lowered |
| **100295** | **23:59:09.291** | **872** | **99.5 %** | **0.0 A** | **5859 W** | **0** | **BMS curtails current limit to 0.0 A** |
| **100377** | **23:59:09.352** | **872** | **100.0 %** | **0.0 A** | **5832 W** | **0** | **Dashboard SOC reaches 100.0%** |
| **101675** | **23:59:10.292** | **872** | **100.0 %** | **0.0 A** | **5859 W** | **1** | **BMS commands charger abort = 1** |
| **183882** | 00:00:09.656 | 872 | 100.0 % | 0.0 A | 6204 W | 1 | Complete shutdown / log ends |

*Note: Raw high-voltage battery (physical SOC) stays constant at 872 ticks (~99.7%) during this transition, showing that the physical cells are fully charged.*

---

## Guidelines for Emulator Implementation

To accurately model a realistic BMW i3 battery charging cutoff sequence in the emulator firmware:

1. **Phase 1 (Active Balancer Done):**
   * Change message `0x2F5` bytes 2 and 3 to `00 20` (which decodes to `8192` ticks = `0.0 A`).

2. **Phase 2 (Dashboard update):**
   * Set display SOC in message `0x432` byte 4 to `C8` (which decodes to `100.0%`).

3. **Phase 3 (Active cutoff mandate):**
   * Change message `0x431` byte 0 from `CA` to `DA` (setting `battery_request_abort_charging = 1`).

---

## Emulator & Inverter SOC Drift Mitigation

### Observed Issue
We have observed that the State of Charge (SOC) representation drifts over time. Consequently, the battery does not get fully charged. 

Currently, the emulator simulates that the vehicle is in **Drive Mode** during charging rather than **Charge Mode**. Under Drive Mode (often mimicking vehicle operation), the Battery Management System (BMS) relies exclusively on *Coulomb Counting* (integrating current over time). This method naturally drifts or accumulates error over long periods and cannot calibrate its state based on cell voltages.

In contrast, **Charge Mode** triggers the BMS to periodically calibrate and synchronize the calculated SOC with the actual physics profile (voltage-curve synching based on cell voltages and relaxation states), ensuring it correctly maps the real SOC and allows the battery to fill fully.

### Next Steps & Architecture Workaround
1. **Drive Mode vs. Charge Mode Switching:**
   * A solution must be engineered where the emulator dynamically toggles the reported state between **Drive Mode** and **Charge Mode**.
   * Charging intervals should map to **Charge Mode** to activate physical cell voltage calibration (Voltage-Sync) and top balancing routines, aligning the software SOC with the physical cells.
2. **Post-Shutdown Balancing:**
   * Cell balancing on the real BMW i3 battery occurs *after* the charger shut down cycle has been triggered. This post-shutdown balancing logic is managed correctly, and our main focus is implementing the mode transitions to prevent visual SoC drift and secure maximum battery capacity.
3. **Inverter Current Limits:**
   * The calculated parameter `max_charge_current_dA` (recalculated automatically from the battery's reported max charge power in `0x40D` or `0x3EB`) is the exact value processed by the emulator and relayed straight to the solar inverter to curtail or stop charging.

