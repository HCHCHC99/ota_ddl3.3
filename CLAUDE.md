# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Motor control system based on HC32F460 (Cortex-M4) with RS485/Modbus RTU and CAN/UDS (ISO 14229) communication, supporting firmware-over-the-air (OTA) download. Controls DC motors with Hall sensor feedback, over-current/voltage detection, rotation angle limiting, and fault handling.

**DDL version:** HC32F460 DDL Rev3.3.0 (from `ChangeLog.md`). The LL driver API reflects Rev3.3.0 naming conventions (e.g., `stc_tmr6_init_t`, not older `stc_timer6_init_t`). When referencing DDL APIs, use the Rev3.3.0 header definitions under `drivers/hc32_ll_driver/`.

**License:** See `LICENSE` (HDSC proprietary).

The firmware is split into **three independently-built images** under `ota_ddl3.3_v.1.1/`:

| Image | Directory | Flash Start | Role |
|-------|-----------|-------------|------|
| Bootloader | `boot/` | `0x00000000` | Startup, slot selection, WDT fallback, UDS OTA programming |
| APP1 | `app1/` | `0x0001A000` | Motor control application (slot A) |
| APP2 | `app2/` | `0x0004C000` | Motor control application (slot B, identical to APP1) |

Each directory is a self-contained Keil project with its own copy of the HC32F460 DDL (drivers, midwares, application source).

## File Encoding Warning

**Most `.c` and `.h` files use GBK encoding** (Chinese comments). However, **`Timer0_Unit2.c` files are UTF-16LE** (Keil MDK default for newly created files). Always check encoding before editing:

```bash
file file.c   # check encoding first
```

For GBK files:
```bash
iconv -f GBK -t UTF-8 file.c > file_utf8.c     # before editing
iconv -f UTF-8 -t GBK file_utf8.c > file.c      # after editing
```

For UTF-16LE files:
```bash
iconv -f UTF-16LE -t UTF-8 file.c > file_utf8.c  # before editing
iconv -f UTF-8 -t UTF-16LE file_utf8.c > file.c   # after editing
```

Never use `sed` directly on UTF-16LE files — it will corrupt them.

## Build System

- **IDE:** Keil MDK (uVision 5), **MCU:** HC32F460xE (512KB Flash)
- **Three separate projects** must be built independently:
  - `boot/projects/ev_hc32f460_lqfp100_v2/template/MDK/template.uvprojx`
  - `app1/projects/ev_hc32f460_lqfp100_v2/template/MDK/template.uvprojx`
  - `app2/projects/ev_hc32f460_lqfp100_v2/template/MDK/template.uvprojx`
- Each project uses its own `HC32F460xE.sct` linker script (all three are identical: Flash at `0x00000000`, RAM at `0x1FFF8000`). The actual load address is determined by the Keil target settings — each project must be programmed to its designated Flash offset.
- **Build output:** `template/MDK/output/debug/` (`.axf`, `.hex`, `.bin`, `.map`)
- Open `.uvprojx` in Keil, click Build (F7), or use `UV4.exe -r template.uvprojx -o output.txt`
- **Debug probe:** JLink (Cortex-M4)
- IAR EWARM and Eclipse/GCC project files also exist under `template/EWARM/` and `template/GCC/` but are not actively maintained

## Flash Partition Layout

512KB Flash (`0x00000000`–`0x0007FFFF`), sector size `0x2000` (8KB):

```
0x00000000 ─────────── Bootloader (128KB)
0x00010000 ─── UDS Shared State (8KB sector)  ← Bootloader ↔ APP OTA handoff
0x00012000 ─── (reserved)
0x00016000 ─── APP1 State (8KB sector)         ← WDT count + feed control
0x00018000 ─── APP2 State (8KB sector)         ← WDT count + feed control
0x0001A000 ═══════════ APP1 (196KB)
0x0004C000 ═══════════ APP2 (196KB)
0x0007C000 ─── APP_RUN_SLOT (8KB sector)       ← Magic number: current active slot
```

