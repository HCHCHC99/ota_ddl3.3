# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Motor control system based on HC32F460 (Cortex-M4) with dual communication stacks: RS485/Modbus RTU and CAN/UDS (ISO 14229) with firmware-over-the-air (OTA) download. Controls DC motors with Hall sensor feedback, over-current/voltage detection, rotation angle limiting, and fault handling.

All source code lives under `ota_ddl3.3_v.1.1/HC32F460_DDL_Rev3.3.0/`. The base is Huada Semiconductor's HC32F460 DDL (Device Driver Library) Rev3.3.0.

## GBK Encoding Warning

**All `.c` and `.h` files in this project use GBK encoding** (Chinese comments). Never edit them directly — use `iconv` to/from UTF-8:

```bash
iconv -f GBK -t UTF-8 file.c > file_utf8.c   # before editing
iconv -f UTF-8 -t GBK file_utf8.c > file.c    # after editing
```

## Build System

- **IDE:** Keil MDK (uVision 5)
- **Project file:** `ota_ddl3.3_v.1.1/HC32F460_DDL_Rev3.3.0/projects/ev_hc32f460_lqfp100_v2/template/MDK/template.uvprojx`
- **MCU:** HC32F460xE (512KB Flash) — linker script is `template/MDK/config/linker/HC32F460xE.sct`
- **Debug probe:** JLink (Cortex-M4)
- **Build output:** `template/MDK/output/debug/` (`.axf`, `.hex`, `.bin`, `.map`)
- Open the `.uvprojx` in Keil, click Build (F7), or use `UV4.exe -r template.uvprojx -o output.txt` from command line
- IAR EWARM and Eclipse/GCC project files also exist under `template/EWARM/` and `template/GCC/` but are not actively maintained

## Source Tree

```
projects/ev_hc32f460_lqfp100_v2/
├── Adp/          # Hardware adapter layer (rs485, CAN, PWM, ADC, DMA, GPIO, timers, flash)
├── App/          # Application logic (motor control, Modbus, fault, realtime)
├── Dev/          # Device drivers (motor, ADC, hall, voltage, sensor, EventBus, DeviceManager)
├── Utils/        # Utilities (ring_buf, msg_queue, lock, param_manager, TickTimer, RTT manager)
├── UDS/          # UDS diagnostic stack over CAN (ISO 14229 + ISO 15765-2 + OTA download)
├── RTT/          # SEGGER RTT debug output
└── template/
    ├── source/   # main.c, main.h, hc32f4xx_conf.h
    └── MDK/      # Keil project, startup, linker scripts, JLink config
```

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
- Protocol layer knows nothing about register meanings; it calls `on_read`/`on_write`/`on_validate` callbacks
- Comm_HAL knows nothing about Modbus; it just assembles byte streams into frames by idle timeout
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

**OTA download path (UDS 0x34→0x36→0x37):**

```
UDS/uds_diagnostic.c      — Handles 0x34/0x36/0x37 service IDs, calls dl_if->on_request_download() etc.
    ↓ calls (via function pointer table)
UDS/uds_dl_if.h           — Abstract download interface (uds_dl_if_t struct of function pointers)
    ↓ implemented by
UDS/uds_dl_bridge.c       — Bridge: converts uds_dl_if_t calls → FlashDownload_* calls
    ↓ calls
UDS/flash_download.c/h    — Firmware download state machine (erase, write, verify, CRC)
    ↓ uses
Adp/flash_advanced.c/h    — Advanced Flash wrapper (sector erase, write without blocking)
Adp/hc32f46x_flash.c/h    — Low-level EFM Flash register operations
```

- **CAN ID filter:** ISOTP filters 4 CAN IDs: `0x18DA03F1` (phys request), `0x18DAF103` (phys response), `0x18FF8118` (OTA), `0x18DBFFF0` (functional broadcast)
- **Address mapping:** TBOX addresses `0x08004000-0x08054000` are mapped to Flash `0x00034000-0x00054000` via `MAP_TBOX_ADDR_TO_FLASH()`
- **Flash write gate:** `FW_FLASH_WRITE_ENABLED` (1=real write, 0=dry run) in flash_download.h
- **Security:** Seed/key algorithm (`seedkey_calc_lv1_key`) in security_access.c, 3-attempt lockout

## Key Architecture Patterns

