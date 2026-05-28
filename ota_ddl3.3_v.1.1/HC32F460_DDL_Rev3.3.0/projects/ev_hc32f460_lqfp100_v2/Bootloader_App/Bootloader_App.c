#include "Bootloader_App.h"

// ###########################################################################
//
//                          ##############################
//                          #                            #
//                          #    ����ʽ�㡿��ʽ���ܴ���    #
//                          #                            #
//                          ##############################
//
// ������Bootloader���ġ�Flash��WDT����ת������RAM
//
// ###########################################################################

// ==================== ����ȫ�ֱ����������ã� ====================
volatile uint32_t g_u32Debug_ClearAppState = 3;

// ==================== �ڲ���̬�������� ====================
static en_wdt_reset_type_t GetWdtResetType(void);
static en_slot_type_t GetCurrentSlot(void);
static void ValidateSlotFlag(stc_boot_context_t *pstcCtx);
static void InitAppInfo(stc_app_info_t *pstcApp, en_slot_type_t eSlot, uint32_t u32Addr);
static void UpdateAppState(stc_app_info_t *pstcApp);
static void HandleWatchdogReset(stc_boot_context_t *pstcCtx);
static void SelectTargetSlot(stc_boot_context_t *pstcCtx);
static void UpdateSlotFlagToFlash(stc_boot_context_t *pstcCtx);
static en_boot_status_t GetBootStatus(stc_boot_context_t *pstcCtx);
static void ShowBootStatus(en_boot_status_t eStatus);
static void RunBootloaderForever(void);
static void CheckAndClearAppState(void);

// ###########################################################################
//                          �������ߺ���
// ###########################################################################
uint32_t READ_FLASH_DIRECT(uint32_t addr)
{
    uint32_t value;
    uint32_t u32FrmcState;

    u32FrmcState = CM_EFM->FRMC;
    CM_EFM->FRMC = (u32FrmcState & ~EFM_FRMC_CACHE);
    __DSB();
    value = *((volatile uint32_t *)addr);
    __DMB();
    CM_EFM->FRMC = u32FrmcState;

    return value;
}

void Bootloader_Delay(uint32_t u32Count)
{
    while (u32Count-- > 0) { __nop(); }
}

