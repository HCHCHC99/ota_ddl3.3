# Flash Partition Layout

## Overview

HC32F460xE: 512KB Flash (`0x00000000` – `0x0007FFFF`), sector size `0x2000` (8KB).

```
0x00000000 +===========================================================+
           |                    BOOTLOADER (128KB)                      |
           |  Boot_StartupSequence, WDT fallback, slot selection,       |
           |  UDS OTA programming mode (Bootloader_UdsMain)             |
           |  Source: boot/projects/.../Bootloader_App/                 |
0x00020000 +-----------------------------------------------------------+
           |                    BOOTLOADER (reserved)                    |
           |  128KB allocated in linker script                           |
           |  Unused flash space for bootloader growth                  |
0x00040000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x00050000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x00070000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x00090000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x000B0000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x000D0000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x000F0000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (64KB)                 |
0x00010000 +-- UDS Shared State (8KB) ---------------------------------+
           |  stc_uds_shared_t (56 bytes): magic, phase, target_slot,   |
           |  fw_size, fw_crc, result, pending_sid                      |
           |  Written by: UdsShared_Write() in Bootloader_App.c         |
           |  Written from: 0x11 handler (Bootloader), 0x31 handler     |
           |                 (APP), FlashDownload completion            |
           |  Read by: App_CheckPendingUdsAck() (APP startup),          |
           |           Boot_StartupSequence() (Bootloader startup)      |
0x00012000 +-----------------------------------------------------------+
           |                    UNUSED / RESERVED (16KB)                 |
0x00016000 +-- APP1 State Sector (8KB) --------------------------------+
           |  Offset +0x000: WDT feed control (WDT_FEED_ENABLE/DISABLE) |
           |  Offset +0x008: WDT reset count (max 3 => slot disabled)   |
           |  Written by: SetWdtFeedControl(), UpdateWdtResetCount()    |
           |  Read by: InitAppInfo() in Boot_StartupSequence()          |
0x00018000 +-- APP2 State Sector (8KB) --------------------------------+
           |  Offset +0x000: WDT feed control (WDT_FEED_ENABLE/DISABLE) |
           |  Offset +0x008: WDT reset count (max 3 => slot disabled)   |
           |  Same structure as APP1 state sector                       |
0x0001A000 +===========================================================+
           |                    APP1 FIRMWARE (196KB)                    |
           |  Slot A: motor control application                         |
           |  VTOR = 0x0001A000 (APP1_START_ADDR)                       |
           |  Source: app1/projects/.../                                |
           |  LED debug: PB7 toggle 5 times                             |
0x0004B000 +-----------------------------------------------------------+
           |                    UNUSED (4KB)                             |
0x0004C000 +===========================================================+
           |                    APP2 FIRMWARE (196KB)                    |
           |  Slot B: identical to APP1 (backup / OTA target)           |
           |  VTOR = 0x0004C000 (APP2_START_ADDR)                       |
           |  Source: app2/projects/.../                                |
           |  LED debug: PB7 toggle 2 times                             |
           |  OTA download target: UDS_TARGET_FLASH_ADDR                |
0x0007B000 +-----------------------------------------------------------+
           |                    UNUSED (4KB)                             |
0x0007C000 +-- APP_RUN_SLOT Sector (8KB) ------------------------------+
           |  Slot magic at offset 0x000:                               |
           |    SLOT_A_MAGIC = 0x5A5A5A5A => boot APP1                 |
           |    SLOT_B_MAGIC = 0xA5A5A5A5 => boot APP2                 |
           |  Written by: Boot_SetRunSlotToAddr(), Boot_SwitchAndRun()  |
           |  Read by: GetCurrentSlot() in Boot_StartupSequence()       |
0x0007E000 +-----------------------------------------------------------+
           |                    UNUSED (8KB)                             |
0x00080000 +===========================================================+
```

## Key Macros (defined in Bootloader_App.h)