- **EventBus** (`Dev/EventBus.h`): Publish/subscribe for inter-module communication. 14 topics including `TOPIC_MANUAL_IO` (motor commands), `TOPIC_VOLTAGE_ALARM`, `TOPIC_CURRENT_ALARM`, `TOPIC_RTURN_LIMIT`. Uses deferred publish — events published before `EventBus_Enable()` are queued as a bitmask and replayed on enable. Max 4 subscribers per topic, priority-ordered (0 = highest).

- **DeviceManager** (`Dev/device_manager.h`): Uniform device registry with time-sliced update scheduling. Each device gets an `update()` callback called at its configured interval (typically 1ms, voltage bus at 10ms). Mutex-protected access. 16 devices registered (motor arbitrator, power outputs, Hall switches, IO buttons, PWM, ADC channels, sensors).

- **Param Manager** (`Utils/param_manager.h`): Register-based parameter storage with Flash persistence. Parameters live in `g_AppParam` (type `AppParamRecord_t`). Read/write via `Param_ReadByReg()`/`Param_WriteByReg()`, save via `Param_Save()`. Uses wear-leveled Flash storage across sectors 56-62 with CRC32 validation, sequence numbers, and magic headers.

- **Motor arbitration** (`Dev/dev_motor.c`): Commands go through the motor arbitrator which decides whether to allow based on mode (auto/remote/manual). Uses a `block_fwd`/`block_rev` bitmask — multiple devices can independently block a direction (e.g., overcurrent adds `DEV_ID_OVERCUR_FWD`, limit switches add `DEV_ID_RTURN_FWD`). Arbitration re-evaluates on every block/unblock.

- **Simulation mode** (`ENABLE_SIMULATION_MODE=1` in `App_Motor_Project.c`): Enabled by default. The `g_sim` struct holds simulated hardware signals (power, Hall limits, IO buttons, ADC values). The main loop detects state changes on `g_sim` and publishes corresponding EventBus events, allowing full motor arbitration testing without physical hardware.

- **CAN RX dispatch:** `CanIf_RegisterRxFilter()` registers (CAN ID, mask, callback) entries. On RX, CanIf iterates registered filters and dispatches to matching callback. Unmatched frames go to optional default callback. Used by ISOTP to claim its 4 CAN IDs and by main.c for heartbeat test.

- **Download interface abstraction:** `uds_dl_if_t` is a vtable of 14 function pointers. `uds_dl_bridge.c` implements it by delegating to `FlashDownload_*` functions and registers via `uds_dl_register()`. This decouples UDS protocol from the specific download implementation (currently firmware Flash, could be swapped for eMMC, external memory, etc.).

- **SEGGER RTT** (`Utils/rtt_manager.h`): Debug logging via `MAIN_D()`, `COMM_DBG()`, `HAL_DEBUG()`, `ISOTP_D()`, `DL_D()` macros — these output through RTT channel, not UART.

- **1ms tick gating** (`main.c`): The main loop runs faster than 1ms, but ISOTP/UDS timeout counters must tick at exactly 1ms intervals. The `TMR0_Unit2_IRQHandler` → `tickTimer_Update()` increments a global tick counter every 1ms. In the main loop, `tickTimer_GetCount()` is compared against a saved last-tick value — `isotp_ms_update()`, `uds_ms_update()`, and `isotp_tx_process()` are only called when the tick value changes. This prevents timeout drift that would occur if those functions were called every loop iteration.

- **CAN debug macros** (`main.c` line ~103, `Adapter_Can.c` line ~10):
  - `CAN_HEARTBEAT_ENABLE` (0/1) — Enables a 1-second CAN heartbeat packet (ID `0x12345678`) for bus health monitoring. Currently **disabled** (0).
  - `CANIF_ECHO_ENABLE` (0/1) — When enabled, unmatched CAN frames are echoed back on the bus. Currently **disabled** (0) to avoid bus noise.

## Fault System

Fault bits (stored in `g_RealTimeData.fault_status`, readable via Modbus register 0x2740):

| Bit | Macro | Description |
|-----|-------|-------------|
| bit0 | `FAULT_BIT_OVERVOLTAGE` | Overvoltage |
| bit2 | `FAULT_BIT_OVERCURRENT` | Overcurrent |
| bit4 | `FAULT_BIT_OVERTEMP` | Overtemp |
| bit6 | `FAULT_BIT_UNDERVOLTAGE` | Undervoltage |
| bit5 | `FAULT_BIT_STALL` | Motor stall |