- **APP_RUN_SLOT** (`0x7C000`): Stores `SLOT_A_MAGIC` (`0x5A5A5A5A`) or `SLOT_B_MAGIC` (`0xA5A5A5A5`) to track which APP is currently active
- **UDS Shared State** (`0x10000`): 56-byte `stc_uds_shared_t` struct used during OTA to pass phase/status between Bootloader and APP across resets (see UDS OTA Flow below)
- **APPx State sectors**: Persist WDT reset count (offset `+0x008`) and feed-control flag (offset `+0x000`). Both values survive across resets
- **Param Manager** uses sectors 56–62 for wear-leveled Flash persistence (CRC32, sequence numbers, magic headers)

## Bootloader Architecture

`Bootloader_App.c/h` is a **shared service module** used by bootloader, APP1, and APP2. It provides power-on checks, WDT/Flash state management, slot switching, and UDS shared-state operations. Each firmware calls the appropriate entry point from its own `main.c`:

| Firmware | `main.c` calls | Role |
|----------|---------------|------|
| Bootloader | `Boot_StartupSequence()` | Full startup: check UDS state, handle WDT fallback, select slot, jump to APP |
| APP1 | `App_CheckPendingUdsAck()` + own loop | Check pending UDS ACK, then run application (WDT feed via shared RAM) |
| APP2 | `App_CheckPendingUdsAck()` + own loop | Same as APP1, uses its own `app2_feed_ctrl` |

### Bootloader startup sequence (`Boot_StartupSequence()` in `Bootloader_App.c`)

1. **Check Shared RAM debug flag** — if set, persist WDT feed control from RAM to Flash
2. **Check UDS Shared State** — if phase is `UDS_PHASE_ENTER_BOOTLOADER`, enter `Bootloader_UdsMain()` for OTA programming (does not return)
3. **CheckAndClearAppState()** — debug hook: clear WDT state based on `g_u32Debug_ClearAppState` (1=clear APP1, 2=clear APP2, 3=clear both)
4. **Determine reset cause** — read RMU flags for SWDT/WDT reset
5. **Read current slot** from `APP_RUN_SLOT_ADDR`
6. **Init app info** — read WDT counts for both slots from their state sectors
7. **Handle WDT reset** — if WDT/SWDT caused reset, increment WDT count for the current slot. If count ≥ `MAX_WDT_RESET_COUNT` (3), mark slot DISABLED
8. **Select target slot** — prefer current slot if healthy, otherwise fall back to the other slot, or none if both disabled
9. **Jump to APP** via `Bootloader_JumpToApp()` — sets MSP, VTOR, then branches to reset vector

### Bootloader startup (`boot/template/source/main.c`)

1. `Hardware_Init()` — system clock, GPIO, peripherals, Timer0 (1ms tick + WDT feeding)
2. LED debug: toggle PB6 5 times (1s intervals) to indicate bootloader entry
3. `Boot_StartupSequence()` — full check sequence, then jump to selected APP (does not return unless both slots disabled)
4. Fallback `while(1) { __nop(); }` — reached only if both slots are disabled

### APP startup (each APP's own `main.c`)

1. Set `SCB->VTOR` to its own Flash base address (`APP1_START_ADDR` or `APP2_START_ADDR`)
2. `__enable_irq()` — enable global interrupts (Timer0 starts WDT feeding)
3. `Hardware_Init()` — system clock, GPIO, peripherals
4. `MAIN_D(...)` — log which APP is running
5. `App_CheckPendingUdsAck()` — if OTA just completed, send pending UDS ACK on CAN (e.g., `51 01` for ECU Reset)
6. LED debug: toggle PB7 N times (APP1: 5 toggles, APP2: 2 toggles) to visually identify running slot
7. Main `while(1) {}` — idle loop, all work done in interrupts (WDT via Timer0, CAN/RS485 via ISRs)

**Key point:** `Bootloader_App` does NOT contain `main()` or application main loops. Each firmware's `main.c` owns its own flow. The main loop is intentionally empty — WDT is fed in `TMR0_Unit2_IRQHandler`.

### WDT feeding mechanism (interrupt-based)

WDT feeding moved from main loop to `TMR0_Unit2_IRQHandler` (1ms timer interrupt):