```
+---------------------------+----------------+------------------------------------------+
| Macro                     | Value          | Purpose                                  |
+---------------------------+----------------+------------------------------------------+
| APP1_START_ADDR           | 0x0001A000     | APP1 (Slot A) base address               |
| APP2_START_ADDR           | 0x0004C000     | APP2 (Slot B) base address               |
| APP_RUN_SLOT_ADDR         | 0x0007C000     | Current active slot magic storage        |
| APP1_STATE_SECTOR_BASE    | 0x00016000     | APP1 WDT state / feed control            |
| APP2_STATE_SECTOR_BASE    | 0x00018000     | APP2 WDT state / feed control            |
| UDS_SHARED_SECTOR_BASE    | 0x00010000     | UDS OTA shared state (Bootloader <-> APP) |
| UDS_TARGET_FLASH_ADDR     | APP2_START_ADDR| OTA firmware download target              |
| UDS_POST_FLASH_BOOT_ADDR  | APP1_START_ADDR| Post-OTA forced boot slot (hardcoded)     |
| UDS_SHARED_MAGIC          | 0x55445300     | "UDS\0" validation magic                  |
+---------------------------+----------------+------------------------------------------+
```

## Code-to-Flash Mapping

```
+----------------------+------------------------------------+-----------------------------+
| Flash Region         | Related Code / Module              | Key Functions               |
+----------------------+------------------------------------+-----------------------------+
| 0x00000000 (Boot)    | Bootloader_App.c/h                 | Boot_StartupSequence()      |
|                      |                                    | Bootloader_JumpToApp()      |
|                      |                                    | Bootloader_UdsMain()        |
|                      |                                    | Boot_SetRunSlotToAddr()     |
|                      |                                    | Boot_SwitchAndRunOther()    |
|                      |                                    | GetCurrentSlot()            |
|                      |                                    | SelectTargetSlot()          |
+----------------------+------------------------------------+-----------------------------+
| 0x00010000 (UDS Sh.) | Bootloader_App.c                   | UdsShared_Read()            |
|                      |                                    | UdsShared_Write()           |
|                      |                                    | UdsShared_Clear()           |
|                      |                                    | UdsShared_SetPhase()        |
|                      |                                    | App_CheckPendingUdsAck()    |
|                      +------------------------------------+-----------------------------+
|                      | UDS/uds_diagnostic.c               | uds_handle_ecu_reset()      |
|                      |                                    | uds_handle_routine_control()|
+----------------------+------------------------------------+-----------------------------+
| 0x00016000 (APP1 St) | Bootloader_App.c                   | SetWdtFeedControl()         |
|                      |                                    | GetWdtFeedControl()         |
|                      |                                    | UpdateWdtResetCount()       |
|                      |                                    | GetWdtResetCount()          |
|                      |                                    | ClearWdtResetCount()        |
|                      |                                    | InitAppInfo()               |
+----------------------+------------------------------------+-----------------------------+
| 0x00018000 (APP2 St) | Bootloader_App.c                   | (same functions as APP1)    |
+----------------------+------------------------------------+-----------------------------+
| 0x0001A000 (APP1)    | app1/projects/.../                 | Whole application firmware  |
|                      | Bootloader_App.c (shared module)   | App_CheckPendingUdsAck()    |
|                      | main.c                             | main() - APP1 startup       |
+----------------------+------------------------------------+-----------------------------+
| 0x0004C000 (APP2)    | app2/projects/.../                 | Whole application firmware  |
|                      | Bootloader_App.c (shared module)   | App_CheckPendingUdsAck()    |
|                      | main.c                             | main() - APP2 startup       |
|                      +------------------------------------+-----------------------------+
|                      | FlashDownload (OTA target)         | UDS_TARGET_FLASH_ADDR       |
|                      |                                    | FW_APP_START_ADDR           |
+----------------------+------------------------------------+-----------------------------+
| 0x0007C000 (Slot)    | Bootloader_App.c                   | Boot_SetRunSlotToAddr()     |
|                      |                                    | Boot_SwitchAndRunOther()    |
|                      |                                    | GetCurrentSlot()            |
+----------------------+------------------------------------+-----------------------------+
```

## Shared RAM Layout (not Flash, but related)

