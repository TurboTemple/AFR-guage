# Building and installing the gauge, and logging it in HP Tuners

This takes you from a parts order to a working gauge in the car, with the same wideband signal logged in HP Tuners VCM Scanner next to the PCM's data.

Prices are UK, inc VAT, as found in October 2026. Anything marked *est* is an estimate. Wire colours and installation rules for the Spartan come from the [Spartan 3 Lite v2 user manual](https://cdn.shopify.com/s/files/1/0189/1312/files/Spartan_3_Lite_v2_User_Manual_Dec_19_2023.pdf?v=1703126168). The ProLink wiring comes from [HP Tuners' ProLink wiring article](https://docs.hptuners.com/Knowledge_Base_Articles/ProLink_Wiring/).

**Contents**
1. [Shopping list](#1-shopping-list)
2. [How it fits together](#2-how-it-fits-together)
3. [Flash the firmware](#3-flash-the-firmware)
4. [Build the interface board](#4-build-the-interface-board)
5. [Bench test](#5-bench-test)
6. [Fit the sensor](#6-fit-the-sensor)
7. [Wire it into the car](#7-wire-it-into-the-car)
8. [First start](#8-first-start)
9. [Log it in HP Tuners VCM Scanner](#9-log-it-in-hp-tuners-vcm-scanner)
10. [Using it](#10-using-it)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. Shopping list

### The gauge

| # | Part | Qty | Price | Where | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261) | 1 | £28.80 (£32.70 with case) | [The Pi Hut](https://thepihut.com/products/esp32-s3-1-75-amoled-round-touch-display-dev-board-466x466), [Waveshare](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm) | The Pi Hut was out of stock when checked. The version with a case is worth it. |
| 2 | 14Point7 Spartan 3 Lite v2 | 1 | £78.00 | [Bofi Racing](https://bofiracing.com/engine-performance/engine-electrical-ignition/engine-sensors-switches-solenoids/performance-sensors/14point7-spartan-2-wideband-lambda-controller/) | Comes with a fuse holder, two 5 A fuses and the status LED. |
| 3 | Bosch LSU 4.9, genuine (0 258 017 025) | 1 | £85.00–85.66 | [Bofi Racing](https://bofiracing.com/engine-performance/engine-electrical-ignition/engine-sensors-switches-solenoids/performance-sensors/genuine-bosch-lsu-4-9-wideband-o2-afr-sensor/), [Motorsport Electronics](https://motorsport-electronics.co.uk/product/bosch-lsu4-9-wideband-sensor/) | Buy genuine from a dealer. Never cut the plug off: it holds the sensor's factory calibration resistor. |
| 4 | M18×1.5 weld-in bung, stainless | 1 | £6 | [T7Design](https://newbuild-2025.t7design.co.uk/mwo-ss-m18x1-5.html) | Skip it if your exhaust already has a spare M18×1.5 port before the cats. |
| 5 | ADS1115 16-bit ADC module (generic, with ADDR and ALRT pins) | 2 | £3–6 each | eBay / Amazon UK | Check the chip is marked **BOGI**. A chip marked BRPI is an ADS1015, which is only 12-bit. |
| 6 | Metal-film resistors, 1 %: 10 kΩ ×6, 15 kΩ ×5, 47 kΩ ×1, 1 kΩ ×1 | 1 kit | ~£3 *est* | CPC / Amazon UK | A mixed 1 % kit covers this. The start-up calibration removes the dividers' gain error. |
| 7 | Capacitors: 220 nF C0G/film ×2, 100 nF ceramic ×4 | 1 set | ~£3 *est* | CPC / Farnell | The 220 nF go on the lambda inputs (C0G or polypropylene film, not Y5V). |
| 8 | 3.3 V zener (BZX55C3V3) | 1 | ~£0.20 *est* | CPC | Clamps the optional supply-sense input. |
| 9 | Stripboard or small protoboard | 1 | ~£4 *est* | CPC / The Pi Hut | Carries the ADCs and resistors. |
| 10 | 8-core screened cable, 0.22 mm² (7/0.2) | 5 m (car-dependent) | ~£8–10 *est* | CPC / RS (by the metre) | **Must be screened.** Unscreened alarm cable picks up ignition noise. |
| 11 | Connector for the cable at the gauge end (e.g. 8-way Superseal or Deutsch DT) | 1 pair | ~£8–12 *est* | CPC / Amazon UK | Lets the gauge come out without cutting wires. |
| 12 | USB-C hardwire kit, 12 V in, 5 V **3 A** out | 1 | £6–19 | Amazon UK | Powers the gauge from an ignition-switched fuse. Avoid 2 A kits. |
| 13 | Add-a-circuit fuse taps to suit the car's fuses | 2 | ~£6 *est* | Halfords / Amazon UK | One for the gauge feed. One for the Spartan, only if it can't take the fuel-pump relay feed directly. |
| 14 | Heater ground wire, 1.0 mm² or bigger, plus ring terminal | 1 m | ~£3 *est* | Halfords | The Spartan's heater ground (white) to the block or chassis. |
| 15 | microSD card, 8–32 GB, genuine SanDisk or Samsung | 1 | ~£7 *est* | Amazon UK | Format FAT32, 32 KB clusters. |
| 16 | Gauge pod or 3D-printed housing (PETG/ASA, not PLA) | 1 | £0–10 *est* | — | Keep it out of direct sun. |
| 17 | Heatshrink assortment and cable ties | — | ~£5 *est* | — | |

**Gauge subtotal: about £240–270** before postage, plus the bung welding below.

### HP Tuners logging

| # | Part | Qty | Price | Where | Notes |
| --- | --- | --- | --- | --- | --- |
| 18 | HP Tuners **PROLINK+** cable | 1 | $79.99 on HP Tuners' site; about $49.99 at US dealers. Budget £50–80 in the UK. | [HP Tuners](https://www.hptuners.com/product/pro-link-plus/) | Plugs into your MPVI4 and adds two 0–5 V inputs sampled at 100 Hz. The MPVI4 already includes the Pro Feature Set it needs. |

You already have the MPVI4.

### Labour and tools

| Item | Price | Notes |
| --- | --- | --- |
| Welding the bung | £0–50 *est* | Free if you or a friend weld; otherwise any exhaust shop. |
| 22 mm slotted O2 sensor socket | ~£8 *est* | |
| Torque wrench covering 40–60 Nm | — | Sensor torque. |
| Soldering iron, solder, multimeter, wire strippers, crimp tool | — | |
| USB-C **data** cable and a PC | — | For flashing and first set-up. |

### Optional

| Part | Price | Why |
| --- | --- | --- |
| BSS138 4-channel level-shifter module | ~£3 | Only needed if the Spartan ignores the gauge's 3.3 V commands (section 11). |
| 1S LiPo with protection, MX1.25 plug | ~£6–10 | Keeps the gauge running through cranking. Keep it out of the sun: cabin heat and LiPos don't mix. |
| Arduino Nano (5 V) + MCP4725 DAC module | ~£10 | Bench simulator (`sim/spartan_sim`) to test the gauge before it goes in the car. |

---

## 2. How it fits together

```
            exhaust                 engine bay                       cabin
  LSU 4.9 ──── plug ──── Spartan 3 Lite v2 ══ 8-core screened cable ══ interface board ── header ── gauge board ── USB-C ── hardwire kit ── ignition fuse
                          │ red   → 5 A fuse → engine-running 12 V        (ADS1115 ×2,                                 │
                          │ white → block/chassis (heater ground)          dividers)                                   └── microSD (logs)
                          │ brown → not used, insulate
                          └ green ───────────────────────────────────────────┬──→ PROLINK+ red ── MPVI4 ── VCM Scanner
                                                                             └ (gauge input)
```

- **The Spartan** runs the sensor and outputs lambda on the green wire: 0 V = λ 0.68, 5 V = λ 1.36 (10–20 AFR on petrol). It is powered only while the engine runs.
- **The gauge** reads the green wire at 860 samples a second. It also reads the blue wire (heater status) and talks to the Spartan over its UART (orange and yellow). It is powered with the ignition.
- **HP Tuners** reads the same green wire through the PROLINK+ and logs it with everything the PCM reports. Both devices take the signal from the same wire and share one ground point, so they read the same lambda.

---

## 3. Flash the firmware

Every push to GitHub builds the firmware automatically. To get the latest build:

1. On GitHub, open **TurboTemple/AFR-guage → Actions**. Click the newest run with a green tick, scroll to **Artifacts** and download **afr-gauge-firmware**. Unzip it. It contains `afr-gauge.bin`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin` and `flasher_args.json`.
2. Plug the board into the PC with a USB-C data cable. If it isn't detected, hold **BOOT**, tap **RESET**, then release BOOT.
3. Flash it. Pick one of these:
   - **In a browser (Chrome or Edge):** open the [Espressif web flasher](https://espressif.github.io/esptool-js/), click **Connect** and pick the board's port. Add the three files at these addresses, then click **Program**:

     | Address | File |
     | --- | --- |
     | `0x0` | `bootloader.bin` |
     | `0x8000` | `partition-table.bin` |
     | `0x10000` | `afr-gauge.bin` |

   - **From a terminal:**

     ```sh
     pip install esptool
     esptool.py --chip esp32s3 -p COM5 -b 460800 write_flash --flash_mode qio --flash_size 16MB \
       0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 afr-gauge.bin
     ```

     Use your port: `COM5` is Windows; on a Mac or Linux it looks like `/dev/ttyACM0`. If `flasher_args.json` lists different offsets, use those.
   - **To build it yourself** with ESP-IDF v5.5.5: `idf.py set-target esp32s3 && idf.py -p PORT flash monitor`.
4. The board restarts and shows **WIDEBAND**, then the main screen. With nothing connected it shows **FAULT – Lambda ADC not found**. That's expected, and it proves the display and touch work.

---

## 4. Build the interface board

The ESP32's pins are 3.3 V only and the Spartan's signals are 0–5 V. Every Spartan signal therefore goes through a **10 kΩ / 15 kΩ divider** (5 V becomes 3.0 V) before it reaches the ADCs or the ESP32.

### Board header (8-pin)

| Pin | Signal | Connect to |
| --- | --- | --- |
| 1 | VBUS | **Nothing.** Never feed power in here. |
| 2 | GND | The gauge ground point: both ADS1115 GND pins, every divider's bottom resistor, the Spartan's black wire, the cable screen and the PROLINK+ black wire. |
| 3 | 3V3 | VDD of both ADS1115s, ADDR of ADC #2, and the 10 kΩ pull-up on ALRT. |
| 4 | GPIO44 | UART RX: from the Spartan's **orange** wire through a 10 k / 15 k divider. |
| 5 | GPIO43 | ALRT/RDY of ADC #1 through a **1 kΩ** series resistor. |
| 6 | GPIO17 | I2C SDA to both ADS1115s. |
| 7 | GPIO18 | I2C SCL to both ADS1115s. |
| 8 | GPIO16 | UART TX: to the Spartan's **yellow** wire, direct. |

### ADC #1: lambda (address 0x48)

| ADS1115 pin | Connect to |
| --- | --- |
| VDD / GND | Header pin 3 / pin 2 |
| SDA / SCL | Header pins 6 / 7 |
| ADDR | **GND** (address 0x48) |
| ALRT | 1 kΩ to header pin 5, plus a 10 kΩ pull-up to 3V3 |
| A0 | **Green** (lambda) → 10 kΩ → A0. From A0: 15 kΩ to GND and **220 nF** to GND. |
| A1 | **Ground sense** → 10 kΩ → A1. From A1: 15 kΩ to GND and **220 nF** to GND. |
| A2, A3 | Not used |

The **ground sense** is its own core in the cable, joined to the Spartan's black wire **at the Spartan end**. The gauge reads green *minus* this wire, which cancels any voltage difference between the Spartan's ground and the gauge's ground.

### ADC #2: housekeeping (address 0x49)

| ADS1115 pin | Connect to |
| --- | --- |
| VDD / GND / SDA / SCL | Shared with ADC #1 |
| ADDR | **3V3** (address 0x49) |
| ALRT | Not used |
| A0 | **Blue** (heater status) → 10 kΩ → A0. From A0: 15 kΩ to GND and 100 nF to GND. |
| A2 | *Optional supply sense:* **red** (Spartan 12 V, after its fuse) → 47 kΩ → A2. From A2: 10 kΩ to GND, 100 nF to GND and the 3.3 V zener to GND (band to A2). It is off in the standard build: to use it, build the firmware yourself with **AFR gauge → supply sense fitted** turned on in `idf.py menuconfig`. Otherwise leave it out. |
| A1, A3 | Not used |

### UART

- **Orange** (Spartan TX, 5 V) → 10 kΩ → header pin 4, with 15 kΩ from pin 4 to GND.
- Header pin 8 → **yellow** (Spartan RX), direct.

### Tips

- Keep the green and sense dividers next to ADC #1, and keep the 220 nF capacitors' leads short.
- Put a 100 nF capacitor across VDD–GND on each ADS1115 if the module doesn't already have one (most do).
- The ADS1115 modules already have pull-ups on SDA and SCL, so don't add more.

---

## 5. Bench test

Do this before anything goes near the car.

1. **Power:** board on USB, interface board fitted, nothing on the green wire. The screen should show **ENGINE OFF**. If it shows *FAULT – Lambda ADC not found*, check ADC #1's ADDR, SDA, SCL and power. If it shows *LED ADC not found*, check ADC #2's ADDR is on 3V3.
2. **ADC check:** put a known voltage on the green input (a bench supply, or a 1.5 V AA cell for a rough check) with the sense input on GND. Swipe right to **Settings**, scroll to **Service**, and compare *v_out* with a multimeter. It should match to about 0.01 V. The *missed* and *gaps* counters should stay at 0.
3. **Optional simulator:** `sim/spartan_sim` on an Arduino Nano with an MCP4725 behaves like a Spartan. It runs the start-up calibration steps and then a session with a lean-out, fuel cuts and rich moments. Each one should show its warning, and the lean-out should light the red ring. The wiring is in the sketch header.

---

## 6. Fit the sensor

Rules from the Spartan manual, plus the research notes:

- **Before the catalytic converters.** The cats remove oxygen and make the reading wrong.
- **About 60 cm (2 ft) from the exhaust port** on a naturally aspirated engine, after the collector. With long-tube headers, a few inches past the collector usually meets this.
- **Between 10 and 2 o'clock**, within 60° of vertical, so condensation drains away from the sensor. Never on the underside of the pipe.
- **One bank only.** On a V8 the wideband reads the bank it's fitted to, and the PCM's own O2 sensors stay where they are. If you can, use the bank whose O2 sensor is in the same position, so the two readings compare like for like.
- **No exhaust leaks upstream.** A leaking manifold, collector gasket or bung weld draws in air and reads lean, even at full throttle. Fix leaks first.

Steps:

1. Mark the bung position and check the sensor body and cable clear the floor, the gearbox tunnel and anything that moves.
2. Drill or cut the hole to suit the bung (usually 22 mm / 7/8") and weld the bung on all round. Pin-holes in the weld read lean.
3. Clean the threads. A genuine Bosch sensor comes with anti-seize already on its thread. If you add any, keep it **off** the sensor's tip and slots.
4. Fit the sensor and torque it to **40–60 Nm** with the slotted socket.
5. Route the sensor cable away from the exhaust: the connector must stay below 140 °C and the cable below 250 °C. Leave slack for engine movement.
6. **Never run the engine with the sensor fitted but not powered by the Spartan**: soot fouls an unheated sensor. Until the Spartan is wired in, leave the original plug in the bung.

---

## 7. Wire it into the car

Disconnect the battery while you wire it.

### The Spartan

| Spartan wire | Connect to | Notes |
| --- | --- | --- |
| **Red** | Supplied 5 A fuse → a 12 V feed that is live **only while the engine runs** | The manual recommends the fuel pump relay output. Find it on your car's wiring diagram. On many GM cars the pump also runs for about 2 s at key-on to prime; that brief power-up is harmless. |
| **White** | Engine block or chassis, ≥1.0 mm², short | Heater ground, carries about 3 A at peak. It must be connected whenever the Spartan powers up, or the Spartan starts in its bootloader instead of running. |
| **Black** | The gauge ground point (through the cable) | Electronics ground. Ground it where the gauge is grounded, **not** to the block. |
| **Green** | Through the cable to the interface board (ADC #1 A0) **and** the PROLINK+ red wire (section 9) | Lambda output, 0–5 V |
| **Blue** | The supplied LED (long lead to blue, short lead to ground) **and**, through the cable, to ADC #2 A0 | Heater status |
| **Orange** | Through the cable to the UART divider (header pin 4) | Spartan TX, 5 V |
| **Yellow** | Through the cable to header pin 8 | Spartan RX |
| **Brown** | Nothing. Insulate it. | Simulated narrowband. Not needed: your PCM keeps its own O2 sensors. |

Mount the Spartan somewhere dry and cool: in the cabin, or on the bulkhead away from heat. Plug the sensor in **with the Spartan unpowered**, and never connect or disconnect it while the Spartan is on.

**Extra protection:** once the gauge is talking to the Spartan, set **Settings → Spartan heat-up → Wait**. The Spartan then waits (up to 10 minutes) for exhaust heat before heating the sensor, which protects it from condensation at cold start.

### The signal cable (8-core screened)

| Core | Spartan end | Gauge end |
| --- | --- | --- |
| 1 | Green | Green divider (ADC #1 A0) |
| 2 | **Joined to black at the Spartan** | Sense divider (ADC #1 A1) |
| 3 | Black | Gauge ground point |
| 4 | Blue | Blue divider (ADC #2 A0) |
| 5 | Orange | UART divider (header pin 4) |
| 6 | Yellow | Header pin 8 |
| 7 | Red, after the fuse (optional) | Supply-sense divider (ADC #2 A2) |
| 8 | Spare | — |
| Screen | **Not connected** at this end | Gauge ground point |

- Ground the screen at the gauge end only.
- Keep the cable away from coil packs and plug leads, and cross them at right angles where it can't be avoided.
- Through the bulkhead, use an existing grommet or a new one: never bare holes.

### The gauge

1. Fit a fuse tap in an **ignition-switched** fuse slot and connect the USB-C kit's red lead to it and its black lead to a good chassis earth near the fuse box.
2. Run the USB-C lead to the gauge and plug it into the board's USB-C port.
3. Mount the gauge where you can see it at a glance during a pass. Not on the top of the dashboard in direct sun, which is too hot for the board and any LiPo.
4. Put the microSD card in.

---

## 8. First start

1. **Ignition on, engine off.** The gauge shows **ENGINE OFF** with the STREET chip.
2. **Start the engine.** The Spartan powers up:
   - **CALIBRATING**: the Spartan holds 1.67 V then 3.33 V for about 5 s each. The gauge uses these to calibrate its own readings, and the bar fills blue.
   - **WARMING UP** (or *WAITING FOR EXHAUST HEAT* if you set heat-up to Wait).
   - Then the number appears, usually within 30–60 s.
3. **Check the status line** at the bottom: **● LOG  TARGET 1.00**. If it says *NOT CALIBRATED* or *CAL REJECTED*, see section 11.
4. **Check the Service screen** (swipe right to Settings, then Service). You should see:
   - *Sensor LSU 4.9*
   - *Cal FRESH*
   - the Spartan's firmware version.

   In **Settings**, *Spartan response* should read **10 ms** with no star (the gauge sets it at every start-up).
5. **Sanity check at idle:** a healthy warm engine in closed loop should sit around **λ 1.00** and wander a few hundredths either side. A steady reading well above 1.05 at idle points to an exhaust leak, or a misfire, ahead of the sensor.
6. Switch off and take the microSD card out. The log (`/logs/YYYYMMDD_HHMMSS.csv`) should start with `# cal=FRESH ...`, `# spartan_fw=...` and `# spartan_perf=1`.

---

## 9. Log it in HP Tuners VCM Scanner

The gauge firmware doesn't need any change for this. The PROLINK+ reads the same green wire.

### Wire the PROLINK+

| PROLINK+ wire | Connect to |
| --- | --- |
| **Red** (Analog 1) | Spartan **green**, spliced at the gauge end before the green divider |
| **Black** (GND) | The **gauge ground point**, which the Spartan's black wire also goes to |
| Blue (Analog 2) | Spare: a second 0–5 V input for later |
| Orange / Yellow (CAN H / L) | Not used. Insulate. |

Splice it at the gauge end so the gauge and HP Tuners use the same ground point. Then a ground difference can't make them disagree. Leave a disconnectable joint (a 2-way connector) so the PROLINK+ can be unplugged when you take the laptop out.

These colours are from HP Tuners' ProLink wiring article. Before you connect it, check your cable with a multimeter: HP Tuners says that with nothing connected, an analog input floats at about 1.34–1.37 V.

### Set up the channel

1. Plug the PROLINK+ into the MPVI4 and the MPVI4 into the car's OBD port and the laptop. Open **VCM Scanner**.
2. In the **Channel Selector**, open **External Inputs → MPVI → Pro Link** and double-click **A/D Input 1**, the red wire. Depending on the software version the folder may be named after your interface (MPVI2, MPVI4).
3. Right-click the new channel and choose **Transform**. Set up a linear transform for the Spartan's default output:

   | Volts | Lambda | AFR (petrol, 14.7) |
   | --- | --- | --- |
   | 0.0 | 0.68 | 10.0 |
   | 5.0 | 1.36 | 20.0 |

   In formula form: **λ = 0.68 + 0.136 × V**, or **AFR = 10 + 2 × V**. Work in lambda if you can: it stays right whatever the fuel.
4. **Check that the gauge and VCM agree.** With the engine off and the Spartan powered, open the gauge's **Service** screen and use **ADC check** (tap a button twice). The Spartan holds its output at exactly that voltage until it restarts: 1.0 V, 2.5 V or 4.0 V, which is λ 0.816, 1.02 and 1.224. The gauge's *v_out* and VCM Scanner's channel should both show it. Power-cycle the Spartan afterwards to go back to normal.
5. **Add the channel to your layout:** put it in the Chart vs Time and next to RPM, MAP, TPS, spark advance, knock retard and the PCM's **Commanded EQ Ratio**.
6. **Optional fuel-error channel:** add a math parameter for how far the measured mixture is from what the PCM asked for. Commanded lambda is 1 / EQ, so:

   **error % = (wideband λ × commanded EQ − 1) × 100**

   Positive means leaner than commanded. Build it with VCM Scanner's math parameter editor, pointing at your transformed Pro Link channel and the PCM's commanded EQ channel.

### Logging

- **With the laptop:** start a log as usual. The PROLINK+ channel records at up to 100 Hz alongside the PCM data.
- **Standalone (no laptop):** the MPVI4's Pro Feature Set supports standalone logging. Make sure the Pro Link channel is in the channel list you load onto the MPVI4 for standalone logging.
- **The gauge logs too.** Its microSD log has every sample at 215 Hz, with the warning and mode columns. Use it for detailed lambda work, and the VCM log for lambda against RPM, spark and knock retard.

### Reading a pass

What tuners look for on an NA LS:
- full throttle at about **λ 0.84–0.88**;
- a brief lean blip at each shift, which is normal: fuel cut or throttle movement;
- **a lean trend that grows with RPM** or late in each gear, which usually means fuel delivery (pump, filter, regulator or voltage) rather than the tune;
- **lean plus knock retard together** is the danger pattern.

The sensor reacts a little after the engine. Allow tens of milliseconds for the sensor, plus the exhaust's travel time to it, when lining lambda up against RPM.

---

## 10. Using it

| Input | Action |
| --- | --- |
| **Double-tap** the screen | Street ↔ race mode. The chip at the top shows which. |
| **Long press** (1 s) | Reset the peak boxes and clear the lean ring |
| Swipe left | 30-second strip chart |
| Swipe right | Settings → Service, Clock |
| BOOT button | Day / night brightness |

- **Street mode:** warns when leaner than λ 1.05 for 300 ms, around a 1.00 target.
- **Race mode:** warns when leaner than **λ 0.92 for 15 ms**, around a **0.86** target, with the bar zoomed in on full throttle. In race mode the screen is also red at idle, cruise and on the lift, because the gauge can't see the throttle. Use it for passes.
- **Red screen** = lean now. **Amber screen** = rich now (below λ 0.75).
- **Red ring** = there was a lean-out in the last 5 minutes that recovered. A long press clears it.
- Set your own limits under **Settings**: race lean λ, race delay, race target λ, street lean λ, street delay, rich limit λ and rich delay. If your tune's full-throttle target isn't 0.86, set **Race target** to it and **Race lean** about 0.05 above it.

---

## 11. Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| **FAULT – Lambda ADC not found** | ADC #1 not answering | Check its 3V3, GND, SDA (pin 6), SCL (pin 7), and ADDR on GND. |
| **FAULT – LED ADC not found** | ADC #2 not answering | Check its ADDR is on **3V3**, and its power. |
| Stuck on **ENGINE OFF** with the engine running | Spartan not powered, or the blue wire not reaching ADC #2 | Check the red feed, its fuse and the white heater ground. Check the blue wire and its divider. |
| **NOT CALIBRATED** at the bottom | The gauge didn't see the Spartan's start-up calibration steps | Normal before the first engine start. Otherwise check the green and sense wiring. |
| **CAL REJECTED** | The calibration steps looked wrong | Check the 10 k / 15 k values on the green and sense inputs, and the ground-sense core. |
| Service screen never shows the Spartan's firmware | UART not working | Check orange → divider → pin 4 and yellow → pin 8. If they're right, the Spartan may not accept the gauge's 3.3 V commands: fit a BSS138 level shifter on both UART lines. |
| Reading jumps around | Noise | The cable must be screened, with the screen grounded at the gauge end only. Keep it away from the coils. Service: the *spikes* counter should stay low. |
| Lean at idle, normal at full throttle | Exhaust leak, cam overlap or reversion | Fix leaks ahead of the sensor. Some idle lean reading is normal with a big cam. |
| Gauge and VCM disagree | Ground difference or a wrong transform | Both grounds must be at the gauge ground point. Re-check the transform (0 V = 0.68, 5 V = 1.36), and use the ADC check in section 9. |
| **NO LOG** | No card, or card not FAT32 | Reformat FAT32 (32 KB clusters) and reinsert. |
| Gauge restarts while cranking | The USB kit's low-voltage cut-out | Harmless. Fit the optional LiPo to ride through it. |