```c
// Channel A, fires every 1ms:
tickTimer_Update();                    // global 1ms tick
s_wdt_cnt++;                            // increment counter
if (s_wdt_cnt >= 500) {                 // every 500ms:
    s_wdt_cnt = 0;
    // Check shared RAM feed control based on current firmware identity:
    if (SCB->VTOR == APP1_START_ADDR)   feed_ctrl = GetSharedCtrl()->app1_feed_ctrl;
    else if (SCB->VTOR == APP2_START_ADDR) feed_ctrl = GetSharedCtrl()->app2_feed_ctrl;
    // Bootloader (VTOR=0) always feeds; APP follows shared control:
    if (feed_ctrl == WDT_FEED_ENABLE)   SWDT_FeedDog();
}
```

- Uses `SCB->VTOR` to identify which firmware is running (bootloader=0, APP1=0x1A000, APP2=0x4C000)
- Bootloader always feeds WDT unconditionally; APPs respect shared RAM control
- This decouples WDT safety from main loop activity — WDT is fed even if main loop hangs

### WDT watchdog fallback

Each APP has its WDT independently fed by the Timer0 interrupt. The bootloader tracks WDT reset count per-slot in Flash:
- APP feeds WDT normally → count stays 0
- APP crashes → WDT reset → bootloader increments count → if ≥3, slot disabled → bootloader switches to other slot
- `SetWdtFeedControl()` allows disabling WDT feeding for a slot via shared RAM (e.g., `WDT_FEED_DISABLE = 0xDEADBEEF`)

### Shared RAM control

`stc_shared_ctrl_t` at `0x1FFF8000 + 0x2F000 - 0x100` (top of RAM, 32 bytes):
- `app1_feed_ctrl` / `app2_feed_ctrl`: `WDT_FEED_ENABLE` (0) or `WDT_FEED_DISABLE` (0xDEADBEEF)
- `debug_flag`: used to pass WDT feed control state from debugger reset to bootloader
- WDT is fed in `TMR0_Unit2_IRQHandler` based on VTOR identity + shared RAM control, NOT in main loop

### Slot switching: `Boot_SwitchAndRunOther()`

Saves the opposite slot's magic to `APP_RUN_SLOT_ADDR` then calls `NVIC_SystemReset()`. On next boot, `Boot_StartupSequence()` reads the new magic and jumps to the other APP. Used for APP-to-APP switching without going through full bootloader logic in main.

### LED debug indicators

Each firmware toggles a different GPIO at startup for visual identification:

| Firmware | GPIO | Toggle count | Duration |
|----------|------|-------------|----------|
| Bootloader | PB6 | 5 | ~5s |
| APP1 | PB7 | 5 | ~2.5s |
| APP2 | PB7 | 2 | ~1s |

## UDS OTA Flow (3-Phase Cross-Reset)

OTA firmware download uses UDS services (0x31/0x34/0x36/0x37) and spans multiple resets:

### Phase 1: APP running → TBOX sends OTA
1. TBOX sends UDS `0x31` (RoutineControl) to request OTA entry
2. APP's UDS stack handles `0x31`, writes `UDS_PHASE_ENTER_BOOTLOADER` + target slot to UDS shared Flash sector
3. APP calls `NVIC_SystemReset()`

### Phase 2: Bootloader → UDS Programming Mode
1. Bootloader starts, checks UDS shared state → sees `ENTER_BOOTLOADER` phase
2. Enters `Bootloader_UdsMain()`:
   - Sends `0x31` positive response (`71 01 FF 00`) on CAN
   - Initializes ISOTP, UDS, FlashDownload modules
   - Main loop: `CanIf_Poll()` → ISOTP RX → UDS dispatch → `FlashDownload_Task()` + WDT feed every 500ms
3. TBOX sends `0x34` (RequestDownload) → `0x36` (TransferData) → `0x37` (RequestTransferExit)
4. Firmware written to APP2 (0x4C000) via UDS_TARGET_FLASH_ADDR
5. Main loop detects `FW_UPDATE_COMPLETE`, writes `phase=PROGRAMMING_DONE, result=1, target_slot=SLOT_APP2` to shared sector. Does NOT reset.
6. TBOX sends `0x11` (ECU Reset) → Bootloader writes `pending_sid=0x11` to shared sector → `NVIC_SystemReset()` (NO CAN response)