void DisableAllNVICInterrupts(void)
{
    uint32_t i;
    for(i = 0; i < 128; i++)
    {
        NVIC_DisableIRQ((IRQn_Type)i);
    }
    for(i = 0; i < 4; i++)
    {
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
}

// ###########################################################################
//                          ����RAM ��ʼ��
// ###########################################################################
void InitSharedCtrl(void)
{
    stc_shared_ctrl_t *pCtrl = GetSharedCtrl();
    if (pCtrl->app1_feed_ctrl != WDT_FEED_ENABLE &&
        pCtrl->app1_feed_ctrl != WDT_FEED_DISABLE)
    {
        pCtrl->app1_feed_ctrl = WDT_FEED_ENABLE;
        pCtrl->app2_feed_ctrl = WDT_FEED_ENABLE;
        pCtrl->debug_flag = 0;
        for (int i = 0; i < 5; i++) pCtrl->reserved[i] = 0;
    }
}

// ###########################################################################
//                          Flash ����
// ###########################################################################
int32_t Bootloader_FlashEraseSector(uint32_t u32Addr)
{
    int32_t res;
    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    res = EFM_SectorErase(u32Addr);
    EFM_REG_Lock();
    return res;
}

// ###########################################################################
//                          ���Ź�״̬����
// ###########################################################################
uint32_t GetWdtResetCount(uint32_t u32Addr)
{
    uint32_t u32Count = READ_FLASH_DIRECT(u32Addr);
    return (u32Count == 0xFFFFFFFF) ? 0 : u32Count;
}

void UpdateWdtResetCount(uint32_t u32Addr, uint32_t u32CurrentCount)
{
    uint32_t u32SectorBase, u32CountAddr, u32FeedAddr;
    uint32_t u32Count, u32Feed;

    if (u32Addr == WDT_COUNT_APP1_ADDR) {
        u32SectorBase = APP1_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP1_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP1_ADDR;
    } else if (u32Addr == WDT_COUNT_APP2_ADDR) {
        u32SectorBase = APP2_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP2_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP2_ADDR;
    } else return;

    u32Count = READ_FLASH_DIRECT(u32CountAddr);
    u32Feed = READ_FLASH_DIRECT(u32FeedAddr);
    u32Count = (u32Count == 0xFFFFFFFF) ? 0 : u32Count;
    u32Feed = (u32Feed == 0xFFFFFFFF) ? WDT_FEED_ENABLE : u32Feed;

    if (u32Count < MAX_WDT_RESET_COUNT) u32Count++;

    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(u32SectorBase);
    EFM_ProgramWord(u32FeedAddr, u32Feed);
    EFM_ProgramWord(u32CountAddr, u32Count);
    EFM_REG_Lock();
}

void ClearWdtResetCount(uint32_t u32Addr)
{
    uint32_t u32SectorBase, u32CountAddr, u32FeedAddr;
    uint32_t u32Count, u32Feed;

    if (u32Addr == WDT_COUNT_APP1_ADDR) {
        u32SectorBase = APP1_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP1_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP1_ADDR;
    } else if (u32Addr == WDT_COUNT_APP2_ADDR) {
        u32SectorBase = APP2_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP2_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP2_ADDR;
    } else return;

    u32Count = READ_FLASH_DIRECT(u32CountAddr);
    u32Feed = READ_FLASH_DIRECT(u32FeedAddr);
    u32Count = (u32Count == 0xFFFFFFFF) ? 0 : u32Count;
    u32Feed = (u32Feed == 0xFFFFFFFF) ? WDT_FEED_ENABLE : u32Feed;

    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(u32SectorBase);
    EFM_ProgramWord(u32FeedAddr, u32Feed);
    EFM_ProgramWord(u32CountAddr, 0);
    EFM_REG_Lock();
}

void SetWdtFeedControl(uint32_t u32Addr, uint32_t u32Value)
{
    uint32_t u32SectorBase, u32CountAddr, u32FeedAddr;
    uint32_t u32Count, u32Feed;

    if (u32Value != WDT_FEED_ENABLE && u32Value != WDT_FEED_DISABLE) return;

    if (u32Addr == WDT_FEED_CONTROL_APP1_ADDR) {
        u32SectorBase = APP1_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP1_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP1_ADDR;
    } else if (u32Addr == WDT_FEED_CONTROL_APP2_ADDR) {
        u32SectorBase = APP2_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP2_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP2_ADDR;
    } else return;

    u32Count = READ_FLASH_DIRECT(u32CountAddr);
    u32Feed = READ_FLASH_DIRECT(u32FeedAddr);
    u32Count = (u32Count == 0xFFFFFFFF) ? 0 : u32Count;
    u32Feed = u32Value;

    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(u32SectorBase);
    EFM_ProgramWord(u32FeedAddr, u32Feed);
    EFM_ProgramWord(u32CountAddr, u32Count);
    EFM_REG_Lock();
}

uint32_t GetWdtFeedControl(uint32_t u32Addr)
{
    uint32_t u32Value = READ_FLASH_DIRECT(u32Addr);
    return (u32Value == 0xFFFFFFFF) ? WDT_FEED_ENABLE : u32Value;
}

void ClearAppStateBySlot(en_slot_type_t eSlot)
{
    uint32_t u32SectorBase, u32CountAddr, u32FeedAddr, u32Feed;

    if (eSlot == SLOT_APP1) {
        u32SectorBase = APP1_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP1_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP1_ADDR;
    } else if (eSlot == SLOT_APP2) {
        u32SectorBase = APP2_STATE_SECTOR_BASE;
        u32CountAddr = WDT_COUNT_APP2_ADDR;
        u32FeedAddr = WDT_FEED_CONTROL_APP2_ADDR;
    } else return;

    u32Feed = READ_FLASH_DIRECT(u32FeedAddr);
    u32Feed = (u32Feed == 0xFFFFFFFF) ? WDT_FEED_ENABLE : u32Feed;

    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(u32SectorBase);
    EFM_ProgramWord(u32FeedAddr, u32Feed);
    EFM_ProgramWord(u32CountAddr, 0);
    EFM_REG_Lock();
}

// ###########################################################################
//                          Ӧ����ת & ��λ�л�
// ###########################################################################
void Bootloader_JumpToApp(uint32_t u32AppAddr)
{
    uint32_t app_start_address;
    app_start_address = *(uint32_t *)(u32AppAddr + 4);
    if (app_start_address == 0xFFFFFFFF) return;

    // 1. �������RAM��bootloader��APP��������
    ClearAllRAM();
    
    // 2. ֹͣSysTick
    SysTick->CTRL = 0;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk; 

    // 3. ���������ж�
    __disable_irq();
    DisableAllNVICInterrupts();
    
    // 4. ����ж�ʹ�ܺ͹���Ĵ���
    for (uint8_t i = 0; i < 8; i++) 
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // 5. ����ջָ���������
    __set_MSP(*(uint32_t *)u32AppAddr);
    SCB->VTOR = ((uint32_t)u32AppAddr & SCB_VTOR_TBLOFF_Msk);
    
    __DSB();
    __ISB();

    // 6. ��ת��APP
    (*((void(*)(void))app_start_address))();
}

// RAM��պ������ؼ�ʵ�֣�
void ClearAllRAM(void)
{
    volatile uint32_t *ram_addr = (volatile uint32_t *)RAM_START_ADDR;
    uint32_t ram_word_count = RAM_SIZE / 4;  // ��32λ�����
    
    // ������գ�Ч�ʸߣ�
    for (uint32_t i = 0; i < ram_word_count; i++) 
    {
        ram_addr[i] = 0;
    }
    
    // ȷ�������ɣ��ڴ����ϣ�
    __DSB();
    __ISB();
}

void APP_SwitchAndRunOther(void)
{
    uint32_t curr = READ_FLASH_DIRECT(APP_RUN_SLOT_ADDR);
    uint32_t target = (curr == SLOT_A_MAGIC) ? SLOT_B_MAGIC : SLOT_A_MAGIC;

    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
    while(SET != EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(APP_RUN_SLOT_ADDR);
    EFM_ProgramWord(APP_RUN_SLOT_ADDR, target);
    EFM_REG_Lock();

    NVIC_SystemReset();
}

// ###########################################################################
//                          Bootloader ������
// ###########################################################################
void Bootloader_Init(void) {}

void Bootloader_Main(void)
{
    stc_boot_context_t stcCtx;
    memset(&stcCtx, 0, sizeof(stc_boot_context_t));

    stc_shared_ctrl_t *pSharedCtrl = GetSharedCtrl();
    if (pSharedCtrl->debug_flag == 0x5A5A5A5A) {
        SetWdtFeedControl(WDT_FEED_CONTROL_APP1_ADDR, pSharedCtrl->app1_feed_ctrl);
        SetWdtFeedControl(WDT_FEED_CONTROL_APP2_ADDR, pSharedCtrl->app2_feed_ctrl);
        pSharedCtrl->debug_flag = 0;
    }

    CheckAndClearAppState();
    stcCtx.eWdtResetType = GetWdtResetType();
    stcCtx.eCurrentSlot = GetCurrentSlot();
    ValidateSlotFlag(&stcCtx);

    InitAppInfo(&stcCtx.stcApp1, SLOT_APP1, APP1_START_ADDR);
    InitAppInfo(&stcCtx.stcApp2, SLOT_APP2, APP2_START_ADDR);
    UpdateAppState(&stcCtx.stcApp1);
    UpdateAppState(&stcCtx.stcApp2);
    HandleWatchdogReset(&stcCtx);
    SelectTargetSlot(&stcCtx);
    UpdateSlotFlagToFlash(&stcCtx);

    ShowBootStatus(GetBootStatus(&stcCtx));

    if (stcCtx.eTargetSlot == SLOT_APP1)      Bootloader_JumpToApp(APP1_START_ADDR);
    else if (stcCtx.eTargetSlot == SLOT_APP2) Bootloader_JumpToApp(APP2_START_ADDR);
    else RunBootloaderForever();
}

// ###########################################################################
//                          �ڲ���̬����
// ###########################################################################
static en_wdt_reset_type_t GetWdtResetType(void) {
    if ( SET == RMU_GetStatus(RMU_FLAG_SWDT)) return WDT_RESET_SWDT;
    if ( SET == RMU_GetStatus(RMU_FLAG_WDT)) return WDT_RESET_WDT;
    return WDT_RESET_NONE;
}

static en_slot_type_t GetCurrentSlot(void) {
    uint32_t s = READ_FLASH_DIRECT(APP_RUN_SLOT_ADDR);
    if (s == SLOT_A_MAGIC) return SLOT_APP1;
    if (s == SLOT_B_MAGIC) return SLOT_APP2;
    return SLOT_NONE;
}

static void ValidateSlotFlag(stc_boot_context_t *pstcCtx) {
    if (pstcCtx->eCurrentSlot == SLOT_NONE) {
        pstcCtx->eCurrentSlot = SLOT_APP1;
        pstcCtx->u8NeedUpdateSlotFlag = 1;
    }
}

static void InitAppInfo(stc_app_info_t *pstcApp, en_slot_type_t eSlot, uint32_t u32Addr) {
    pstcApp->eSlot = eSlot;
    pstcApp->u32StartAddr = u32Addr;
    uint32_t cnt = (eSlot == SLOT_APP1) ? READ_FLASH_DIRECT(WDT_COUNT_APP1_ADDR) : READ_FLASH_DIRECT(WDT_COUNT_APP2_ADDR);
    pstcApp->u32WdtCount = (cnt == 0xFFFFFFFF) ? 0 : cnt;
    pstcApp->eState = APP_STATE_AVAILABLE;
}

static void UpdateAppState(stc_app_info_t *pstcApp) {
    pstcApp->eState = (pstcApp->u32WdtCount >= MAX_WDT_RESET_COUNT) ? APP_STATE_DISABLED : APP_STATE_AVAILABLE;
}

static void HandleWatchdogReset(stc_boot_context_t *pstcCtx) {
    if (pstcCtx->eWdtResetType == WDT_RESET_NONE) return;
    stc_app_info_t *app = NULL;
    uint32_t addr = 0;

    if (pstcCtx->eCurrentSlot == SLOT_APP1) {
        app = &pstcCtx->stcApp1; addr = WDT_COUNT_APP1_ADDR;
    } else if (pstcCtx->eCurrentSlot == SLOT_APP2) {
        app = &pstcCtx->stcApp2; addr = WDT_COUNT_APP2_ADDR;
    } else return;

    if (app->eState == APP_STATE_AVAILABLE) {
        UpdateWdtResetCount(addr, 0);
        app->u32WdtCount = (app->eSlot == SLOT_APP1) ? GetWdtResetCount(WDT_COUNT_APP1_ADDR) : GetWdtResetCount(WDT_COUNT_APP2_ADDR);
        if (app->u32WdtCount >= MAX_WDT_RESET_COUNT) app->eState = APP_STATE_DISABLED;
    }
}

static void SelectTargetSlot(stc_boot_context_t *pstcCtx) {
    uint8_t aok = (pstcCtx->stcApp1.eState == APP_STATE_AVAILABLE);
    uint8_t bok = (pstcCtx->stcApp2.eState == APP_STATE_AVAILABLE);

    if (pstcCtx->eCurrentSlot == SLOT_APP1 && aok) pstcCtx->eTargetSlot = SLOT_APP1;
    else if (pstcCtx->eCurrentSlot == SLOT_APP2 && bok) pstcCtx->eTargetSlot = SLOT_APP2;
    else if (bok) { pstcCtx->eTargetSlot = SLOT_APP2; pstcCtx->u8NeedUpdateSlotFlag = 1; }
    else if (aok) { pstcCtx->eTargetSlot = SLOT_APP1; pstcCtx->u8NeedUpdateSlotFlag = 1; }
    else pstcCtx->eTargetSlot = SLOT_NONE;
}

static void UpdateSlotFlagToFlash(stc_boot_context_t *pstcCtx) {
    if (!pstcCtx->u8NeedUpdateSlotFlag || pstcCtx->eTargetSlot == SLOT_NONE) return;
    uint32_t val = (pstcCtx->eTargetSlot == SLOT_APP1) ? SLOT_A_MAGIC : SLOT_B_MAGIC;

    EFM_REG_Unlock(); EFM_FWMC_Cmd(ENABLE); while(!EFM_GetStatus(EFM_FLAG_RDY));
    EFM_SectorErase(APP_RUN_SLOT_ADDR);
    EFM_ProgramWord(APP_RUN_SLOT_ADDR, val);
    EFM_REG_Lock();
    pstcCtx->u8NeedUpdateSlotFlag = 0;
}

static en_boot_status_t GetBootStatus(stc_boot_context_t *pstcCtx) {
    uint8_t aok = (pstcCtx->stcApp1.eState == APP_STATE_AVAILABLE);
    uint8_t bok = (pstcCtx->stcApp2.eState == APP_STATE_AVAILABLE);
    if (!aok && !bok) return BOOT_STATUS_BOTH_DISABLED;
    if (!aok) return BOOT_STATUS_APP1_DISABLED;
    if (!bok) return BOOT_STATUS_APP2_DISABLED;
    return BOOT_STATUS_NORMAL;
}

static void ShowBootStatus(en_boot_status_t eStatus) {
    stc_gpio_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcPortInit);
    stcPortInit.u16PinDir = PIN_DIR_OUT;
    GPIO_Init(GPIO_PORT_B, GPIO_PIN_06, &stcPortInit);
    uint8_t cnt; uint32_t dly;

    switch(eStatus) {
        case BOOT_STATUS_NORMAL: cnt=2; dly=50; break;
        case BOOT_STATUS_APP1_DISABLED: cnt=3; dly=50; break;
        case BOOT_STATUS_APP2_DISABLED: cnt=4; dly=50; break;
        default: cnt=5; dly=200000; break;
    }

    for (uint8_t i=0; i<cnt; i++) {
        GPIO_TogglePins(GPIO_PORT_B, GPIO_PIN_06); Bootloader_Delay(dly);
        GPIO_TogglePins(GPIO_PORT_B, GPIO_PIN_06); Bootloader_Delay(dly);
    }
}

static void RunBootloaderForever(void) {
    stc_gpio_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcPortInit);
    stcPortInit.u16PinDir = PIN_DIR_OUT;
    GPIO_Init(GPIO_PORT_B, GPIO_PIN_06, &stcPortInit);
    while(1) { GPIO_TogglePins(GPIO_PORT_B, GPIO_PIN_06); Bootloader_Delay(200000); }
}