- `App_FaultHandler` subscribes to `TOPIC_VOLTAGE_ALARM` and `TOPIC_CURRENT_ALARM`, sets/clears fault bits in realtime data
- Overcurrent triggers **dual blocking**: dev_motor blocks forward via `DEV_ID_OVERCUR_FWD`, dev_rturn also blocks via `TOPIC_RTURN_LIMIT` for redundancy
- **Auto-clear mode**: faults clear automatically when the alarm condition resolves
- **Manual-clear mode**: faults persist until cleared via Modbus write to `REG_FAULT_STATUS` (0x2740), which calls `FaultHandler_ClearFault()`

## Control Commands (REG_CTRL_CMD = 0x2720)

Bits written via Modbus function 0x06 (single write only):

| Bit | Value | Description |
|-----|-------|-------------|
| bit0 | 0x0001 | START — enable RS485 control |
| bit1 | 0x0002 | STOP — disable RS485 control, stop motor |
| bit2 | 0x0004 | ESTOP — emergency stop (motor stops, control stays enabled) |
| bit3 | 0x0008 | RESET — `__NVIC_SystemReset()` after 200ms delay |
| bit4 | 0x0010 | FWD — forward (requires START first, uses `g_AppParam.target_speed`) |
| bit5 | 0x0020 | REV — reverse (requires START first, uses `g_AppParam.target_speed`) |

Typical sequence: START (0x0001) → FWD (0x0011) → STOP (0x0002)

## Important Constraints

- Flash erase/write cycles are limited (~10K-100K). Each `Param_Save()` triggers a sector erase. Avoid calling it per-register in multi-register writes (0x10) — the batch write path calls `Param_Save()` once for the entire batch
- Interrupt safety: Comm_HAL uses `__disable_irq()`/`__enable_irq()` around ring buffer reads. Keep critical sections short
- `ModbusRTU_ProcessFrame` expects `len <= 256`. Frame buffer is 256 bytes. Modbus RTU max frame is 256 bytes so this is safe
- RS485 direction pin polarity is configurable via `dir_polarity` (0 = high-TX/low-RX, 1 = low-TX/high-RX)
- Realtime data (`g_RealTimeData`) is RAM-only, **not persisted to Flash** — lost on power cycle
- Multi-register writes (0x10) reject batches that include `REG_CTRL_CMD` or `REG_FAULT_STATUS` — those must use single writes (0x06)
- ISOTP RX buffer is 8KB (`ISOTP_BUFFER_SIZE`), max message length 4095 bytes (12-bit FF length field)
- ISOTP TX uses configurable BlockSize (`tx_bs`) and STmin (`tx_st_min`) for flow-controlled consecutive frame pacing
- Flash download uses 60KB RAM buffer (`FW_RAM_BUFFER_SIZE`) to accumulate firmware before writing
- `FW_FLASH_WRITE_ENABLED=1` gates actual Flash writes; set to 0 for dry-run testing
- **Stack safety:** `flash_download.c` uses a global `g_fw_ram_buffer[60KB]` for Flash write buffering instead of a stack-local array. The stack on this MCU (~192KB total SRAM, stack at top of RAM1) cannot safely hold a 60KB local variable. Do not replace this with a stack allocation
- 1ms timing: `isotp_ms_update()`, `uds_ms_update()`, `isotp_tx_process()` must be called exactly once per millisecond, gated by `tickTimer_GetCount()` change detection. Calling them at the main loop rate (10-1000x faster) would cause timeout counters to expire prematurely

## Documentation

- `ota_ddl3.3_v.1.1/通信栈架构说明.md` — Full 4-layer Modbus communication stack explanation (Chinese)
- `ota_ddl3.3_v.1.1/电流控制逻辑说明.md` — Over-current detection flow, dual blocking, fault recovery (Chinese)
- `ota_ddl3.3_v.1.1/实时数据使用说明.md` — Real-time data register map and usage (Chinese)
- `安全审查报告.md` — Security audit report (Chinese) — CRC bounds, memcpy sizing, Flash write frequency concerns
- `ota_ddl3.3_v.1.1/modbus_test_cmds.py` — Generates Modbus RTU hex command frames for read (0x03), write (0x06), multi-write (0x10), fault clear, and control commands. Edit the config at the top of the script and run `py modbus_test_cmds.py`
