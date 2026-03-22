# temp_monitor
STM32L432KC monitors on-chip temperature with VREFINT compensation for accuracy. Outputs data via LCD, USART2, buzzer, and LED. Ideal for industrial/refinery use, providing real-time monitoring and alarms for overheating conditions.

Built and flashed with **PlatformIO** inside VS Code.

---

## Features

| What | How |
|---|---|
| Temperature sensing | Internal ADC sensor, VDDA-compensated via VREFINT |
| Live display | ST7735 160×80 SPI colour LCD (5-wire) |
| Threshold alert | Buzzer on TIM2_CH1 (~3 kHz) + LED blink when ≥ 30 °C |
| Serial logging | USART2 at 9600 baud — paste into any serial monitor |
| Emergency stop | Button on PB4 cleanly disables everything |

---

## Project Structure

```
├── src/
│   ├── main.c            # Application logic, setup, ADC, UART, TIM2
│   ├── display.c         # ST7735 driver — graphics, text, shapes
│   ├── spi.c             # SPI1 init + 8/16-bit transfer helpers
│   └── eeng1030_lib.c    # Clock init, GPIO helpers, SysTick delay
│   ├── display.h
│   ├── spi.h
│   ├── eeng1030_lib.h
│   └── font5x7.h         # 5×7 bitmap font for LCD text rendering
└── platformio.ini        # PlatformIO build configuration
```

---

## Pin Map

```
PA0  ──  TIM2_CH1  (AF1)   Buzzer PWM output
PA1  ──  SPI1_SCK  (AF5)   LCD clock
PA2  ──  USART2_TX (AF7)   Serial out → USB-UART adapter
PA4  ──  GPIO Out          LCD CS  (chip select)
PA5  ──  GPIO Out          LCD D/C (data/command)
PA6  ──  GPIO Out          LCD RST (reset)
PA7  ──  SPI1_MOSI (AF5)   LCD data
PB3  ──  GPIO Out          Status LED
PB4  ──  GPIO In (pull-up) Shutdown button (active LOW)
```

---

## Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
- ST-Link driver (ships with most Nucleo boards)

### 1 — Clone the repository

```bash
git clone https://github.com/your-username/stm32-temp-monitor.git
cd stm32-temp-monitor
```

### 2 — Open in VS Code

```
File → Open Folder → select the project root
```

PlatformIO will automatically detect `platformio.ini` and download the STM32 toolchain on first open.

### 3 — Configure `platformio.ini`

```ini
[env:nucleo_l432kc]
platform  = ststm32
board     = nucleo_l432kc
framework = cmsis

### 4 — Build & Flash

Use the PlatformIO toolbar at the bottom of VS Code:

| Button | Action |
|---|---|
| Build | Compile the project |
| Upload | Flash via ST-Link |
| Serial Monitor | Open at 9600 baud |

---

## Display Layout

```

Temp Monitor ← static header (white)
Temp: 24.37 C  ← live reading  (cyan)
Thresh: 30.00 C ← static label  (yellow)
Normal ← status        (green / red)

```

Once the temperature hits **THRESHOLD TEMPERATURE**, the status line switches to `THRESH BREACHED` (red) and stays latched — the alert never resets until you press RESET, even if the board cools down.

---

## Serial Output

```
=== STM32L432KC Temperature Monitor ===
-------------------------------------------
[   1] Temp: 24.13 C      (normal)
[   2] Temp: 24.15 C      (normal)
...
[  47] Temp: 30.02 C  <<< THRESHOLD! [LED+BUZZER]

---

## How the Temperature Reading Works

The STM32L432 stores factory calibration samples in flash:

- **TS_CAL1** — raw ADC reading at 30 °C with VDDA = 3.0 V
- **TS_CAL2** — raw ADC reading at 130 °C with VDDA = 3.0 V
- **VREFINT_CAL** — raw VREFINT reading at VDDA = 3.0 V

At runtime, VREFINT is sampled alongside the temperature sensor and used to correct for any drift in the actual supply voltage:

```
corrected = raw_ts × (VREFINT_CAL / raw_vref)

temperature = (corrected − TS_CAL1) × (100 / (TS_CAL2 − TS_CAL1)) + 30
```

This gives a reasonably accurate reading across the full supply-voltage range without needing an external reference.

---

## Buzzer & Alert Logic

- TIM2 runs continuously with `PSC = 79`, `ARR = 332` → **~3 kHz** tick
- `buzzer_on()` sets OC1M to PWM Mode 1 — PA0 oscillates audibly
- `buzzer_off()` forces OC1M inactive — PA0 held LOW, timer keeps running
- LED (PB3) and buzzer are toggled together in 250 ms on / 250 ms off cycles whenever `alert_active` is set

---

## Shutdown (Button)

Hold **PB4 LOW** (active-low with internal pull-up). After a 5 ms debounce:

1. Buzzer and LED are switched off
2. LCD shows `STOPPED / Press RESET`
3. TIM2 and ADC are disabled
4. A final log line is printed over UART
5. The firmware spins in an infinite loop — press the **RESET** button on the Nucleo to restart

---

## 📚 Libraries & References

- [Bresenham's Line Algorithm](https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm) — used in `drawLine()`
- [Midpoint Circle Algorithm](https://en.wikipedia.org/wiki/Midpoint_circle_algorithm) — used in `drawCircle()` / `fillCircle()`
- Font data from Pascal Stang's `font5x7` (Atmel AVR Graphic LCD library, 2001)

---