static void CheckAndClearAppState(void) {
    if (g_u32Debug_ClearAppState == 1) ClearAppStateBySlot(SLOT_APP1);
    else if (g_u32Debug_ClearAppState == 2) ClearAppStateBySlot(SLOT_APP2);
    else if (g_u32Debug_ClearAppState == 3) { ClearAppStateBySlot(SLOT_APP1); ClearAppStateBySlot(SLOT_APP2); }
}

// ###########################################################################
//
//                          ##################################################
//                          #                                                #
//                          #       ������ʾ���㡿APP1 / APP2 ҵ�����         #
//                          #       ��������ʾ����˸LED�������л�             #
//                          #                                                #
//                          ##################################################
//
// ˵�����ⲿ������ʾ��ҵ����ʽ��Ʒ���滻Ϊ��ʵҵ�����
//
// ###########################################################################

// ###########################
//  APP1 ���Դ���
// ###########################
volatile uint8_t g_u8Debug_JumpOther = 0;
volatile uint32_t g_u32DrvStatus = 0;

#define TIME0_IRQ_PRIORITY		(7UL)
#define TIME0_IRQN          	(INT009_IRQn)
#define TIME_GCM_NUM			(INT_SRC_TMR0_1_CMP_B)

#define USEC_TO_COUNT(us, clk) (uint16_t)(((uint64_t)(us)*clk)/1000000U)

