# Kavach project structure (for a new standalone repo)

This document describes the **structure of the Kavach program** and the **minimum set of files and folders** you need when creating a new repository that contains only this project.

---

## 1. What the program does (reminder)

- **Voice recognition** (ESP-SR): wake word + commands (help, alert, call family, help, light, etc.).
- **Minimal UI**: one screen with title “Kavach”, status text, and an on-screen “light” (idle / listening / command ok / alert).
- **Voice confirmations**: different WAV per command (OK, Alerted, Calling, Help) from SPIFFS.

---

## 2. External dependencies (outside your repo)

The project **does not** ship these; they must be available at build time:

| Dependency | Purpose |
|------------|--------|
| **ESP-IDF** (v5.1+) | Framework; set `IDF_PATH` and use `idf.py`. |
| **esp-box BSP** | Board support (display, I2S, codec, buttons). Currently: `set(EXTRA_COMPONENT_DIRS ../../components)` in root `CMakeLists.txt` points to `esp-box/components` (the `bsp` component). For a standalone repo you can: (a) add **esp-box** as a git submodule and point `EXTRA_COMPONENT_DIRS` to its `components` folder, or (b) copy the `bsp` component into your repo (e.g. `components/bsp`). |
| **IDF Component Manager** | Pulls: `espressif/esp-sr`, `espressif/led_strip`, `espressif/qrcode`, `espressif/ir_learn`, `espressif/aht20`, `espressif/at581x` (see `main/idf_component.yml`). Fetched automatically when you run `idf.py build`. |

So for a **new repo** you need either the **esp-box** repo (or at least its **components** folder) next to or inside your project so the BSP is found.

---

## 3. Minimal folder and file set (what to put in the new repo)

Below is the **exact** set of files and folders that are **strictly necessary** for the program. Everything else in the current `kavach_demo` (other UI, rmaker, app_led, etc.) is **not** built; it’s leftover from the factory demo and can be omitted in a clean repo.

### 3.1 Recommended directory tree (minimal repo)

```text
kavach/
├── CMakeLists.txt              # Root project; EXTRA_COMPONENT_DIRS to BSP
├── partitions.csv
├── sdkconfig.defaults
├── README.md
├── VOICE_CONFIRMATIONS.md
├── PROJECT_STRUCTURE.md        # This file (optional)
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── main.h                   # Optional; version macros only
│   ├── idf_component.yml
│   ├── settings.c
│   ├── settings.h
│   │
│   ├── app/
│   │   ├── app_sr.c             # ESP-SR + command list
│   │   ├── app_sr.h
│   │   ├── app_sr_handler.c     # Command handling + WAV playback
│   │   ├── app_sr_handler.h
│   │   ├── app_rmaker_stub.c    # No-op RainMaker stub
│   │   ├── app_rmaker_stub.h
│   │   ├── sensor_stub.c
│   │   ├── sensor_stub.h
│   │   ├── mute_stub.c
│   │   └── mute_stub.h
│   │
│   └── gui/
│       ├── ui_kavach.c          # Minimal UI (text + light)
│       ├── ui_kavach.h
│       └── font/
│           └── font_en_24.c     # Only font used by UI
│
└── spiffs/
    ├── echo_en_ok.wav
    ├── echo_en_alerted.wav
    ├── echo_en_calling.wav
    ├── echo_en_help.wav
    ├── echo_cn_ok.wav
    ├── echo_cn_alerted.wav
    ├── echo_cn_calling.wav
    └── echo_cn_help.wav
```

You can drop the other `echo_*_wake.wav` / `echo_*_end.wav` and `spiffs/mp3/` if you don’t need them.

**ESP-SR model partition:** Voice recognition needs a separate **model** partition (wake word + multinet). It is not part of this repo; it is usually built/flashed from the **esp-box** or **esp-sr** flow (e.g. a `model` SPIFFS image). Your `partitions.csv` must include a `model` partition and you must flash it; see esp-box or ESP-SR docs.

### 3.2 What each part is for