```
0x1FFF8000 +-- RAM_START_ADDR -----------------------------------------+
           |                    RAM (188KB)                              |
           |                    Main stack + data + BSS                 |
0x1FFFAF00 +-- SHARED_CTRL_ADDR (top of RAM - 0x100) ------------------+
           |  stc_shared_ctrl_t (32 bytes):                             |
           |    app1_feed_ctrl: WDT_FEED_ENABLE(0) or DISABLE(0xDEADBEEF)|
           |    app2_feed_ctrl: WDT_FEED_ENABLE(0) or DISABLE(0xDEADBEEF)|
           |    debug_flag: WDT feed persistence across debugger resets  |
           |  Accessed via GetSharedCtrl() inline function               |
           |  Used by: TMR0_Unit2_IRQHandler (WDT feeding decision)     |
0x1FFFAF20 +-- RAM_END_ADDR -------------------------------------------+
```

## Sector Erase Notes

- All Flash writes require sector erase first (8KB granularity)
- `Boot_SetRunSlotToAddr()` erases `APP_RUN_SLOT_ADDR` sector (0x7C000), writes 1 word
- `UdsShared_Write()` erases `UDS_SHARED_SECTOR_BASE` sector (0x10000), writes 56 bytes
- `FlashDownload` erases APP2 sectors (0x4C000–0x57000 range, 48KB max)
- `SetWdtFeedControl()` / `UpdateWdtResetCount()` erase APPx state sectors (8KB each)
- **No two functions share erase on the same sector** — `UDS_SHARED_SECTOR_BASE` ≠ `APPx_STATE_SECTOR_BASE`

---

## Flash Management Code Modules

### Overview (4 layers)

```
+------------------+  uds_diagnostic.c   (UDS protocol dispatch)
|   UDS Layer      |  uds_dl_bridge.c    (interface bridge)
+------------------+
|   Flash Logic    |  flash_download.c   (OTA state machine, buffer, CRC)
|                  |  param_manager.c    (wear-leveled param storage)
|                  |  Bootloader_App.c   (UDS shared, WDT state, slot)
+------------------+
|   Flash Advanced |  flash_advanced.c   (protection, statistics, lifetime)
+------------------+
|   Flash Low-level|  hc32f46x_flash.c   (DDL EFM wrapper, .ramfunc)
+------------------+
```

---

### 1. `hc32f46x_flash.c` / `.h` — Low-level Flash Adapter

**Location:** `boot/Adp/`, `app1/Adp/`, `app2/Adp/`

**Purpose:** Wraps HC32F460 DDL EFM driver with unified status codes and debug macros.

**Key API:**

```
HC32FLASH_EraseSector(addr)            // Erase 8KB sector (runs from RAM, .ramfunc)
HC32FLASH_WritedWord_NoCheck(addr,data)// Write 1 word, no read-back verify
HC32FLASH_WritedWord_Check(addr,data)  // Write 1 word with read-back verify (runs from RAM, .ramfunc)
HC32FLASH_ReaddWord(addr)             // Direct read (bypasses cache)
HC32FLASH_GetStatus()                 // WPRERR/PGAERR/PEWERR/COLERR/PGMISMTCH
```

**Limitations:**
- Erase & checked-write MUST execute from RAM (`.ramfunc` section) — HC32F460 cannot run code from Flash while erasing/writing the same Flash bank
- Erase granularity is always 8KB (hardware sector size), no sub-sector erase
- Erase sets all bits to 1; program can only clear bits to 0 (Flash physics)
- Write without read-back check is faster but less safe — use only when caller verifies
- `FLASH_DEBUG_ENABLE` compile flag gates debug output (per-project Options → C/C++ → Define)

---

### 2. `flash_advanced.c` / `.h` — Flash Sector Protection & Statistics

**Location:** `boot/Adp/`, `app1/Adp/`, `app2/Adp/`

**Purpose:** Protection layer above `hc32f46x_flash.c`. Tracks erase counts per-sector, enforces protected regions, supports bulk writes, and persists lifetime info to sector 9 (0x12000).

**Protected sectors (macros in flash_advanced.h):**