### Phase 3: Bootloader → New APP
1. Bootloader starts, `Boot_SetRunSlotToAddr(UDS_POST_FLASH_BOOT_ADDR)` sets APP_RUN_SLOT to APP1
2. Jumps to APP1
3. APP startup calls `App_CheckPendingUdsAck()`:
   - If `pending_sid == 0x11`: sends deferred `51 01` (ECU Reset ACK) on CAN
   - If `phase == PROGRAMMING_DONE`: reads fw info (size/CRC/result)
   - Clears shared sector

### Key OTA constraints
- Flash download target is **APP2** (0x4C000), configured via `UDS_TARGET_FLASH_ADDR` / `FW_APP_START_ADDR` macros
- After OTA download, boot slot forced to **APP1** (0x1A000) via `UDS_POST_FLASH_BOOT_ADDR` macro (development hardcode, plan to make dynamic)
- `FlashDownload_Init()` in bootloader mode configures: `max_firmware_size=48KB`, `user_start_addr=UDS_TARGET_FLASH_ADDR` / `user_end_addr=UDS_TARGET_FLASH_ADDR+0xC000`
- TBOX address mapping: `MAP_TBOX_ADDR_TO_FLASH(0x08004000)` → `0x0004C000` (APP2)
- Context detection in UDS handlers: `SCB->VTOR == 0` = Bootloader, `SCB->VTOR == APP1/APP2_START_ADDR` = APP
- Deferred response pattern: UDS handler that triggers reset sets `*resp_len = 0` (no CAN response), writes intent to shared Flash, then NVIC_SystemReset(). Next firmware sends the deferred ACK.

## Source Tree (within each boot/app1/app2 project)

```
projects/ev_hc32f460_lqfp100_v2/
├── Adp/               # Hardware adapter layer (rs485, CAN, PWM, ADC, DMA, GPIO, timers, flash)
├── App/               # Application logic (motor control, Modbus, fault, realtime)
├── Bootloader_App/    # Bootloader core: slot selection, WDT fallback, UDS OTA, APP jump
├── Dev/               # Device drivers (motor, ADC, hall, voltage, sensor, EventBus, DeviceManager)
├── Utils/             # Utilities (ring_buf, msg_queue, lock, param_manager, TickTimer, RTT manager)
├── UDS/               # UDS diagnostic stack over CAN (ISO 14229 + ISO 15765-2 + OTA download)
├── RTT/               # SEGGER RTT debug output
└── template/
    ├── source/        # main.c, main.h, hc32f4xx_conf.h
    └── MDK/           # Keil project, startup, linker scripts, JLink config
```

Each firmware also has `drivers/` (bsp, cmsis, hc32_ll_driver) and `midwares/` (hc32) at its root.

## Dual Communication Stack

### RS485/Modbus RTU (4-layer top-down dependency)

```
App_Comm.c/h              — Register callbacks, motor control commands, Flash persistence
    ↓ calls
Protocol_ModbusRtu.c/h    — CRC16, function codes (0x03/0x06/0x10), exception responses
    ↓ calls
Comm_HAL.c/h              — Ring buffers, frame timeout (3.5 char times), TX queue
    ↓ calls
rs485.c/h                 — Pure hardware: USART4 + PA03 direction pin + ISRs
```

- Each layer only calls the layer directly below it — no cross-layer access
- All config aggregated into `App_Comm_Config_t` in main.c

### CAN/UDS (ISO 14229) + OTA Download

```
UDS/uds_diagnostic.c/h    — UDS service dispatch (0x10/0x22/0x27/0x2E/0x31/0x34/0x36/0x37/0x3E)
                              Session management, security access (seed/key), routine control
    ↓ calls
UDS/isotp_transport.c/h   — ISO 15765-2 multi-frame transport (SF/FF/CF/FC, 8KB buffer)
    ↓ calls
Adp/Adapter_Can.c/h       — CanIf layer: SW TX queue, RX filter/callback dispatch (16 entries)
    ↓ calls
Adp/Can_LLD.c/h           — Low-level CAN driver (CAN0 unit, FIFO, ISRs, Bus-Off recovery)
```

