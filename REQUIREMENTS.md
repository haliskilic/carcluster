# Requirements & Tested Versions

> **Son sürüm**: v0.7.5 (Mayıs 2026)

İki ayrı bölüm:
- **Geliştirme ortamı** — projenin geliştirildiği ve test edildiği EXACT versiyonlar
- **Minimum gereksinimler** — projenin çalışması için gereken alt sınırlar

---

## 1. Geliştirme ortamı (tested with)

Tüm v0.1 → v0.7.0 sürümleri aşağıdaki kombinasyonla geliştirildi ve test edildi.

### Donanım

| Bileşen | Model / Spec |
|---|---|
| **Geliştirme kartı** | Waveshare ESP32-S3-Touch-LCD-7 **V1.2** |
| **MCU modülü** | ESP32-S3-WROOM-1 **N16R8** (16 MB Quad SPI flash + 8 MB Octal PSRAM) |
| **CPU** | ESP32-S3 dual core Xtensa LX7 @ 240 MHz |
| **PSRAM** | Octal SPI @ 80 MHz, 8 MB |
| **Display** | 7" 800×480 RGB TFT, ST7262 driver IC |
| **Touch** | GT911 capacitive (5-point), I²C |
| **I/O expander** | CH422G (I²C, 8 EXIO) |
| **Power** | USB Type-C (5V/2A), opsiyonel 470 µF bulk cap brownout için |

### Yazılım toolchain

| Bileşen | Versiyon |
|---|---|
| **ESP-IDF** | **v5.3.2** (release) |
| **LVGL** | **v8.4.0** (managed_components, `lvgl/lvgl: ~8.4.0`) |
| **Xtensa GCC** | crosstool-NG esp-13.2.0_20240530 (gcc 13.2.0) |
| **Python** | 3.11.2 (ESP-IDF venv) |
| **CMake** | 3.25.1 |
| **Ninja** | 1.11.1 |
| **Git** | 2.39+ |

### Host işletim sistemi

Geliştirme ve test ortamı:

| Bileşen | Versiyon |
|---|---|
| **OS** | Debian GNU/Linux 6.1.x x86_64 (Bookworm tabanlı) |
| **Locale** | `LC_ALL=C` ZORUNLU (Türkçe locale Xtensa toolchain'i çuvallatıyor) |
| **Shell** | bash 5.x |
| **USB** | USB-C native (Type-C OTG portu, **data destekli kablo**) |

> **Not**: macOS ve Windows'da da çalışması beklenir ama proje üzerinde test edilmedi. WSL2 üzerinde USB pass-through ile flash etmek karmaşık (`usbipd-win` gerekli) — yerel Linux önerilir.

### Sürüm tutarlılığını doğrulama

Build ortamını yeniden oluştururken EXACT eşleşmeyi sağlamak için:

```bash
# ESP-IDF v5.3.2'yi tag ile klon
cd ~/esp
git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git

# Toolchain'i sabit sürüm ile yükle
cd esp-idf
./install.sh esp32s3

# LVGL 8.4.0 zaten dependencies.lock'a sabitlendi:
cat carcluster/dependencies.lock | grep -A1 "lvgl"
```

### dependencies.lock

Repo `dependencies.lock` içeriyor — LVGL ve diğer bağımlılıkların EXACT hash'leri sabitlenmiştir. ESP-IDF Component Manager `idf.py build` sırasında bu lock dosyasını saygıyla okur, aynı versiyonları indirir.

---

## 2. Minimum gereksinimler

Projenin çalışması için kabul edilebilir alt sınırlar. Bu seviyenin altında **denenmedi**, çalışmama riski vardır.

### Donanım minimum

| Bileşen | Minimum | Sebep |
|---|---|---|
| **MCU** | ESP32-S3 (her revizyon) | RGB LCD peripheral S3'te var, S2'de yok |
| **Flash** | **2 MB** | Binary ~700 KB + bootloader + NVS partition + LVGL XIP rodata |
| **PSRAM** | **4 MB Octal SPI**\* | Frame buffers (1.5 MB) + LVGL buffers (512 KB) + snapshot cache (626 KB) |
| **PSRAM hızı** | 80 MHz Octal | 40 MHz QSPI bandwidth yetmez 16 MHz PCLK için |
| **Display** | 800×480 RGB TFT, ST7262 veya benzeri | Başka çözünürlük UI yeniden konumlandırma gerektirir |
| **Touch** | Opsiyonel — GT911 olmazsa touch UI çalışmaz, geri kalanı çalışır | Sadece settings modal etkilenir |
| **Power** | 5V / 1A USB | Backlight + tüm telltale ON ~600-800 mA çekiyor |
| **Cable** | USB Type-C **data destekli** | Charge-only kablolarda port görünmez |

\* **PSRAM kritik**: 4 MB QSPI ile fonksiyonel ama 38 Hz panel scan'da bandwidth contention olabilir. **Önerilen**: 8 MB Octal @ 80 MHz.

### Yazılım minimum

| Bileşen | Minimum | Maximum test edildi |
|---|---|---|
| **ESP-IDF** | **v5.3.0** | v5.3.2 |
| **LVGL** | v8.4.0 | v8.4.x |
| **Xtensa GCC** | gcc 13.x | gcc 13.2.0 |
| **Python** | 3.8 | 3.11 |
| **CMake** | 3.16 | 3.25 |
| **Ninja** | 1.10 | 1.11 |

#### ESP-IDF sürüm uyumluluğu

- **v5.3.0+**: ✓ test edildi (v5.3.2 ile birebir)
- **v5.2.x**: muhtemelen çalışır ama `esp_lcd_rgb_panel` API'sinin bazı flag'leri (örn. `bb_invalidate_cache`) eksik olabilir, mimariyi kontrol et
- **v5.1.x ve öncesi**: ÖNERİLMEZ — bounce buffer + RESTART_IN_VSYNC kombinasyonu kararsızdı bu sürümlerde
- **v5.4+**: TEST EDİLMEDİ — API değişiklikleri olabilir, özellikle indev/disp drv yapıları LVGL v9'a yaklaştığında

#### LVGL sürüm uyumluluğu

- **v8.4.x**: ✓ test edildi
- **v8.3.x**: muhtemelen çalışır ama `lv_snapshot_take_to_buf` API mevcut olmalı
- **v8.0-8.2**: BAZI API'lar farklı, refactor gerekir
- **v9.x**: ÇALIŞMAZ — büyük API breaking changes (lv_disp_t → lv_display_t, lv_obj_set_style_* signature değişti, indev API farklı)

### İşletim sistemi minimum

| OS | Durum |
|---|---|
| **Linux x86_64** | ✓ test edildi (Debian Bookworm), önerilen |
| **Linux ARM64** | muhtemelen çalışır (ESP-IDF destekliyor), denenmedi |
| **macOS** (Intel + Apple Silicon) | muhtemelen çalışır (ESP-IDF destekliyor), denenmedi |
| **Windows native** | muhtemelen çalışır (ESP-IDF destekliyor), `LC_ALL=C` muadili `chcp 65001` veya PowerShell environment değişkeni |
| **WSL2** | flash için `usbipd-win` ile USB pass-through kurulumu gerekli (karmaşık) |

### Disk + RAM (host)

- **Disk**: ~3-5 GB (ESP-IDF + toolchain + LVGL component cache + build artifacts)
- **RAM**: 2 GB minimum, 4 GB önerilen (LVGL build sırasında parallel ninja + GCC bellek tüketimi)

---

## 3. Doğrulama checklist'i

Yeni bir ortamda kuruyorsanız, kuruluşun beklenen sürümlere uyduğunu doğrulayın:

```bash
# ESP-IDF
. ~/esp/esp-idf/export.sh
idf.py --version
# Beklenen: ESP-IDF v5.3.2 (veya v5.3.x)

# Toolchain
xtensa-esp32s3-elf-gcc --version | head -1
# Beklenen: xtensa-esp-elf-gcc (crosstool-NG esp-13.2.0_20240530) 13.2.0

# Python (ESP-IDF venv aktivasyonu sonrası)
python --version
# Beklenen: Python 3.11.x veya 3.10.x veya 3.8+

# CMake + Ninja
cmake --version | head -1
ninja --version
# Beklenen: cmake 3.16+, ninja 1.10+

# Git (bağımlılıklar için)
git --version
# Beklenen: git 2.30+

# Locale (kritik)
echo $LANG
# Türkçe locale ise her komuta LC_ALL=C ekle

# LVGL (proje klonlandıktan sonra)
cd carcluster
LC_ALL=C idf.py reconfigure   # dependencies.lock'tan LVGL indir
ls managed_components/lvgl__lvgl/
# Beklenen: 8.4.0 sürümü
cat managed_components/lvgl__lvgl/idf_component.yml | grep version
# Beklenen: version: 8.4.0
```

---

## 4. Build artifact boyutları (referans)

v0.7.5 build çıktıları:

| Artifact | Boyut |
|---|---|
| `bootloader.bin` | ~23 KB |
| `partition-table.bin` | 3 KB |
| `carcluster.bin` (uygulama) | ~1.2 MB (Inter font 5 boyutu RGBA = ~500 KB ekledi) |
| **Toplam flash kullanımı** | ~1.2 MB / ~2 MB partition (60% kullanılı) |
| **PSRAM çalışma zamanı** | ~3 MB (FBs + snapshots + LVGL bufs + heap) |
| **Internal SRAM** | ~280 KB (stacklar + bounce buffer + LVGL fast_mem IRAM) |
| **IRAM** | ~140 KB (LVGL hot path + ISR'lar + Stage 1 attribute) |

Build sonrası `idf.py size` ile detaylı görebilirsin.

---

## 5. Bilinen sürüm uyumsuzlukları

### ESP-IDF v5.4+
**Risk**: orta. RGB panel API'sı stabil ama indev struct değişebilir.  
**Test edilmedi**: A4 (damped needle) gelecek geliştirme için bu versiyon test edilebilir.

### LVGL v9.x
**Risk**: yüksek. Kompleksite:
- `lv_disp_t` → `lv_display_t` rename
- `lv_obj_set_style_text_color(obj, color, 0)` → `lv_obj_set_style_text_color(obj, color, LV_PART_MAIN)` (selector enum'larında değişiklik)
- `lv_indev_drv_register` API tamamen değişti, `lv_indev_create` lazım
- `lv_meter` deprecate, `lv_scale` ile değiştirilmiş
- `lv_snapshot_take` API farklı

**Migration efforu**: ~1-2 gün full refactor. Şu an gündemde değil.

### Türkçe Linux locale
**Risk**: build'i durduruyor. **Workaround**: her `idf.py` komutuna `LC_ALL=C` prefix.

```bash
# .bashrc'ye ekleyebilirsin:
alias idfp='LC_ALL=C idf.py'
# Sonra: idfp build, idfp -p /dev/ttyACM0 flash, vs.
```

### Windows native
**Risk**: orta. Çalışması beklenir ama:
- `LC_ALL=C` yerine PowerShell'de `$env:LC_ALL = "C"` veya CMD'de `set LC_ALL=C`
- Path separator (`/` vs `\`) genelde sorun değil
- USB serial port format (`COM3` vs `/dev/ttyACM0`)
- Locale işine yarayabilir: `chcp 65001` (UTF-8 console)

---

## 6. Önerilen geliştirme setup'ı

```
Linux Debian Bookworm x86_64
+ ESP-IDF v5.3.2 (sabit tag)
+ VS Code + Espressif IDF extension (optional, debug ergonomics)
+ git for version control
+ alias: alias idfp='LC_ALL=C idf.py'
+ USB Type-C data kablosu, 5V/2A power supply
+ Waveshare ESP32-S3-Touch-LCD-7 V1.2
```

Bu kombinasyon **birebir aynı versiyonlarla** projenin geliştirildiği ortamdır. Sıfır sürpriz.