```
+--------+--------+---------------------------------------+
| Sector | Addr   | What                                  |
+--------+--------+---------------------------------------+
|  0-7   | 0x00000| Bootloader (128KB)                    |
|    8   | 0x10000| UDS Shared State                      |
| 9-10   | 0x12000| Reserved (9 = management record)      |
| 11-12  | 0x16000| APP1/APP2 WDT State                   |
|   63   | 0x7E000| Extra protected (unused, guard)       |
+--------+--------+---------------------------------------+
```

**Valid user sectors (writable by FlashDownload):** 13-61 (0x1A000-0x7BFFF)

**Key API:**

```
FlashAdv_Create(config, ops)         // Create handle with config + ops table
FlashAdv_EraseSector(handle, addr)   // Protected-sector check + erase
FlashAdv_BulkWriteSimple(handle, addr, data, words) // Multi-word write
FlashAdv_IsAddressProtected(handle, addr)           // Protection check
FlashAdv_GetLifetimeInfo(handle, info)              // Erase counts per-sector
FlashAdv_SaveLifetimeInfo(handle)                   // Persist to sector 9
```

**Limitations:**
- `FlashAdvConfig_t.max_erase_cycles` set to 10000 (typical HC32F460 endurance), only advisory — no hardware enforcement
- Management record in sector 9 (0x12000): if this sector wears out, lifetime tracking is lost but flash operations still work
- Sector 63 is hard-protected by macro (`FLASH_ADV_PROTECTED_SECTOR 63`), treated as never-writable
- `FLASH_MANAGEMENT_RECORD_ENABLE` compile flag controls whether lifetime data is persisted

---

### 3. `Bootloader_App.c` / `.h` — Bootloader Flash Operations (shared module)

**Location:** All 3 projects share identical code via `boot/projects/.../Bootloader_App/`, `app1/.../Bootloader_App/`, `app2/.../Bootloader_App/`

**Purpose:** Central flash management for boot logic. Handles UDS shared state, WDT state, slot selection, and boot-to-app transitions.

**Flash functions & their sectors:**

| Function | Flash Sector | Writes | Erases |
|----------|-------------|--------|--------|
| `READ_FLASH_DIRECT(addr)` | (any) | No | No (bypasses cache for clean read) |
| `Bootloader_FlashEraseSector(addr)` | (any) | No | Yes (generic wrapper) |
| `UdsShared_Read(pState)` | 8 (0x10000) | No | No (reads 56 bytes via READ_FLASH_DIRECT) |
| `UdsShared_Write(pState)` | 8 (0x10000) | Yes (56 bytes) | Yes (full sector) |
| `UdsShared_Clear()` | 8 (0x10000) | No | Yes (full sector) |
| `UdsShared_SetPhase(phase, slot)` | 8 (0x10000) | Yes (56 bytes) | Yes (full sector) |
| `GetWdtFeedControl(addr)` | 11 or 12 (0x16000/0x18000) | No | No |
| `SetWdtFeedControl(addr, val)` | 11 or 12 | Yes (2 words) | Yes (full sector) |
| `GetWdtResetCount(addr)` | 11 or 12 | No | No |
| `UpdateWdtResetCount(addr, cnt)` | 11 or 12 | Yes (2 words) | Yes (full sector) |
| `ClearWdtResetCount(addr)` | 11 or 12 | Yes (2 words) | Yes (full sector) |
| `ClearAppStateBySlot(slot)` | 11 or 12 | Yes (2 words) | Yes (full sector) |
| `Boot_SetRunSlotToAddr(addr)` | 62 (0x7C000) | Yes (1 word) | Yes (full sector) |
| `Boot_SwitchAndRunOther()` | 62 (0x7C000) | Yes (1 word) | Yes (full sector) |
| `UpdateSlotFlagToFlash(ctx)` | 62 (0x7C000) | Yes (1 word) | Yes (full sector) |

**Common pattern (all write functions):**
```
EFM_REG_Unlock() → EFM_FWMC_Cmd(ENABLE) → wait RDY → EFM_SectorErase() → EFM_ProgramWord() × N → EFM_REG_Lock()
```