**Download interface abstraction** (`uds_dl_if.h`):
- `uds_dl_if_t` is a vtable of 14 function pointers
- `uds_dl_bridge.c` implements it by delegating to `FlashDownload_*` functions
- `FlashDownload_Task()` is the download state machine (erase → write → verify → CRC)
- This decouples UDS protocol from the download implementation (could swap for eMMC, external memory, etc.)

**CAN ID filter:** ISOTP filters 4 CAN IDs: `0x18DA03F1` (phys request), `0x18DAF103` (phys response), `0x18FF8118` (OTA), `0x18DBFFF0` (functional broadcast)

**Security:** Seed/key algorithm (`seedkey_calc_lv1_key`) in `security_access.c`, 3-attempt lockout

## Key Architecture Patterns

- **EventBus** (`Dev/EventBus.h`): Publish/subscribe for inter-module communication. 14 topics including `TOPIC_MANUAL_IO`, `TOPIC_VOLTAGE_ALARM`, `TOPIC_CURRENT_ALARM`, `TOPIC_RTURN_LIMIT`. Deferred publish — events published before `EventBus_Enable()` are queued and replayed on enable. Max 4 subscribers per topic, priority-ordered (0 = highest).

- **DeviceManager** (`Dev/device_manager.h`): Uniform device registry with time-sliced update scheduling. Each device gets an `update()` callback at its configured interval (1ms for most, 10ms for voltage bus). Mutex-protected. 16 devices registered.

- **Param Manager** (`Utils/param_manager.h`): Register-based parameter storage with Flash persistence. Parameters in `g_AppParam` (type `AppParamRecord_t`). Read/write via `Param_ReadByReg()`/`Param_WriteByReg()`, save via `Param_Save()`. Uses wear-leveled Flash storage with CRC32 validation.

- **Motor arbitration** (`Dev/dev_motor.c`): Commands go through motor arbitrator. Uses `block_fwd`/`block_rev` bitmask — multiple devices can independently block a direction. Arbitration re-evaluates on every block/unblock.

- **CAN RX dispatch:** `CanIf_RegisterRxFilter()` registers (CAN ID, mask, callback) entries. On RX, CanIf iterates registered filters and dispatches to matching callback. Unmatched frames go to optional default callback.

- **Simulation mode** (`ENABLE_SIMULATION_MODE=1` in `App_Motor_Project.c`): Enabled by default. `g_sim` struct holds simulated hardware signals. The main loop detects state changes and publishes EventBus events, allowing full motor arbitration testing without physical hardware.

- **SEGGER RTT** (`Utils/rtt_manager.h`): Debug logging via `MAIN_D()`, `COMM_DBG()`, `HAL_DEBUG()`, `ISOTP_D()`, `DL_D()` macros — output through RTT channel, not UART.

- **1ms tick gating** (`main.c`): ISOTP/UDS timeout counters must tick at exactly 1ms intervals. `TMR0_Unit2_IRQHandler` → `tickTimer_Update()` increments a global tick counter. In the main loop, `tickTimer_GetCount()` is compared against a saved last-tick value — timeout functions are only called when the tick value changes.

## Fault System

Fault bits stored in `g_RealTimeData.fault_status`, readable via Modbus register `0x2740`:

| Bit | Macro | Description |
|-----|-------|-------------|
| bit0 | `FAULT_BIT_OVERVOLTAGE` | Overvoltage |
| bit2 | `FAULT_BIT_OVERCURRENT` | Overcurrent |
| bit4 | `FAULT_BIT_OVERTEMP` | Overtemp |
| bit5 | `FAULT_BIT_STALL` | Motor stall |
| bit6 | `FAULT_BIT_UNDERVOLTAGE` | Undervoltage |

