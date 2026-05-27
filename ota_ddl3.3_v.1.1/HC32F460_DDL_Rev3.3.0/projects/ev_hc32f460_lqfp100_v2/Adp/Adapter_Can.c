#include "Adapter_Can.h"

/*==============================================================================
 * Local definitions
 *============================================================================*/
/* Acceptance filter: accept all frames */
#define CAN_FILTER_NUM          (1U)
#define CAN_FILTER_SEL          (CAN_FILTER1)

/* Interrupt selection */
#define CAN_INT_SEL             (CAN_INT_PTB_TX | CAN_INT_RX | CAN_INT_ERR_INT)

#define CAN_INT_SRC             (INT_SRC_CAN_INT)
#define CAN_INT_IRQn            (INT122_IRQn)

/*==============================================================================
 * Local variables
 *============================================================================*/
static stc_can_rx_frame_t m_stcRxFrame;          /* Received frame buffer (ISR -> main loop) */
static __IO uint8_t m_u8RxFlag = 0U;             /* 1 = new frame available in m_stcRxFrame */
static __IO uint8_t m_u8TxBusy = 0U;             /* 1 = PTB transmission in progress */

/*==============================================================================
 * Local function prototypes
 *============================================================================*/
static void CanPinConfig(void);
static void CanInitConfig(void);
static void CanIrqConfig(void);
static void CAN_IrqCallback(void);

/*==============================================================================
 * Public functions
 *============================================================================*/

/*
 * Initialize CAN: PB14=RX, PB15=TX, 250kbps, normal mode
 * Call once from main() after Hardware_Init()
 */
void Can_Init(void)
{
    LL_PERIPH_WE(LL_PERIPH_GPIO | LL_PERIPH_FCG);

    CanPinConfig();
    CanInitConfig();
    CanIrqConfig();

    LL_PERIPH_WP(LL_PERIPH_GPIO | LL_PERIPH_FCG);
}

/*
 * Send a CAN frame via PTB (primary transmit buffer).
 * Non-blocking: returns LL_ERR if previous TX still in progress.
 * u32ID: 11-bit std or 29-bit ext ID
 * u8IDE: 0=standard frame, 1=extended frame
 * pu8Data: pointer to data bytes
 * u8DLC: data length (0-8)
 */
int32_t Can_Send(uint32_t u32ID, uint8_t u8IDE, uint8_t *pu8Data, uint8_t u8DLC)
{
    stc_can_tx_frame_t stcTx;
    uint8_t i;

    if (m_u8TxBusy != 0U) {
        return LL_ERR;
    }

    stcTx.u32ID = u32ID;
    stcTx.IDE   = u8IDE;
    stcTx.RTR   = 0U;
    stcTx.BRS   = 0U;
    stcTx.FDF   = 0U;
    stcTx.DLC   = u8DLC;
    for (i = 0U; i < u8DLC; i++) {
        stcTx.au8Data[i] = pu8Data[i];
    }

    (void)CAN_FillTxFrame(CAN_UNIT, CAN_TX_BUF_PTB, &stcTx);
    CAN_StartTx(CAN_UNIT, CAN_TX_REQ_PTB);
    m_u8TxBusy = 1U;

    return LL_OK;
}

/*
 * Receive a CAN frame (non-blocking).
 * Returns LL_OK and fills *pstcRxFrame if a frame is available.
 * Returns LL_ERR if no frame available.
 */
int32_t Can_Recv(stc_can_rx_frame_t *pstcRxFrame)
{
    if (m_u8RxFlag == 0U) {
        return LL_ERR;
    }

    *pstcRxFrame = m_stcRxFrame;
    m_u8RxFlag = 0U;

    return LL_OK;
}

/*==============================================================================
 * Local functions
 *============================================================================*/

/* Configure CAN TX/RX pins */
static void CanPinConfig(void)
{
    GPIO_SetFunc(CAN_TX_PORT, CAN_TX_PIN, CAN_TX_PIN_FUNC);
    GPIO_SetFunc(CAN_RX_PORT, CAN_RX_PIN, CAN_RX_PIN_FUNC);
}

/* Configure CAN baudrate (250kbps) and acceptance filters */
static void CanInitConfig(void)
{
    stc_can_init_t stcCanInit;
    stc_can_filter_config_t astcFilter[CAN_FILTER_NUM] = {
        {0UL, CAN_EXT_ID_MASK, CAN_ID_STD_EXT},     /* Accept all frames: mask=all-1s means ignore all ID bits */
    };

    (void)CAN_StructInit(&stcCanInit);
    stcCanInit.stcBitCfg.u32Prescaler = CAN_PRESCALER;
    stcCanInit.stcBitCfg.u32TimeSeg1  = CAN_TIME_SEG1;
    stcCanInit.stcBitCfg.u32TimeSeg2  = CAN_TIME_SEG2;
    stcCanInit.stcBitCfg.u32SJW       = CAN_SJW;
    stcCanInit.pstcFilter             = astcFilter;
    stcCanInit.u16FilterSelect        = CAN_FILTER_SEL;
    stcCanInit.u8WorkMode             = CAN_WORK_MD_NORMAL;

    FCG_Fcg1PeriphClockCmd(CAN_PERIPH_CLK, ENABLE);
    (void)CAN_Init(CAN_UNIT, &stcCanInit);
    CAN_IntCmd(CAN_UNIT, CAN_INT_ALL, DISABLE);
    CAN_IntCmd(CAN_UNIT, CAN_INT_SEL, ENABLE);
}

/* Configure CAN interrupt */
static void CanIrqConfig(void)
{
    stc_irq_signin_config_t stcIrq;

    stcIrq.enIntSrc    = CAN_INT_SRC;
    stcIrq.enIRQn      = CAN_INT_IRQn;
    stcIrq.pfnCallback = &CAN_IrqCallback;
    (void)INTC_IrqSignIn(&stcIrq);
    NVIC_ClearPendingIRQ(stcIrq.enIRQn);
    NVIC_SetPriority(stcIrq.enIRQn, CAN_INT_PRIO);
    NVIC_EnableIRQ(stcIrq.enIRQn);
}

/* CAN interrupt callback */
static void CAN_IrqCallback(void)
{
    uint32_t u32Status;

    u32Status = CAN_GetStatusValue(CAN_UNIT);
    if (u32Status != 0U) {
        CAN_ClearStatus(CAN_UNIT, u32Status);
    }

    if ((u32Status & CAN_FLAG_PTB_TX) != 0U) {
        m_u8TxBusy = 0U;
    }

    if ((u32Status & CAN_FLAG_RX) != 0U) {
        /* Read frame from hardware RX buffer into local buffer immediately */
        if (CAN_GetRxFrame(CAN_UNIT, &m_stcRxFrame) == LL_OK) {
            m_u8RxFlag = 1U;
        }
    }

    if ((u32Status & CAN_FLAG_ERR_INT) != 0U) {
        if ((u32Status & CAN_FLAG_BUS_OFF) != 0U) {
            CAN_ExitLocalReset(CAN_UNIT);
        }
    }
}