**Key protocol: Read-before-erase for WDT state sectors:**
- APPx state sector stores TWO values: `WDT_FEED_CONTROL` at offset +0x000, `WDT_COUNT` at offset +0x008
- Before erase, BOTH values are read into local variables
- After erase, BOTH values are written back (one possibly modified)
- This avoids data loss from the value NOT being modified

**Limitations:**
- `READ_FLASH_DIRECT()` temporarily disables Flash cache, making consecutive reads slow
- WDT state functions erase the entire 8KB sector to modify just 2 words (wear concern, but WDT resets are rare)
- `APP_RUN_SLOT` sector (0x7C000) is sector 62 — overlaps with `param_manager` sector range (56-62), see sector conflict analysis below
- `UDS_POST_FLASH_BOOT_ADDR` is hardcoded to `APP1_START_ADDR` — no runtime config
- `UDS_TARGET_FLASH_ADDR` is hardcoded to `APP2_START_ADDR` — OTA always flashes APP2
- No wear-leveling for UDS shared sector (0x10000) — only erased during OTA (rare)
- `Boot_SwitchAndRunOther()` calls `NVIC_SystemReset()` directly — uses SYSRESETREQ (breaks J-Link connection)

---

### 4. `flash_download.c` / `.h` — OTA Firmware Download Module

**Location:** `boot/UDS/` (only in bootloader — bootloader is the sole flash writer during OTA)

**Purpose:** Manages the OTA firmware download state machine. Receives blocks via UDS 0x34/0x36/0x37, buffers in 60KB RAM, writes to Flash when buffer is full or transfer ends.

**State machine:**
```
IDLE → PREPARING → READY → TRANSFERRING → VERIFYING → COMPLETE
  ↑        ↓           ↓          ↓            ↓           |
  +--------+-----------+----------+------------+------ ERROR
```

**Key configuration (compile-time):**

| Macro | Default | Meaning |
|-------|---------|---------|
| `FW_APP_START_ADDR` | 0x0004C000 | OTA target = APP2 |
| `FW_APP_MAX_SIZE` | 0x0000C000 | Max firmware = 48KB |
| `FW_RAM_BUFFER_SIZE` | 60 × 1024 | RAM buffer (not on stack, aligned to 4) |
| `FW_FLASH_WRITE_ENABLED` | 1 | Safety switch: 0 = dry-run (no real flash write) |
| `TBOX_ADDR_START` | 0x08004000 | TBOX sends this address range |
| `MAP_TBOX_ADDR_TO_FLASH(addr)` | — | Maps 0x08004xxx → 0x0004Cxxx |

**Key internal structure (FlashDownloadContext_t):**
- `state / last_error` — state machine tracking
- `target_address / total_size / received_size` — download progress
- `buffer_offset` — position in 60KB RAM buffer
- `expected_sequence` — block sequence validation (1-255)
- `flash_handle` — FlashAdv handle for sector protection + erase/write
- `rx_crc` — running CRC32 of received firmware

**Key API (called by UDS layer via uds_dl_bridge):**

```
FlashDownload_Init(config)           // Init state machine + FlashAdv handle
FlashDownload_OnRequestDownload(addr, size) // UDS 0x34: validate + erase range
FlashDownload_OnTransferData(seq, data, len)// UDS 0x36: buffer + write if full
FlashDownload_OnTransferExit()       // UDS 0x37: final flush + CRC verify
FlashDownload_Task()                 // Main loop: process async operations
FlashDownload_GetState()             // Current state
FlashDownload_IsPending()            // true → send NRC 0x78 (Response Pending)
```