static void Timer01B_CallBack(void);
static void timer0_Init(uint32_t us);
static void SystemClk_Init(void);

static void Timer01B_CallBack(void)
{
    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_B);
    GPIO_TogglePins(GPIO_PORT_B, GPIO_PIN_07);
}

static void timer0_Init(uint32_t us)
{
    stc_tmr0_init_t t;
    stc_irq_signin_config_t i;
    MEM_ZERO_STRUCT(t); MEM_ZERO_STRUCT(i);
    stc_clock_freq_t tmp; CLK_GetClockFreq(&tmp);
    uint32_t pclk = tmp.u32Pclk1Freq;

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR0_1, ENABLE);
    t.u32Func = TMR0_FUNC_CMP;
    t.u32ClockSrc = TMR0_CLK_SRC_INTERN_CLK;
    t.u32ClockDiv = TMR0_CLK_DIV64;
    t.u16CompareValue = USEC_TO_COUNT(us, pclk/64) - 1;
    TMR0_Init(CM_TMR0_1, TMR0_CH_B, &t);

    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_B);
    TMR0_IntCmd(CM_TMR0_1, TMR0_INT_CMP_B, ENABLE);

    i.enIRQn = TIME0_IRQN;
    i.enIntSrc = TIME_GCM_NUM;
    i.pfnCallback = Timer01B_CallBack;
    INTC_IrqSignIn(&i);

    NVIC_ClearPendingIRQ(TIME0_IRQN);
    NVIC_SetPriority(TIME0_IRQN, TIME0_IRQ_PRIORITY);
    NVIC_EnableIRQ(TIME0_IRQN);
    TMR0_Start(CM_TMR0_1, TMR0_CH_B);
}

