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