**Limitations:**
- `FW_RAM_BUFFER_SIZE` = 60KB is a large static allocation (BSS), reduces available heap/stack
- `FW_FLASH_WRITE_ENABLED = 0` is a compile-time switch, not runtime — requires rebuild to change
- Only supports single-block flash write (buffer fills, write happens, buffer empties) — no concurrent receive+write
- Block sequence counter wraps at 255 (uint8_t) — no guard against wrap-around replay
- `FlashDownload_Task()` runs in main `while(1)` loop — blocks other tasks during flash erase/write (erase ~20ms, write ~4ms per 60KB)
- TBOX address mapping assumes TBOX always sends 0x08004xxx for APP2 — no support for flashing APP1 via OTA
- No resume after power-loss: if reset during TRANSFERRING, partial flash content + state lost
- `auto_reset_on_complete` defaults to 0 (disabled) — reset is handled by 0x11 ECU Reset service instead

---

### 5. `uds_dl_bridge.c` + `uds_dl_if.h` — UDS Download Interface Bridge

**Location:** `boot/UDS/`

**Purpose:** Decouples UDS protocol layer from flash_download implementation. Uses a function pointer table (`uds_dl_if_t`) so the UDS layer can work with any download backend (firmware, data, configuration).

**Architecture:**
```
uds_diagnostic.c → uds_dl_get_if()->on_request_download(...)
                        ↓
                  uds_dl_bridge.c → flash_download.c
```

**Key types:**

| UDS Layer | Flash Layer |
|-----------|-------------|
| `uds_dl_result_t` | `FlashDownloadResult_t` |
| `uds_dl_state_t` | `FlashDownloadState_t` |
| `uds_dl_progress_t` | `FlashDownloadProgress_t` |

**Key functions:**
```
uds_dl_init_fw()                // Register firmware download interface (called at boot)
uds_dl_register(iface)         // Set active download interface
uds_dl_get_if()                // Get current interface (NULL if not registered)
uds_dl_is_registered()         // Check if any interface is active
```

**Supported DID readback (via 0x22 service):**

| DID | Value | Function |
|-----|-------|----------|
| 0xF000 | Firmware version | `FlashDownload_GetFirmwareVersion()` |
| 0xF001 | Bootloader version | `FlashDownload_GetBootloaderVersion()` |
| 0xF002 | Firmware CRC32 | `FlashDownload_GetFirmwareCRC()` |

**Limitations:**
- Only ONE download interface can be registered at a time (single `g_dl_iface` pointer)
- `dl_fw_init()` ignores `config_data`/`config_len` — always calls `FlashDownload_Init(NULL)` which uses defaults
- No support for multiple simultaneous download types (firmware + data)

---

### 6. `param_manager.c` / `.h` — Wear-Leveled Parameter Storage

**Location:** `boot/Utils/`, `app1/Utils/`, `app2/Utils/`

**Purpose:** Store application parameters (motor config, calibration data) in Flash with wear-leveling across 7 sectors. Survives power cycles and firmware updates.

**Sector layout:**
```
Sector 62 ←── SEC_START (highest priority, try first)
Sector 61
Sector 60
Sector 59
Sector 58
Sector 57
Sector 56 ←── SEC_END
All sectors: 0x70000 - 0x7DFFF (7 × 8KB = 56KB)
```

**Per-sector data format:**
```
+------------------+
| Magic Head       | 0x55AA55AA
| Sequence ID      | incrementing, for wear-leveling
| Erase Count      | lifetime tracking
| Parameter Data   | user-defined struct (size from Param_Config_t)
| CRC32 / Checksum |
| Magic Tail       | 0xAA44AA44
+------------------+
```

**Wear-leveling algorithm:**
- On init: scan sectors 62→56, find valid block with highest sequence ID (most recent), copy data to RAM
- On save: write to NEXT sector (current - 1, wrapping from 56→62), with incremented sequence
- If all sectors full: erase current sector, rewrite — effectively round-robin
- Erase count tracked per write, persisted in sector structure

**Key API:**
```
Param_Init(pConfig, pSetDefaults)   // Init: scan Flash, load valid params (or set defaults)
Param_Save(pConfig)                 // Write current params to next available sector
Param_Debug_EraseAll(pConfig, pDefaults) // Debug: erase all param sectors
```

**Key configuration (Param_Config_t):**
- `pParamBuf` — pointer to RAM parameter struct
- `paramSize` — size of struct in bytes (must fit in one 8KB sector)
- `magicHead / magicTail` — validation magic numbers
- `checksumOffset / seqOffset / eraseCntOffset` — field offsets within struct