- `App_FaultHandler` subscribes to `TOPIC_VOLTAGE_ALARM` and `TOPIC_CURRENT_ALARM`
- Overcurrent triggers **dual blocking**: dev_motor + dev_rturn both block
- **Auto-clear mode**: faults clear when alarm resolves. **Manual-clear mode**: faults persist until Modbus write to `REG_FAULT_STATUS` (0x2740)

## Control Commands (REG_CTRL_CMD = 0x2720)

Bits written via Modbus function 0x06 (single write only):

| Bit | Value | Description |
|-----|-------|-------------|
| bit0 | 0x0001 | START — enable RS485 control |
| bit1 | 0x0002 | STOP — disable RS485 control, stop motor |
| bit2 | 0x0004 | ESTOP — emergency stop |
| bit3 | 0x0008 | RESET — `__NVIC_SystemReset()` after 200ms delay |
| bit4 | 0x0010 | FWD — forward (requires START, uses `g_AppParam.target_speed`) |
| bit5 | 0x0020 | REV — reverse (requires START, uses `g_AppParam.target_speed`) |

Typical sequence: START (0x0001) → FWD (0x0011) → STOP (0x0002)

## Important Constraints

- Flash erase/write cycles are limited (~10K-100K). `Param_Save()` triggers sector erase — batch writes call it once for the entire batch
- Interrupt safety: Comm_HAL uses `__disable_irq()`/`__enable_irq()` around ring buffer reads. Keep critical sections short
- `ModbusRTU_ProcessFrame` expects `len <= 256`. Frame buffer is 256 bytes
- RS485 direction pin polarity is configurable via `dir_polarity`
- Realtime data (`g_RealTimeData`) is RAM-only, **not persisted to Flash** — lost on power cycle
- Multi-register writes (0x10) reject batches that include `REG_CTRL_CMD` or `REG_FAULT_STATUS` — use single writes (0x06)
- ISOTP RX buffer is 8KB (`ISOTP_BUFFER_SIZE`), max message length 4095 bytes
- `FW_FLASH_WRITE_ENABLED=1` gates actual Flash writes; set to 0 for dry-run testing
- **Stack safety:** `flash_download.c` uses global `g_fw_ram_buffer[60KB]` — do not replace with stack allocation
- 1ms timing: `isotp_ms_update()`, `uds_ms_update()`, `isotp_tx_process()` must be called exactly once per millisecond, gated by `tickTimer_GetCount()` change detection
- **CAN debug macros** in main.c: `CAN_HEARTBEAT_ENABLE` (0/1), `CANIF_ECHO_ENABLE` (0/1), `UDS_CAN_ENABLE` (0/1) — currently all disabled in bootloader/APP1/APP2

## Known Security Findings

From `安全审查报告.md` (security audit, Chinese):

- **CRC write overflow (Critical):** `ModbusRTU_SendResponse` writes 2-byte CRC at `raw[len]` / `raw[len+1]` without checking `len + 2 <= 256`. The buffer is 256 bytes; `len` can reach 253, so it's currently safe by 1 byte, but lacks a defensive guard.
- **Unbounded `memcpy` (Critical):** `ModbusRTU_ProcessFrame` does `memcpy(m_stcTxFrame.raw, frame, len)` without checking `len <= 256`. The caller constrains `frameBuf` to 256 bytes so it's safe in practice, but the function itself is unprotected.
- **Send error reporting (High):** `Comm_HAL_Send` returns `bool` — on failure there's no distinction between "busy" and "queue full". Consider logging or an enum return type.

When touching Modbus/Comm_HAL code, add bounds checks rather than relying on caller-side constraints.

## Documentation

- `ota_ddl3.3_v.1.1/通信栈架构说明.md` — Full 4-layer Modbus communication stack (Chinese)
- `ota_ddl3.3_v.1.1/电流控制逻辑说明.md` — Over-current detection flow, dual blocking, fault recovery (Chinese)
- `ota_ddl3.3_v.1.1/实时数据使用说明.md` — Real-time data register map and usage (Chinese)
- `安全审查报告.md` — Security audit report (Chinese)
- `ota_ddl3.3_v.1.1/modbus_test_cmds.py` — Generates Modbus RTU hex command frames. Edit config at top, run `py modbus_test_cmds.py`
