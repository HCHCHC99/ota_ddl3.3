#ifndef FLASH_ADVANCED_H_
#define FLASH_ADVANCED_H_

#include <stdint.h>
#include <stddef.h>
#include "hc32f46x_flash.h"

// ============ Flash管理记录使能宏 ============
// 定义此宏以启用Flash管理记录（擦写记录持久化）
#define FLASH_MANAGEMENT_RECORD_ENABLE

// ============ Flash扇区配置（统一管理） ============
// 扇区大小（8KB = 8192字节）
#define FLASH_ADV_SECTOR_SIZE            0x2000

// 总扇区数（512KB / 8KB = 64）
#define FLASH_ADV_TOTAL_SECTORS          64

// ============ 扇区区域划分 ============
// 保护扇区定义（不可被用户擦写）
#define FLASH_ADV_PROTECTED_SECTOR_START 0     // 保护扇区起始（扇区0）
#define FLASH_ADV_PROTECTED_SECTOR_END   5     // 保护扇区结束（扇区5）
#define FLASH_ADV_PROTECTED_SECTOR       63    // 单个保护扇区（扇区63，兼容旧代码）

// 管理记录扇区配置（扇区5，地址范围 0x0000A000 - 0x0000BFFF）
#define FLASH_ADV_MANAGEMENT_SECTOR      5     // 管理记录扇区号（属于保护区域）
#define FLASH_ADV_MANAGEMENT_SECTOR_ADDR (FLASH_ADV_MANAGEMENT_SECTOR * FLASH_ADV_SECTOR_SIZE)  // 0x0000A000
#define FLASH_ADV_MANAGEMENT_MAGIC       0x5A5A5A5A
#define FLASH_ADV_MANAGEMENT_VERSION     0x00000001

// 有效扇区范围（用户可用区域）
#define FLASH_ADV_VALID_SECTOR_START     6     // 有效起始扇区（扇区6，地址 0x0000C000）
#define FLASH_ADV_VALID_SECTOR_END       62    // 有效结束扇区（扇区62）

// 用户可用区域地址计算
#define FLASH_ADV_USER_START_ADDR        (FLASH_ADV_VALID_SECTOR_START * FLASH_ADV_SECTOR_SIZE)   // 0x0000C000
#define FLASH_ADV_USER_END_ADDR          ((FLASH_ADV_VALID_SECTOR_END + 1) * FLASH_ADV_SECTOR_SIZE) // 0x0007E000
#define FLASH_ADV_USER_SIZE              ((FLASH_ADV_VALID_SECTOR_END - FLASH_ADV_VALID_SECTOR_START + 1) * FLASH_ADV_SECTOR_SIZE)

// ============ 保护检查宏（统一接口） ============
// 检查扇区是否在保护区域内
#define FLASH_ADV_IS_SECTOR_PROTECTED(sector) \
    (((sector) >= FLASH_ADV_PROTECTED_SECTOR_START && (sector) <= FLASH_ADV_PROTECTED_SECTOR_END) || \
     (sector) == FLASH_ADV_PROTECTED_SECTOR)

// 检查地址是否在保护区域内（通过扇区号判断）
#define FLASH_ADV_IS_ADDRESS_PROTECTED(addr) \
    FLASH_ADV_IS_SECTOR_PROTECTED((addr) / FLASH_ADV_SECTOR_SIZE)

// 检查扇区是否在用户可用区域内
#define FLASH_ADV_IS_SECTOR_VALID(sector) \
    ((sector) >= FLASH_ADV_VALID_SECTOR_START && (sector) <= FLASH_ADV_VALID_SECTOR_END)

// 检查地址是否在用户可用区域内
#define FLASH_ADV_IS_USER_ADDRESS(addr) \
    ((addr) >= FLASH_ADV_USER_START_ADDR && (addr) < FLASH_ADV_USER_END_ADDR)

// 检查扇区是否可写（在有效区域内且不在保护区域内）
#define FLASH_ADV_IS_SECTOR_WRITABLE(sector) \
    (FLASH_ADV_IS_SECTOR_VALID(sector) && !FLASH_ADV_IS_SECTOR_PROTECTED(sector))

// 检查地址是否可写
#define FLASH_ADV_IS_ADDRESS_WRITABLE(addr) \
    (FLASH_ADV_IS_USER_ADDRESS(addr) && !FLASH_ADV_IS_ADDRESS_PROTECTED(addr))

// 获取扇区起始地址
#define FLASH_ADV_GET_SECTOR_START(addr) \
    ((addr) & ~(FLASH_ADV_SECTOR_SIZE - 1))