**Limitations:**
- Only ONE parameter struct type per `Param_Config_t` instance (single-purpose)
- Parameter struct must fit in one sector (≤ 8KB minus headers = ~8KB usable)
- Sectors 56-62 overlap with APP_RUN_SLOT sector (62 at 0x7C000) — param_manager and Bootloader_App slot management MUST NOT write concurrently. In practice: Bootloader writes slot only during boot, APP writes params during normal operation. If the bootloader ever uses param_manager, conflict is possible.
- Sector 61 (0x7A000-0x7BFFF) overlaps with tail of APP2 firmware area — APP2 firmware size must not exceed sectors 38-60 (184KB) if param_manager is in use
- No wear-leveling across the 10000-cycle endurance limit of each sector — after 7 × 10000 = 70000 total saves, Flash endurance is exhausted
- `Internal_Erase()` uses `hc32f46x_flash.c` directly (not FlashAdvanced), so no protection check — could theoretically erase a protected sector if misconfigured
- `PARAM_DEBUG` is gated by compile flag — must enable to see wear statistics

---

### Sector Conflict / Coexistence Analysis

```
Sector   Address     Assigned To              Notes
------   -------     ------------              -----
  8      0x10000     UDS Shared State          Exclusive. Only written during OTA.
 11      0x16000     APP1 WDT State            Exclusive. Read-before-erase for 2 values.
 12      0x18000     APP2 WDT State            Exclusive. Read-before-erase for 2 values.
 38-55   0x4C000     APP2 Firmware (OTA)       Written by FlashDownload during OTA only.
 56-60   0x70000     param_manager             Wear-leveled params. APP2 firmware must stop
                                               before sector 56 (0x70000) to avoid overlap.
 61      0x7A000     param_manager (shared)    Last param sector. Conflicts with APP2 if
                                               APP2 > 184KB (0x2E000).
 62      0x7C000     APP_RUN_SLOT + param      SLOT magic at offset 0x000 (1 word). param_manager
                                               may use this sector for parameter storage. The two
                                               systems must be coordinated:
                                               - Boot writes slot magic during boot sequence
                                               - APP writes params during normal operation
                                               - Each must read-before-erase the other's data
                                               - Currently: no coordination code exists
 63      0x7E000     Protected (unused)        FlashAdvanced hard-protects this sector.
```

---

### Compile-Time Safety Gates

| Macro | File | 0 = Safe | 1 = Active |
|-------|------|----------|------------|
| `FW_FLASH_WRITE_ENABLED` | flash_download.h | No real flash writes (dry-run) | Real writes enabled |
| `FLASH_DEBUG_ENABLE` | hc32f46x_flash.h | No debug output | Print status per operation |
| `FLASH_MANAGEMENT_RECORD_ENABLE` | flash_advanced.h | No lifetime tracking | Persist erase counts |
| `PARAM_DEBUG` | param_manager.h | No debug output | Print scan/wear info |

---

### Flash Write Flow Summary (all paths to Flash modification)

```
UDS OTA Flow:
  CAN RX → ISOTP → uds_receive_handler → uds_dl_if_t.on_transfer_data
    → FlashDownload_OnTransferData → buffer in RAM (60KB)
    → FlashDownload_Task() → FlashDownload_FlushBuffer
    → FlashAdv_BulkWriteSimple → hc32f46x_flash.c (.ramfunc) → EFM

UDS Shared State:
  0x11/0x31 handler → UdsShared_SetPhase/Write
    → EFM_REG_Unlock → EFM_SectorErase(UDS_SHARED_SECTOR_BASE) → EFM_ProgramWord×14 → EFM_REG_Lock

WDT State Update:
  TMR0_Unit2_IRQHandler (WDT timeout) → UpdateWdtResetCount
    → Read both values → EFM_Erase → Write both values back

Slot Selection:
  Boot_StartupSequence → Boot_SetRunSlotToAddr / UpdateSlotFlagToFlash
    → EFM_Erase(APP_RUN_SLOT_ADDR) → EFM_ProgramWord(magic)

Parameter Save:
  Application code → Param_Save → find next sector → Internal_Erase → ProgramWord×N
```

