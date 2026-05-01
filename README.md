# carcluster

ESP32-S3 + 7" 800×480 RGB TFT panel için instrument cluster — araç gösterge paneli simülatörü.

> Donanım: **Waveshare ESP32-S3-Touch-LCD-7 V1.2**
> Yazılım: **ESP-IDF v5.3.2** + **LVGL v8.4**

## Özellikler

- ISO 2575 / ISO 26262 stilinde flat tasarım gösterge paneli
- Sol kadran: Tachometer (RPM × 1000), redline 7-9
- Sağ kadran: Speedometer (0–240 km/h)
- 13 adet otomotiv telltale ikonu (sinyal, far, ABS, engine, oil, coolant, battery, brake, airbag, seatbelt, fuel low, vb.)
- Yakıt + soğutma suyu bar göstergesi
- Vites göstergesi (P/R/N/D/1-6)
- ODO sayacı
- VSYNC senkronize phase-locked task pipeline
- Tearing-free render (num_fbs=2 + bounce buffer + LVGL partial mode)
- İçsel sürüş demo'su (60 fps state üretimi keyframe profile veya sweep)
- R-FPS (render rate) ve DR-FPS (display rate) canlı sayaçları

## Mimari

```
                Donanım                          Yazılım
        ┌────────────────────┐         ┌──────────────────────────┐
        │ ST7262 RGB Panel   │         │ Core 0:                  │
        │ 800×480 @ 28-31 Hz │ ◄────── │   demo_task    (VSYNC)   │
        │ DMA + bounce ISR   │         │   ui_refresh   (VSYNC)   │
        └────────────────────┘         │                          │
                ▲                      │ Core 1:                  │
                │ VSYNC IRQ            │   lvgl_task    (5ms)     │
                │ (faz kilit)          │     └─ render → flush_cb │
                └──────────────────────┘                          │
        ┌────────────────────┐         │ Donanım: LCD ISR + DMA   │
        │ CH422G I/O exp.    │ ◄──────                           │
        │ I2C: backlight,    │                                    │
        │ LCD reset, VDD     │                                    │
        └────────────────────┘                                    │
                                       └──────────────────────────┘
```

## Kullanılan Pinler (Waveshare ESP32-S3-Touch-LCD-7)

| İşlev | GPIO |
|---|---|
| HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| RGB565 16-bit veri | 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 |
| I²C SCL / SDA (CH422G + GT911) | 9 / 8 |

CH422G EXIO bağlantıları: bit2=BL_EN, bit3=LCD_RST, bit6=LCD_VDD_EN

## Build & Flash

```bash
. ~/esp/esp-idf/export.sh
cd carcluster
LC_ALL=C idf.py set-target esp32s3
LC_ALL=C idf.py build
LC_ALL=C idf.py -p /dev/ttyACM0 flash monitor
```

> **Türkçe locale notu**: ESP-IDF'in Xtensa toolchain'i Türkçe locale'de `wsr.intclear` opcode lookup'ında hata verir. `LC_ALL=C` her komut başında zorunlu.

## Konfigürasyon Özeti

```
PCLK              12 MHz
Çözünürlük         800×480 RGB565
Refresh            ~28-31 Hz
Frame buffer       2 × (768 KB) Octal PSRAM
LVGL buffer        2 × (128 KB) PSRAM, partial mode
Bounce buffer      10 satır internal SRAM (16 KB)
Anim duration      35 ms ease-out
Tasks              demo + ui_refresh (VSYNC notify) + lvgl (5 ms sabit)
PSRAM              Octal 80 MHz
CPU                240 MHz dual core
```

## Demo Profile

İçsel sürüş senaryosu (`main/demo.c`):
1. **0 → 240 km/h** lineer ivme, 8.4 sn
2. **Cruise** 240 km/h, 1.75 sn
3. **240 → 0 km/h** lineer fren, 8.4 sn
4. **Park**, 1.4 sn
5. Sonsuz döngü

## Klasör Yapısı

```
carcluster/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml      # LVGL ~8.4.0 dependency
│   ├── main.c                 # boot, init, task spawn
│   ├── board.c/h              # CH422G + RGB panel init
│   ├── lvgl_port.c/h          # LVGL ↔ esp_lcd entegrasyonu, VSYNC ISR
│   ├── ui.c/h                 # Cluster UI (lv_meter, label, bar)
│   ├── icons.c/h              # 13 telltale ikonu (lv_obj primitives)
│   ├── state.h                # Paylaşımlı state struct + mutex
│   └── demo.c/h               # İçsel sürüş demo task
└── README.md
```

## Dökümanlar

- [doc/](../doc/) — tüm donanım/yazılım dokümantasyonu (datasheet, şematik, LVGL, ESP-IDF guide'ları)

## Lisans

MIT