static void SystemClk_Init(void)
{
    CLK_SetClockDiv(CLK_BUS_CLK_ALL,
        CLK_HCLK_DIV1  | CLK_EXCLK_DIV2 |
        CLK_PCLK0_DIV1 | CLK_PCLK1_DIV2 |
        CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
        CLK_PCLK4_DIV2);

    CLK_HrcCmd(ENABLE);
    CLK_SetPLLSrc(CLK_PLL_SRC_HRC);
    EFM_REG_Unlock(); EFM_SetWaitCycle(EFM_WAIT_CYCLE5); EFM_REG_Lock();
    while(CLK_GetStableStatus(CLK_STB_FLAG_HRC) != SET);
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_HRC);
}

void APP1_Main(void)
{
    stc_gpio_init_t p;
    uint32_t wdt;

    RMU_ClearStatus();
    MEM_ZERO_STRUCT(p); p.u16PinDir = PIN_DIR_OUT;
    GPIO_Init(GPIO_PORT_B, GPIO_PIN_07, &p);

    stc_shared_ctrl_t *share = GetSharedCtrl();
    SystemClk_Init();
    timer0_Init(1000);
    __enable_irq();

    while(1) {
        if (g_u8Debug_JumpOther) { g_u8Debug_JumpOther=0; APP_SwitchAndRunOther(); }
        wdt = share->app1_feed_ctrl;
        if (wdt == WDT_FEED_ENABLE) SWDT_FeedDog();
        if (g_u32DrvStatus != 0) APP_SwitchAndRunOther();
    }
}