// 获取地址所在的扇区号
#define FLASH_ADV_GET_SECTOR_INDEX(addr) \
    ((addr) / FLASH_ADV_SECTOR_SIZE)

// ============ 1. 基础状态定义 ============
typedef enum {
    FLASH_ADV_OK = 0,
    FLASH_ADV_ERROR = 1,
    FLASH_ADV_BUSY = 2,
    FLASH_ADV_TIMEOUT = 3,
    FLASH_ADV_ADDR_INVALID = 4,
    FLASH_ADV_PROTECTED = 5,
    FLASH_ADV_WRITE_MISMATCH = 6
} FlashAdvStatus_t;

// ============ 2. 配置参数 ============
typedef struct {
    uint32_t base_address;      // Flash基地址
    uint32_t sector_size;       // 扇区大小
    uint32_t total_size;        // 总大小
    uint32_t protected_size;    // 保护区域大小（从基地址开始）
    uint32_t max_erase_cycles;  // 最大擦除次数（用于寿命计算）
} FlashAdvConfig_t;

// ============ 3. 操作类型 ============
typedef enum {
    OP_TYPE_NONE = 0,
    OP_TYPE_ERASE_SECTOR,
    OP_TYPE_WRITE_WORD,
    OP_TYPE_WRITE_BULK,
    OP_TYPE_READ_WORD
} FlashAdvOpType_t;

// ============ 4. 操作历史记录 ============
#define FLASH_ADV_HISTORY_SIZE 16

typedef struct {
    FlashAdvOpType_t op_type;
    uint32_t address;
    uint32_t data;
    FlashAdvStatus_t status;
    uint32_t sequence_num;
    uint32_t timestamp_ms;
} FlashAdvHistoryRecord_t;

// ============ 5. 统计信息 ============
typedef struct {
    uint32_t erase_count;       // 擦除次数
    uint32_t write_count;       // 写入次数（字数）
    uint32_t read_count;        // 读取次数
    uint32_t error_count;       // 错误次数
    uint32_t total_operations;  // 总操作次数
    uint32_t consecutive_errors;// 连续错误次数
    uint32_t retry_count;       // 重试次数
} FlashAdvStatistics_t;

// ============ 6. 寿命信息 ============
#define MAX_FLASH_SECTORS_ADV   64   // 64个扇区，每个8KB

typedef struct {
    uint32_t magic;                     // 魔数，用于验证数据有效性
    uint32_t version;                   // 版本号
    uint32_t max_erase_cycles;          // 最大擦除寿命
    uint32_t warning_threshold;         // 警告阈值（百分比）
    uint32_t current_max_erase;         // 当前最大擦除次数
    uint32_t current_min_erase;         // 当前最小擦除次数
    uint32_t total_erase_count;         // 总擦除次数（所有扇区之和）
    uint32_t sector_size;               // 扇区大小（字节）= 8192
    uint32_t sector_erase_counts[MAX_FLASH_SECTORS_ADV];  // 各扇区擦除次数
    uint16_t sector_count;              // 扇区数量 = 64
    uint8_t health_percent;             // 整体健康度百分比
    uint32_t valid_start_sector;        // 有效起始扇区
    uint32_t valid_end_sector;          // 有效结束扇区
    uint32_t checksum;                  // 校验和
} FlashAdvLifetimeInfo_t;

// ============ 7. 批量写入参数 ============
typedef struct {
    uint32_t start_address;     // 起始地址
    const uint32_t* data;       // 数据指针
    uint32_t word_count;        // 字数（4字节为单位）
    uint8_t verify_enabled;     // 是否启用验证
    uint8_t retry_on_error;     // 错误时是否重试
} FlashAdvBulkWriteParams_t;

// ============ 8. 高级Flash句柄 ============
typedef struct FlashAdvHandle FlashAdvHandle_t;

// ============ 9. 底层操作函数指针 ============
typedef struct {
    FlashAdvStatus_t (*erase_sector)(uint32_t address);
    FlashAdvStatus_t (*write_word_check)(uint32_t address, uint32_t data);
    uint32_t (*read_word)(uint32_t address);
    FlashAdvStatus_t (*wait_done)(uint32_t timeout_us);
} FlashAdvOps_t;

// ============ 10. 公共接口函数 ============

// 创建/销毁实例
FlashAdvHandle_t* FlashAdv_Create(const FlashAdvConfig_t* config, const FlashAdvOps_t* ops);
void FlashAdv_Destroy(FlashAdvHandle_t* handle);