| Path | Role |
|------|------|
| **Root** | |
| `CMakeLists.txt` | Top-level project; includes IDF, sets `EXTRA_COMPONENT_DIRS` to BSP (e.g. `../../components` or `components`), defines project name. |
| `partitions.csv` | Partition table (e.g. nvs, ota, storage for SPIFFS, model for SR). |
| `sdkconfig.defaults` | Default Kconfig (target, SPIRAM, SR models, BSP, etc.). |
| **main/** | |
| `main/CMakeLists.txt` | Registers `main` component; lists only the sources above (or uses the same GLOB + FILTER logic so only these files are built). |
| `main/main.c` | App entry: NVS, settings, SPIFFS mount, display, BSP, `kavach_ui_start()`, `app_sr_start()`. |
| `main/main.h` | Optional; version macros. |
| `main/idf_component.yml` | IDF Component Manager deps: esp-sr, led_strip, qrcode, ir_learn, aht20, at581x. |
| `main/settings.c`, `settings.h` | Persistent settings (language, volume, etc.) in NVS. |
| **main/app/** | |
| `app_sr.c`, `app_sr.h` | ESP-SR integration; default command list (Kavach + factory-style); language and multinet. |
| `app_sr_handler.c`, `app_sr_handler.h` | SR result handling; updates UI; plays confirmation WAVs (OK / alerted / calling / help). |
| `app_rmaker_stub.c`, `app_rmaker_stub.h` | Stub implementations so code that used to call RainMaker still links (no cloud). |
| `sensor_stub.c`, `sensor_stub.h` | Stub for `sensor_ir_learn_enable()` (returns false). |
| `mute_stub.c`, `mute_stub.h` | Stub for `get_mute_play_flag()` (returns true). |
| **main/gui/** | |
| `ui_kavach.c`, `ui_kavach.h` | Single screen: title “Kavach”, status label, on-screen light; `kavach_ui_set_status()`, `kavach_ui_set_light()`. |
| `font/font_en_24.c` | LVGL font used by the minimal UI. |
| **spiffs/** | |
| `echo_*_*.wav` | WAVs for voice confirmations; flashed as the `storage` SPIFFS partition. |

---

## 4. Files/folders you can omit in the new repo

These exist in the current `kavach_demo` but are **not** used by the minimal build (they are excluded by `main/CMakeLists.txt` or not referenced):

- All of **main/rmaker/** (RainMaker).
- **main/app:** `app_led`, `app_fan`, `app_switch`, `app_sntp`, `app_wifi`, `file_manager` (both .c and .h).
- **main/gui:** `ui_main`, `ui_about_us`, `ui_boot_animate`, `ui_device_ctrl`, `ui_factory_mode`, `ui_hint`, `ui_mute`, `ui_net_config`, `ui_player`, `ui_sensor_monitor`, `ui_sr`.
- **main/gui/font:** all fonts **except** `font_en_24.c`.
- **main/gui/image:** entire folder (no images in minimal UI).
- **main/gui:** `lv_symbol_extra_def.h` (only needed by excluded UI).
- **spiffs/mp3/** (not used).
- **spiffs/echo_*_wake.wav**, **echo_*_end.wav** (optional; only if you never use them elsewhere).

So the **minimal repo** only needs the tree in section 3.1 (and optionally this doc).

---

## 5. Root CMakeLists.txt for a standalone repo

If your new repo has the BSP inside it (e.g. as `components/bsp` from a copy or submodule of esp-box):

```cmake
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# BSP: path to folder containing bsp component (e.g. esp-box/components or ./components)
set(EXTRA_COMPONENT_DIRS "components")

add_compile_options(-fdiagnostics-color=always
                    -Wno-ignored-qualifiers
                    -Wno-deprecated-declarations
                    -Wno-unused-but-set-variable)

project(kavach_demo)
```

If the BSP is in a sibling **esp-box** repo:

```cmake
set(EXTRA_COMPONENT_DIRS "../esp-box/components")
```

---

## 6. main/CMakeLists.txt for a minimal-only repo

If you **only** ship the minimal set of sources (no extra app/gui files), you can list sources explicitly and drop the FILTER logic:

```cmake
set(SRCS
    main.c
    settings.c
    app/app_sr.c
    app/app_sr_handler.c
    app/app_rmaker_stub.c
    app/sensor_stub.c
    app/mute_stub.c
    gui/ui_kavach.c
    gui/font/font_en_24.c
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "app" "gui"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-deprecated-declarations")
set_source_files_properties(${SRCS} PROPERTIES COMPILE_OPTIONS -DLV_LVGL_H_INCLUDE_SIMPLE)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
```

That keeps the new repo clean and contains only what’s strictly necessary for this program.