// ###########################
//  APP2 ���Դ���
// ###########################
volatile uint8_t g_u8Debug_JumpOther_APP2 = 0;
volatile uint32_t g_u32DrvStatus_APP2 = 0;

#define TIME0_IRQ_PRIORITY_APP2		(5UL)
#define TIME0_IRQN_APP2             (INT009_IRQn)
#define TIME_GCM_NUM_APP2			(INT_SRC_TMR0_1_CMP_B)

static void Timer01B_CallBack_APP2(void);
static void timer0_Init_APP2(uint32_t us);
static void SystemClk_Init_APP2(void);

static void Timer01B_CallBack_APP2(void)
{
    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_B);
    GPIO_TogglePins(GPIO_PORT_B, GPIO_PIN_06);
}

static void timer0_Init_APP2(uint32_t us)
{
    stc_tmr0_init_t t;
    stc_irq_signin_config_t i;
    MEM_ZERO_STRUCT(t); MEM_ZERO_STRUCT(i);
    stc_clock_freq_t tmp; CLK_GetClockFreq(&tmp);
    uint32_t pclk = tmp.u32Pclk1Freq;

    FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR0_1, ENABLE);
    t.u32Func = TMR0_FUNC_CMP;
    t.u32ClockSrc = TMR0_CLK_SRC_INTERN_CLK;
    t.u32ClockDiv = TMR0_CLK_DIV64;
    t.u16CompareValue = USEC_TO_COUNT(us, pclk/64) - 1;
    TMR0_Init(CM_TMR0_1, TMR0_CH_B, &t);

    TMR0_ClearStatus(CM_TMR0_1, TMR0_FLAG_CMP_B);
    TMR0_IntCmd(CM_TMR0_1, TMR0_INT_CMP_B, ENABLE);

    i.enIRQn = TIME0_IRQN_APP2;
    i.enIntSrc = TIME_GCM_NUM_APP2;
    i.pfnCallback = Timer01B_CallBack_APP2;
    INTC_IrqSignIn(&i);

    NVIC_ClearPendingIRQ(TIME0_IRQN_APP2);
    NVIC_SetPriority(TIME0_IRQN_APP2, TIME0_IRQ_PRIORITY_APP2);
    NVIC_EnableIRQ(TIME0_IRQN_APP2);
    TMR0_Start(CM_TMR0_1, TMR0_CH_B);
}