---

## Partition Optimization Recommendations (分区优化建议)

### Current Problem

Two modules share sector 62 (0x7C000) without coordination code:

| Module | Sector | Address | What It Writes |
|--------|--------|---------|----------------|
| `param_manager` | 56-62 | 0x70000-0x7DFFF | Parameter struct (with magic/CRC) |
| `Bootloader_App` | 62 | 0x7C000 offset 0x000 | SLOT_A_MAGIC or SLOT_B_MAGIC (1 word) |

Both erase the entire sector before writing their data. If one writes after the other, the first writer's data is lost. Currently there is no read-before-erase coordination between these two modules.

### Key Context

OTA download only uses **6 sectors** (38-43, 0x4C000-0x57FFF, 48KB). Sectors 44-55 (0x58000-0x6FFFF, 96KB) are completely unused. The "196KB APP2 partition" in the linker script is an allocation ceiling, not actual usage.

### Plan A: Minimal Fix (recommended for now)

**Change:** `param_manager.c` line 5 — `SEC_START` from `62` to `61`

```c
// Before:
#define SEC_START           62
// After:
#define SEC_START           61
```

**Result:**
```
Sector   Address     Owner                   Size
------   -------     -----                   ----
 44-55   0x58000     FREE (unused)           96KB
 56-61   0x70000     param_manager only      6×8KB=48KB
 62      0x7C000     APP_RUN_SLOT only       8KB
 63      0x7E000     Protected (guard)       8KB
```

| Pros | Cons |
|------|------|
| Only 1 line changed | param_manager loses 1 sector (7→6) |
| No FlashAdvanced changes needed | 6 sectors × 10K = 60K writes still sufficient |
| Zero risk of side effects | |
| Sector 63 stays as safety boundary | |

**Files to change:** `param_manager.c` × 3 projects (boot, app1, app2)

---

### Plan B: Clean Architecture (if more space needed later)

**Changes:**

| File | Change |
|------|--------|
| `Bootloader_App.h` | `APP_RUN_SLOT_ADDR`: `0x7C000` → `0x7E000` |
| `flash_advanced.h` | `FLASH_ADV_PROTECTED_SECTOR`: `63` → remove or set to invalid |
| `param_manager.c` | No change (keep `SEC_START 62`) |

**Result:**
```
Sector   Address     Owner                   Size
------   -------     -----                   ----
 44-55   0x58000     FREE (unused)           96KB
 56-62   0x70000     param_manager only      7×8KB=56KB
 63      0x7E000     APP_RUN_SLOT only       8KB
```

| Pros | Cons |
|------|------|
| param_manager keeps 7 sectors | 3 files changed |
| Sector 63 becomes useful instead of wasted | FlashAdvanced protection change has wider impact |
| Clean conceptual separation | Need to verify FlashAdvanced doesn't rely on sector 63 protection |
| 7 sectors × 10K = 70K writes | |

**Files to change:** `Bootloader_App.h` × 3, `flash_advanced.h` × 3, `param_manager.c` × 3

---

### Additional Observations (not urgent)

1. **Sectors 44-55 are free (96KB):** Could be used for a second OTA slot, data logging partition, or expanded param_manager range without touching APP2's current 48KB footprint.

2. **APP2 linker script claims 196KB but OTA only writes 48KB:** Consider reducing the APP2 partition in the linker script to match the actual OTA size, freeing sectors 44-55 explicitly.

3. **Sector 9 (0x12000) management record:** Currently used by FlashAdvanced for lifetime tracking. If this sector wears out, only statistics are lost — flash operations continue normally. Consider migrating this to the free area (sectors 44-55) if endurance becomes a concern.

4. **No concurrent-write guard:** There is no mutex or flag preventing `param_manager` and `Bootloader_App` from erasing the same sector simultaneously in different interrupt contexts. This is mitigated by the fact that Bootloader runs exclusively (no APP code executing), and param_manager is only called by APP code.