// 基础操作
FlashAdvStatus_t FlashAdv_EraseSector(FlashAdvHandle_t* handle, uint32_t address);
FlashAdvStatus_t FlashAdv_WriteWord(FlashAdvHandle_t* handle, uint32_t address, uint32_t data);
uint32_t FlashAdv_ReadWord(FlashAdvHandle_t* handle, uint32_t address);

// 高级功能：批量写入
FlashAdvStatus_t FlashAdv_BulkWrite(FlashAdvHandle_t* handle, const FlashAdvBulkWriteParams_t* params);
FlashAdvStatus_t FlashAdv_BulkWriteSimple(FlashAdvHandle_t* handle, 
                                           uint32_t start_address, 
                                           const uint32_t* data, 
                                           uint32_t word_count);

// 获取统计信息
FlashAdvStatus_t FlashAdv_GetStatistics(FlashAdvHandle_t* handle, FlashAdvStatistics_t* stats);
void FlashAdv_ResetStatistics(FlashAdvHandle_t* handle);

// 获取寿命信息
FlashAdvStatus_t FlashAdv_GetLifetimeInfo(FlashAdvHandle_t* handle, FlashAdvLifetimeInfo_t* info);

// 保存/加载寿命信息到Flash
FlashAdvStatus_t FlashAdv_SaveLifetimeInfo(FlashAdvHandle_t* handle);
FlashAdvStatus_t FlashAdv_LoadLifetimeInfo(FlashAdvHandle_t* handle);

// 获取操作历史
FlashAdvStatus_t FlashAdv_GetHistory(FlashAdvHandle_t* handle, 
                                      FlashAdvHistoryRecord_t* out_buffer, 
                                      uint32_t buffer_size, 
                                      uint32_t* num_returned);

// 获取最新一次操作记录
FlashAdvStatus_t FlashAdv_GetLastOperation(FlashAdvHandle_t* handle, FlashAdvHistoryRecord_t* record);

// 清除历史记录
void FlashAdv_ClearHistory(FlashAdvHandle_t* handle);

// 保护区域检查
uint8_t FlashAdv_IsAddressProtected(FlashAdvHandle_t* handle, uint32_t address);

// 地址有效性检查
uint8_t FlashAdv_IsValidAddress(FlashAdvHandle_t* handle, uint32_t address);

// 扇区地址对齐
uint32_t FlashAdv_GetSectorStart(FlashAdvHandle_t* handle, uint32_t address);

// 获取指定扇区的擦除次数
uint32_t FlashAdv_GetSectorEraseCount(FlashAdvHandle_t* handle, uint32_t sector_index);

// 获取指定地址所在扇区的擦除次数
uint32_t FlashAdv_GetAddressEraseCount(FlashAdvHandle_t* handle, uint32_t address);

// 重置寿命统计
void FlashAdv_ResetLifetime(FlashAdvHandle_t* handle);

// 检查扇区是否可写
uint8_t FlashAdv_IsSectorWritable(FlashAdvHandle_t* handle, uint32_t sector_index);

// 获取有效扇区范围
void FlashAdv_GetValidSectorRange(FlashAdvHandle_t* handle, uint32_t* start, uint32_t* end);

// ============ 11. 测试函数 ============
uint32_t flash_advanced_test(void);

// ============ 12. 用户区域接口（供上层模块使用） ============

// 获取用户可用Flash区域
void FlashAdv_GetUserRegion(uint32_t *start_addr, uint32_t *end_addr, uint32_t *size);

// 检查地址是否在用户可用区域内（使用宏定义）
static inline uint8_t FlashAdv_IsUserAddress(uint32_t address)
{
    return FLASH_ADV_IS_USER_ADDRESS(address);
}

// 获取扇区大小
static inline uint32_t FlashAdv_GetSectorSize(void)
{
    return FLASH_ADV_SECTOR_SIZE;
}

// 获取有效扇区范围（扇区号）
void FlashAdv_GetValidSectorRangeNum(uint32_t *start_sector, uint32_t *end_sector);

// 获取保护扇区范围
static inline void FlashAdv_GetProtectedSectorRange(uint32_t *start_sector, uint32_t *end_sector)
{
    if (start_sector) *start_sector = FLASH_ADV_PROTECTED_SECTOR_START;
    if (end_sector) *end_sector = FLASH_ADV_PROTECTED_SECTOR_END;
}

// 检查地址是否在保护区域内
static inline uint8_t FlashAdv_IsAddressProtectedByMacro(uint32_t address)
{
    return FLASH_ADV_IS_ADDRESS_PROTECTED(address);
}

#endif /* FLASH_ADVANCED_H_ */
