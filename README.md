# carcluster

ESP32-S3 + 7" 800×480 RGB TFT panel için **profesyonel araç gösterge paneli (instrument cluster)** — animasyonlu telltale'ler, smooth gösterge ibreleri, trip computer, NVS-persisted odometer, GT911 dokunmatik ayar paneli.

> **Donanım**: Waveshare ESP32-S3-Touch-LCD-7 V1.2  
> **Yazılım**: ESP-IDF v5.3.2 + LVGL v8.4  
> **Performans**: R-FPS ~200, DR-FPS 38 (panel-limited), tear-free, ~626 KB PSRAM snapshot cache  
> **Mevcut sürüm**: **v0.8.1** ([releases](https://github.com/haliskilic/carcluster/releases))

---

## İçindekiler

1. [Özellikler](#özellikler)
2. [Donanım](#donanım)
3. [Hızlı başlangıç](#hızlı-başlangıç)
4. [Detaylı kurulum](#detaylı-kurulum)
5. [Mimari](#mimari)
6. [Modüller](#modüller)
7. [Touch UI kullanımı](#touch-ui-kullanımı)
8. [Konfigürasyon](#konfigürasyon)
9. [Bilinen sorunlar / workaround'lar](#bilinen-sorunlar--workaroundlar)
10. [Sürüm geçmişi](#sürüm-geçmişi)
11. [Yol haritası](#yol-haritası)
12. [Lisans](#lisans)

---

## Özellikler

### Görsel
- **Sol kadran**: Tachometer (RPM × 1000), 5 renkli band gradient (yeşil → lime → sarı → turuncu → kırmızı), needle smooth scale (91 pozisyon, 1500 RPM = 15 internal)
- **Sağ kadran**: Speedometer (0–240 km/h), tek-renk arc fill (cyan accent), needle direkt değer
- **Custom tick label'ları**: 23 toplam (10 RPM + 13 speed), needle taradığı sayılar dim → bright reveal efekti
- **Static layer snapshot cache** (A1): scale ticks + backplate + RPM color bands bir kez render → `lv_img` blit (her frame rasterize maliyeti SIFIR)
- **13 ISO 2575 telltale ikonu**: sinyal (sol/sağ), far (uzun/kısa), brake, ABS, airbag, seatbelt, engine MIL, battery, oil, coolant, fuel low
- **Telltale animasyon modları**: OFF / ON / **BLINK** (2 Hz sinyal) / **PULSE** (1 Hz fade kritik uyarılar)
- **Boot bulb-check**: ilk 1.2 sn tüm telltale'ler ON + needle sweep (gerçek araç hissi)
- **Trip computer paneli**: TRIP km, TIME, anlık L/100km tüketim, RANGE — 1 Hz integrator, sentetik tüketim modeli
- **Yakıt + soğutma suyu bar'ları** + dinamik renkli (low fuel = kırmızı, normal = yeşil)
- **Vites göstergesi**: P/R/N/D/1-6 (vites'e göre renk: P/N mavi, R kırmızı, D/sayı yeşil)
- **ODO** (uint32_t, NVS persisted, autosave 30 sn'de bir)
- **R-FPS / DR-FPS / Core0% / Core1% canlı sayaçları**
- **ISO 2575 / OEM-style palet**: Audi-style near-black bg, BMW warm amber, cool-white primary

### Etkileşim
- **GT911 capacitive touch**: I2C polling 50 Hz, LVGL pointer indev
- **Settings modal**: ekrana long-press (~400 ms) → "Reset Trip" + "Close"

### Performans
- **VSYNC-driven phase-locked task pipeline** (demo + ui_refresh + lvgl_task)
- **Tearing-free**: num_fbs=2 + bounce buffer + bb_invalidate_cache + same-core RGB ISR
- **PCLK 16 MHz** community-proven config (Waveshare/ESPHome) — DR-FPS ~38 Hz
- **Snapshot cache**: lv_meter rasterize maliyeti tek seferlik (LVGL #3328 issue)
- **Per-core CPU%**: FreeRTOS idle hook, auto-calibrating baseline

### Güvenilirlik
- Critical NULL/race fix paketi (mutex/malloc creation checks, abort on failure)
- `total_km` uint32_t (silent overflow yok), `trip_m` saturated
- CH422G I2C write mutex-protected
- NVS error logging
- VSYNC ISR IRAM_SAFE

---

## Donanım

> **Detaylı versiyon ve gereksinim listesi**: [REQUIREMENTS.md](REQUIREMENTS.md) — geliştirme ortamı (tested with) + minimum gereksinimler ayrı ayrı.

### Gerekenler
- **Waveshare ESP32-S3-Touch-LCD-7 V1.2** ([Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)) — 7" 800×480 RGB TFT, ST7262 sürücü, GT911 dokunmatik, CH422G I/O expander, ESP32-S3-WROOM-1 N16R8 (16MB flash + 8MB octal PSRAM @ 80 MHz)
- USB Type-C kablo (data destekli — bazı charge-only kablolar çalışmaz)
- **Önerilen**: 5V/2A USB güç adaptörü (USB-A → USB-C), 470 µF bulk cap brownout için

### Pin haritası

| İşlev | GPIO |
|---|---|
| RGB HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| RGB565 16-bit veri | 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 |
| I²C SCL / SDA (CH422G + GT911) | 9 / 8 |
| GT911 INT | 4 (OUTPUT LOW olarak kalır — bkz [bilinen sorunlar](#bilinen-sorunlar--workaroundlar)) |

**CH422G EXIO bit haritası**:
- EXIO1 = TP_RST (touch panel reset)
- EXIO2 = BL_EN (backlight enable, sadece on/off)
- EXIO3 = LCD_RST
- EXIO6 = LCD_VDD enable

---

## Hızlı başlangıç

ESP-IDF kuruluysa ve toolchain hazırsa:

```bash
git clone https://github.com/haliskilic/carcluster.git
cd carcluster
. ~/esp/esp-idf/export.sh
LC_ALL=C idf.py -p /dev/ttyACM0 flash monitor
```

> **Türkçe locale notu**: `LC_ALL=C` her komut başında **zorunlu**. Yoksa Xtensa toolchain `wsr.intclear` opcode lookup'ında çuvallıyor.

---

## Detaylı kurulum

### 1. ESP-IDF kurulumu (Linux/macOS)

```bash
# Bağımlılıklar (Debian/Ubuntu)
sudo apt-get install git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
    dfu-util libusb-1.0-0

# ESP-IDF v5.3.2 (proje bu sürümle test edildi)
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3

# Her terminal session'da export
. ~/esp/esp-idf/export.sh
```

Doğrulama:
```bash
idf.py --version    # ESP-IDF v5.3.2
xtensa-esp32s3-elf-gcc --version
```

### 2. Repo'yu klonla

```bash
cd ~/projects   # veya istediğin yer
git clone https://github.com/haliskilic/carcluster.git
cd carcluster
```

### 3. Hedef chip ayarı

Sadece ilk seferde gerekli:
```bash
LC_ALL=C idf.py set-target esp32s3
```

> **Neden `LC_ALL=C`?** ESP-IDF'in Xtensa toolchain'inin Turkish locale'de Unicode dotted/undotted I (`İ`/`ı`) yüzünden opcode tablo lookup'ında crash ettiği bilinen bir bug var. C locale İngilizce ASCII karşılaştırma yapar — fix bu.

### 4. Build

```bash
LC_ALL=C idf.py build
```

İlk build ~3-5 dk sürer (LVGL component download + toolchain compile). Sonraki incremental build'ler ~10-30 sn.

Çıktı: `build/carcluster.bin` (~700 KB)

### 5. Flash + monitor

USB-C ile bağla, port'u tespit et:
```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
# Tipik: /dev/ttyACM0
```

Flash:
```bash
LC_ALL=C idf.py -p /dev/ttyACM0 flash
```

Serial monitor (boot loglarını görmek için):
```bash
LC_ALL=C idf.py -p /dev/ttyACM0 monitor
# Çıkış: Ctrl-]
```

Birlikte:
```bash
LC_ALL=C idf.py -p /dev/ttyACM0 flash monitor
```

### 6. Doğrulama

Boot loglarında görmek istediklerin:
```
I (xxx) carcluster: === carcluster boot (VSYNC-driven, phase-locked) ===
I (xxx) board: CH422G + LCD power + backlight OK
I (xxx) board: RGB panel ready
I (xxx) touch: GT911 ready, ID: 911<NUL> (39 31 31 00)
I (xxx) lvgl_port: LVGL ready (VSYNC notify, bloksuz flush)
I (xxx) persist: loaded total_km=12345
I (xxx) carcluster: Ready.
```

Ekranda görmen gereken:
1. **Açılış (~1.2 sn)**: 13 telltale tümü yanar (bulb-check) + needle 0→max→0 sweep
2. **Cluster**: sol RPM dial gradient, sağ speed dial cyan arc fill, ortada vites kutusu + trip computer paneli, alt'ta fuel/coolant bars + ODO + R-FPS counter
3. **Demo cycle**: 0→240→0 km/h tekrar, sol/sağ sinyal blink, park'ta brake pulse + dörtlü flaşör
4. **Touch**: ekrana 400 ms basılı tut → settings modal (Reset Trip / Close)

### Sorun giderme

**"Permission denied" `/dev/ttyACM0`'a erişirken** (Linux):
```bash
sudo usermod -aG dialout $USER
# logout/login sonra tekrar dene
```

**Build "incompatible language" hatası**:
- `LC_ALL=C` unutulmuş. Komutun başına ekle.

**Flash "no port" hatası**:
- USB kablo data destekli mi? Charge-only kablolar görünmez.
- Boot mode: BOOT button basılı tut + RST'ye bas (hardware reset). Bazı board'larda gerekli.

**Ekran karanlık / artifakt**:
- Backlight EXIO2 etkin mi? (`board_init` log'unda "backlight OK" bekleniyor)
- USB güç yetersiz olabilir (~600 mA çekiyor) — 2A adaptör dene
- Kart hard reset (`idf.py -p /dev/ttyACM0 erase_flash` sonra flash)

---

## Mimari

```
   Donanım                                     Yazılım (ESP32-S3 dual core)
┌────────────────────┐                ┌──────────────────────────────────┐
│ ST7262 RGB Panel   │                │ Core 0 (PRO):                    │
│ 800×480 @ 38 Hz    │ ◄── DMA ──     │   demo_task    (VSYNC notify)    │
│ PCLK 16 MHz        │                │   ui_refresh   (VSYNC notify)    │
│                    │                │   trip_task    (1 Hz integrator) │
│   ▲ VSYNC IRQ      │                │   persist      (autosave 30s)    │
│   │ (faz kilit +   │                │   touch        (50 Hz I2C poll)  │
│   │  same-core)    │                │   cpu_meter    (500 ms sampler)  │
└───┼────────────────┘                │                                  │
    │                                 │ Core 1 (APP):                    │
    │      ┌─────────────────────┐    │   lvgl_task    (5 ms periodic)   │
    │      │ Bounce buffer (SRAM)│ ◄──┤     └─ render → flush_cb         │
    │      │ 10 lines × 800×2 px │    │   board_init   (RGB ISR registr.)│
    │      └─────────────────────┘    │                                  │
    │                                 └──────────────────────────────────┘
    │      ┌─────────────────────┐                  ▲
    │      │ FB0/FB1 in PSRAM    │                  │ lvgl_port_lock
    └──────┤ 2 × 768 KB          │ ◄── flush ───────┘ (mutex)
           └─────────────────────┘
                  ▲
                  │ static snapshot
                  │ (313 KB × 2 in PSRAM)
           ┌──────┴──────────────┐
           │ A1 cache: scale +   │
           │ backplate + bands   │
           │ → lv_img blit       │
           └─────────────────────┘

┌────────────────────┐
│ CH422G I/O exp.    │  EXIO1: TP_RST   EXIO3: LCD_RST
│ I²C addr 0x24      │  EXIO2: BL_EN    EXIO6: LCD_VDD
└────────────────────┘

┌────────────────────┐
│ GT911 cap. touch   │  I²C addr 0x5D (INT pin'i reset'te low → addr select)
│ INT: GPIO4         │  Polling-based (LVGL indev)
└────────────────────┘
```

**Phase-locked pipeline**: VSYNC ISR `demo_task`, `ui_refresh` ve LVGL'i aynı 26 ms slot'unda uyandırır. Her frame deterministik — drift yok, tearing yok.

**A1 snapshot cache**: `lv_meter`'ın scale + tick + band rasterize'ı her frame'de yapılmıyor; bir kez `lv_snapshot_take_to_buf` ile PSRAM buffer'a render → orijinal meter widget silinir → `lv_img` olarak ekrana yapıştırılır. Sadece dinamik katman (needle + arc fill) live çiziliyor.

**A3 same-core + bounce + bb_invalidate_cache**: Espressif resmi anti-tearing pattern'i — RGB DMA ISR ile lv_timer_handler aynı core'da, bounce buffer cache invalidate flag'i ile DMA fresh PSRAM okuyor.

---

## Modüller

```
main/
├── main.c          // app_main, state init, NVS load, board_init (core 1), task spawn
├── board.c/h       // CH422G I2C + LCD reset/power + RGB panel config
├── lvgl_port.c/h   // LVGL init, VSYNC ISR, flush_cb, indev register
├── ui.c/h          // Cluster UI (snapshot cache, custom labels, settings modal)
├── icons.c/h       // 13 ISO telltale icons + animation modes (OFF/ON/BLINK/PULSE)
├── demo.c/h        // VSYNC-driven test scenario (accel → cruise → brake → park)
├── trip.c/h        // Trip computer 1 Hz integrator (sentetik fuel model)
├── persist.c/h     // NVS odometer load/save
├── touch.c/h       // GT911 driver + 50 Hz polling task
├── cpu_meter.c/h   // Per-core idle hook + auto-calibrating sampler
├── state.h         // Shared cluster_state_t + mutex + global handles
├── CMakeLists.txt
└── idf_component.yml   // LVGL ~8.4.0 dependency
```

### Modül işlevleri

| Modül | Sorumluluk |
|---|---|
| `main` | Boot, init sırası, task spawn |
| `board` | I2C + CH422G + LCD power-on + RGB panel ESP_LCD config |
| `lvgl_port` | LVGL init, VSYNC ISR, flush_cb (panel'e render), indev register |
| `ui` | Cluster widget'ları, snapshot cache orchestration, settings modal |
| `icons` | Telltale çizimi, animation timer (50 ms tick, sync'd OFF/ON/BLINK/PULSE) |
| `demo` | Sentetik sürüş profili (state generator) |
| `trip` | Mesafe + süre + tüketim + range hesaplama |
| `persist` | NVS read/write odometer |
| `touch` | GT911 init + 50 Hz I2C poll → shared state snapshot |
| `cpu_meter` | FreeRTOS idle hook → per-core utilization % |
| `state` | Paylaşımlı `cluster_state_t` (speed, rpm, fuel, telltale flags, trip), mutex |

---

## Touch UI kullanımı

| Eylem | Sonuç |
|---|---|
| **Long-press** (~400 ms, herhangi bir yer) | Settings modal açılır |
| Tap **"Reset Trip"** | Trip distance/time/fuel sıfırlanır, modal kapanır |
| Tap **"Pause Demo"** | Demo state üretimi durur + trip integrator durur (TIME/RANGE donar). Tekrar tıkla → "Resume Demo" |
| Tap **"Close"** | Modal kapanır, hiçbir şey değişmez |

Modal opaque overlay (`#05080F` near-black) + ortalanmış 460×380 panel. Footer'da:
- 1. satır: `carcluster <version> • <build date>` (esp_app_get_description'tan canlı)
- 2. satır: `boots N • panic X • wdt Y • brownout Z` (NVS counter, panic|wdt > 0 ise **amber** renk)

**Render notu** (v0.7.5'te eklendi): Modal `LV_OBJ_FLAG_HIDDEN` ile yaratılır, tüm child hierarchy build edilir, sonra TEK `lv_obj_clear_flag` çağrısıyla görünür olur. Bu sayede LVGL partial-flush rendering'de (BUF_LINES=160 → 3 dilim/frame) modal içeriği koherent olarak render edilir, "perde gibi yukarıdan aşağı" effect yok.

**Idle sleep** (v0.7.8): Demo paused + **30 sn dokunma yok** → backlight off (CH422G EXIO2). Herhangi bir touch event idle_mark_activity tetikler → backlight back on. Demo running iken activity her zaman fresh sayılır, idle hiç tetiklenmez. Threshold prod için 5 dk olabilir, demo amaçlı 30 sn (idle.c IDLE_THRESHOLD_MS).

---

## Konfigürasyon

### sdkconfig.defaults öne çıkanlar

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Octal PSRAM 80 MHz
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y

# CPU + FreeRTOS
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000

# PERF Bundle (Stage 1 + cluster gerekleri)
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y
CONFIG_LV_MEMCPY_MEMSET_STD=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y

# LVGL fonts (Montserrat 14, 18, 24, 36, 48)
CONFIG_LV_FONT_MONTSERRAT_*=y
CONFIG_LV_MEM_SIZE_KILOBYTES=128
```

### Runtime parametreler

```
PCLK              16 MHz (Waveshare/ESPHome community-proven)
Çözünürlük         800×480 RGB565
Refresh            ~38 Hz panel scan
Frame buffer       2 × 768 KB Octal PSRAM (num_fbs=2)
LVGL buffer        2 × 256 KB PSRAM (BUF_LINES=160, partial mode)
Bounce buffer      10 satır internal SRAM (~16 KB) + bb_invalidate_cache=1
Snapshot cache     2 × 313 KB PSRAM (RGBA, lv_img source)
Anim/timing        VSYNC notify-driven, faz kilit
PSRAM              Octal 80 MHz (2.3-3 MB kullanım)
CPU                240 MHz dual core, RGB ISR + lvgl_task aynı core (1)
```

---

## Bilinen sorunlar / workaround'lar

### GPIO4 (GT911 INT) INPUT modunda display kararıyor — V1.2

**Semptom**: `touch_init` içinde GPIO4'ü `GPIO_MODE_INPUT`'a çevirmek panel'i tamamen karartıyor.

**Schematic analizi sonrası gerçek sebep** (Waveshare V1.2 schematic'i incelendi): GPIO4 SADECE GT911 INT'e bağlı, başka hiçbir şeye değil. Black-screen sebebi büyük olasılıkla:
- **FPC ground bounce / EMI coupling**: GT911 high-Z INT pin'inde scan switching aktivitesi, J1 (LCD FPC) ile J2 (touch FPC) arasında paylaşılan ground return üzerinden RGB data lines'a coupling yapıyor
- **Eksik RC filter**: GT911 programming guide section 7.5 INT pin için **680Ω series + 1nF cap to GND** öneriyor; V1.2 board'unda bu yok

**Yazılım workaround (current)**: GPIO4 OUTPUT LOW olarak kalır. Polling-based touch read için INT pin gerekmez. **Uyarı**: Bu yapı GT911'i scan moduna geçirmediği için touch sample rate'i düşük olabilir.

**Donanım modu (önerilen)**: GT911 INT (GPIO4) ile GND arasına **1nF cap**, GPIO4 ile GT911 INT pin arasına **680Ω series resistor**. Bu RC filter EMI coupling'i bastırıyor → GPIO4'ü datasheet-uyumlu INPUT high-Z'ye çevirebilir, GT911 normal scan moduna geçer.

**Datasheet uyumlu sequence** (yapılmadı — donanım modu olmadan riskli):
```
RST low (≥10ms) → INT output low → RST high → wait 5ms (INT still low) →
INT to high-Z (input floating) → wait 50ms → first I2C read
```

### Türkçe locale Xtensa toolchain crash

**Semptom**: `idf.py build` Turkish locale'de "incompatible language" benzeri hata veriyor.

**Sebep**: Xtensa-esp32s3-elf binutils opcode tablo lookup'ında dotted/undotted I (`İ`/`ı`) Unicode farkı.

**Workaround**: Her `idf.py` komutuna `LC_ALL=C` prefix.

### A2 (direct mode rendering) deferred

**Denenen 3 yaklaşım**:
1. `direct_mode=1` + 2 panel FB + manual carry-over → bandwidth overload (R-FPS 20)
2. `full_refresh=1` + 2 panel FB → render too slow (R-FPS 8)
3. `direct_mode=1` + 1 panel FB + VSYNC sync → tearing visible

**Sebep**: ESP-IDF + LVGL anti-tearing pattern'i `esp-bsp/esp_lvgl_port` seviyesinde driver entegrasyonu gerektiriyor (~200 satırlık state machine, VSYNC + dirty area + buffer alternation koordinasyonu).

**Karar**: Permanent deferred. Mevcut v0.7.0 zaten 38 Hz panel-limited + R-FPS ~200 + tear-free.

### Brownout (yüksek current draw)

**Semptom**: Demo'da yüksek RPM + tüm telltale ON sırasında reset.

**Workaround**: 5V/2A USB adaptör + 470 µF bulk cap (Waveshare 4.3" sibling için belgelenen fix, V1.2'de de geçerli).

### LCD power-on sequence — V1.2 datasheet-uyumsuz sıra GEREKLİ

**Bulgular** (v0.7.2'de denendi, v0.7.4'te geri alındı): ST7262 datasheet sayfa 90 BL_EN'in `DCLK output + 250ms` sonrası verilmesini öneriyor. Datasheet-uyumlu sıra (BL_EN panel init sonrası) **V1.2'de display'i karartıyor**.

**V1.2 için çalışan sıra** (orijinal Waveshare örneklerinden ve bizim test'lerden):
```
1. board_ch422g_write(VDD|RST, 0)  delay 20 ms
2. board_ch422g_write(0, RST)       delay 20 ms
3. board_ch422g_write(RST, 0)        delay 120 ms
4. board_ch422g_write(BL_EN, 0)     ← BL_EN, panel init'ten ÖNCE
5. esp_lcd_new_rgb_panel(&cfg, &handle)
6. esp_lcd_panel_init(handle)
```

**Pratik > teori**: V1.2 board'unda gizli init-sequence bağımlılıkları var, datasheet ihlali olsa bile bu sıra ile düzgün çalışıyor.

### Önerilen donanım modifikasyonları (V1.2 board)

Schematic incelemesi sonrası bulunan eksiklikler:

| Mod | Sebep | Etki |
|---|---|---|
| **GT911 INT RC filter**: 680Ω series + 1nF cap (GT911 INT ↔ GPIO4 arası) | Programming guide §7.5; V1.2'de eksik | EMI coupling bastırılır, GPIO4 INPUT high-Z mode'a güvenle geçilebilir |
| **3V3 bulk cap**: 22-47 µF tantal/seramik U8 yakınında | Wi-Fi peak transient (500mA) için module'ün 10µF + 100nF yeterli değil | Brownout riski azalır |
| **AVDD_LCD bulk**: +10 µF/25V seramik J1-AVDD'de | ST7262 80mA çekimi için marjinal | Yüksek refresh rate'de ripple azalır |
| **Backlight bulk**: +22 µF/35V VLED+ rail'da | Backlight current 580-1000mA olabilir | Backlight ripple/flicker azalır |
| **5V brownout supervisor** (MAX809/TPS3839) | Üst seviye brownout protection yok | Battery operation güvenli olur |

---

## Sürüm geçmişi

| Version | Vurgular |
|---|---|
| **v0.1** (initial) | Cluster baseline, VSYNC pipeline, 13 telltale ikon, demo |
| **v0.2.0** | Telltale animasyonu (BLINK/PULSE), boot splash + needle sweep, NVS persistence, trip computer |
| **v0.3.0** | Per-core CPU meter, BUF_LINES bump, layout tweaks, boot splash kaldırıldı |
| **v0.4.0** | Critical NULL/race fix paketi, boot sweep + bulb-check, ISO 2575 polish |
| **v0.5.0** | A3: Same-core RGB ISR + bounce buffer + bb_invalidate_cache + PCLK 16 MHz (DR-FPS 30→38) |
| **v0.6.0** | A1: lv_meter snapshot cache (PSRAM-backed, R-FPS 130→200) |
| **v0.7.0** | C6: GT911 touch UI + settings modal (Reset Trip) |
| **v0.7.1** | REQUIREMENTS.md + ODO label position fix |
| **v0.7.2** | Paket A: critical fixes (demo total_km mm-accumulator, dsc_pool guard) |
| **v0.7.3** | Paket D: Inter Display font (tabular figures) + needle damping (150ms ease-out) + settings modal expansion (Pause Demo + version footer) |
| **v0.7.4** | Display fix: V1.2 BL_EN-after-panel sequence reverted (datasheet ihlali ama V1.2 zorunluluğu) |
| **v0.7.5** | Trip pause sync (Pause Demo TIME/RANGE da donar) + modal HIDDEN-pattern (perde efekti yok, single-shot render) + PROJECT_VER UI footer |
| **v0.7.6** | Paket B: TWDT panic + critical task subscribe (lvgl/demo/ui_refresh/trip self-pet, hung task → reset). Reset reason logging + NVS counter (fault tracking). 10s WDT timeout. |
| **v0.7.7** | E1: Trip persist across reboot — NVS blob (trip_m + trip_seconds + trip_fuel_ml). Boot'ta yüklenir, her 30 trip-saniyede save (pause'da save yok). Reset Trip butonu NVS'i de temizler. |
| **v0.7.8** | C8 yazılım: idle sleep (demo paused + 30s no-touch → backlight off, touch wake) + Settings "About" reset count breakdown (boots/panic/wdt/brownout, panic|wdt > 0 ise amber renk uyarı) |
| **v0.7.9** | B1: Theme system — 4 preset palette (Blue/Orange/Yellow/Red), theme.c globals + ui.c extern, settings'te picker, NVS u8 persist, reboot-required apply |
| **v0.8.0** | B2-B3-B4-B7-B8: Tabbed settings modal (lv_tabview, 4 sekme). **B2 Units** (Metric/Imperial, NVS, reboot apply). **B3 Trip B** (bağımsız ikinci sayaç, paralel tick, NVS). **B7 Limits** (RPM redline 5000-9000 + coolant warn 90-120°C sliders). **B8 Stats** (max speed/longest trip/total fuel/runtime, NVS). **B4 Diag** (heap, PSRAM, uptime, reset counts, Trip B + lifetime stats görünümü) |
| **v0.8.1** | I²C bus recovery — soft reset (idf.py flash, esp_restart) sonrası GT911/CH422G önceki transaction'da takılı kalmış olabilir. board.c i2c_init() öncesi NXP UM10204 sequence: SDA released, SCL 9 puls + STOP. Pin'ler önce HIGH set'lenir SONRA OUTPUT_OD config (LOW pulse sızdırma yok). |

```bash
git tag -l    # tüm versiyonlar
git checkout v0.7.0   # belirli sürüm
```

---

## Yol haritası

### Tamamlananlar
- ✅ **A1**: lv_meter snapshot cache (R-FPS 200) — v0.6.0
- ✅ **A3**: Same-core RGB ISR + bounce + bb_invalidate_cache (DR-FPS 38) — v0.5.0
- ✅ **C6**: GT911 touch UI + settings modal — v0.7.0+
- ✅ **D1**: Tabular numerals (Inter Display) — v0.7.3
- ✅ **D2**: Needle damping (150ms ease-out) — v0.7.3

### Sıradaki (yazılım)
- ✅ **B (revize)**: TWDT panic + reset reason + counter — v0.7.6 tamamlandı
- ✅ **E1**: Trip persist across reboot — v0.7.7 tamamlandı
- ✅ **C8 (yazılım)**: Idle sleep — v0.7.8 tamamlandı (demo pause + 30s no-touch → BL off, touch wake)
- ✅ **Settings "About"**: reset count breakdown — v0.7.8 tamamlandı
- ✅ **B1**: Theme system (4 preset) — v0.7.9 tamamlandı
- ✅ **B2**: Unit toggle (Metric/Imperial) — v0.8.0 tamamlandı
- ✅ **B3**: Trip B (bağımsız ikinci sayaç) — v0.8.0 tamamlandı
- ✅ **B4**: Diagnostic ekranı (heap/PSRAM/stats görünümü) — v0.8.0 tamamlandı
- ✅ **B7**: Custom limits (RPM redline + coolant warn) — v0.8.0 tamamlandı
- ✅ **B8**: Lifetime stats (max speed, longest trip, total fuel) — v0.8.0 tamamlandı

**Yazılım-only kalan kısa liste**: B5 splash screen, B6 touch kalibrasyon ekranı, C1 WiFi+OTA, C2 GitHub Actions CI, D1 odometer flip animasyonu, D2 shift hint. Sıradaki büyük iş donanım (CAN-bus / OBD-II).

### Donanım gerektirir
- [ ] **C8 (PWM)**: Backlight dimming — V1.2 stock'ta sadece on/off (CH422G EXIO2). Community GPIO16 ledc claim doğrulanmadı.
- [ ] **C5**: CAN-bus / OBD-II — TWAI driver + SN65HVD230 transceiver
- [ ] **C9**: Audio (buzzer) — piezo + PWM
- [ ] **E6**: DS3231 RTC — absolute time, key-off persistence

### Kalıcı deferred
- ~~A2. Direct mode rendering~~ — 4 farklı yaklaşım denendi, hepsi başarısız (display tearing veya R-FPS düşüşü). esp-bsp seviyesinde driver state machine gerek (~1-2 gün iş, marjinal kazanç).

---

## Lisans

MIT — `LICENSE` dosyası repo'da.

### Atıflar / kaynaklar

- [Waveshare ESP32-S3-Touch-LCD-7 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)
- [ESP-IDF v5.3.2 docs](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/)
- [LVGL v8.4 docs](https://docs.lvgl.io/8.4/)
- [esp-bsp performance.md](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md)
- [ISO 2575:2021 — Symbols for controls, indicators and tell-tales](https://www.iso.org/standard/68409.html)
- ISO 26262 ASIL automotive safety integrity guidelines
- Espressif anti-tearing FAQ ([RGB LCD docs](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html))

Community projeler (PCLK config + GT911 timing):
- [iamfaraz/Waveshare_ST7262_LVGL](https://github.com/iamfaraz/Waveshare_ST7262_LVGL)
- [inytar/waveshare-esp32-s3-touch-lcd-7-esphome](https://github.com/inytar/waveshare-esp32-s3-touch-lcd-7-esphome)
- [bennydiamond/esphome_lvgl_hmi_garage](https://github.com/bennydiamond/esphome_lvgl_hmi_garage)