static void SystemClk_Init_APP2(void)
{
    CLK_SetClockDiv(CLK_BUS_CLK_ALL,
        CLK_HCLK_DIV1  | CLK_EXCLK_DIV2 |
        CLK_PCLK0_DIV1 | CLK_PCLK1_DIV2 |
        CLK_PCLK2_DIV4 | CLK_PCLK3_DIV4 |
        CLK_PCLK4_DIV2);

    CLK_HrcCmd(ENABLE);
    CLK_SetPLLSrc(CLK_PLL_SRC_HRC);
    EFM_REG_Unlock(); EFM_SetWaitCycle(EFM_WAIT_CYCLE5); EFM_REG_Lock();
    while(CLK_GetStableStatus(CLK_STB_FLAG_HRC) != SET);
    CLK_SetSysClockSrc(CLK_SYSCLK_SRC_HRC);
}

void APP2_Main(void)
{
    stc_gpio_init_t p;
    uint32_t wdt;

    RMU_ClearStatus();
    MEM_ZERO_STRUCT(p); p.u16PinDir = PIN_DIR_OUT;
    GPIO_Init(GPIO_PORT_B, GPIO_PIN_06, &p);

    stc_shared_ctrl_t *share = GetSharedCtrl();
    SystemClk_Init_APP2();
    timer0_Init_APP2(1000);
    __enable_irq();

    while(1) {
        if (g_u8Debug_JumpOther_APP2) { g_u8Debug_JumpOther_APP2=0; APP_SwitchAndRunOther(); }
        wdt = share->app2_feed_ctrl;
        if (wdt == WDT_FEED_ENABLE) SWDT_FeedDog();
        if (g_u32DrvStatus_APP2 != 0) APP_SwitchAndRunOther();
    }
}
