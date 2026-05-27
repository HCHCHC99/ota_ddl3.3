#ifndef __ADAPTER_CAN_H__
#define __ADAPTER_CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "hc32_ll.h"

/* CAN unit */
#define CAN_UNIT                (CM_CAN)
#define CAN_PERIPH_CLK          (FCG1_PERIPH_CAN)

/* CAN pin definitions: PB14=RXCAN, PB15=TXCAN */
#define CAN_TX_PORT             (GPIO_PORT_B)
#define CAN_TX_PIN              (GPIO_PIN_15)
#define CAN_TX_PIN_FUNC         (GPIO_FUNC_50)

#define CAN_RX_PORT             (GPIO_PORT_B)
#define CAN_RX_PIN              (GPIO_PIN_14)
#define CAN_RX_PIN_FUNC         (GPIO_FUNC_51)

/*
 * CAN baudrate: 250k bit/s
 * CAN Clock = 8MHz, Prescaler=4, TimeSeg1=6, TimeSeg2=2
 * Baudrate = CANClock / (Prescaler * (TimeSeg1 + TimeSeg2))
 *          = 8MHz / (4 * 8) = 250kHz
 * Sample Point = (1 + TimeSeg1) / (1 + TimeSeg1 + TimeSeg2) = 7/9 ≈ 77.8%
 */
#define CAN_PRESCALER           (4U)
#define CAN_TIME_SEG1           (6U)
#define CAN_TIME_SEG2           (2U)
#define CAN_SJW                 (2U)

/* CAN interrupt priority */
#define CAN_INT_PRIO            (DDL_IRQ_PRIO_03)

void Can_Init(void);
int32_t Can_Send(uint32_t u32ID, uint8_t u8IDE, uint8_t *pu8Data, uint8_t u8DLC);
int32_t Can_Recv(stc_can_rx_frame_t *pstcRxFrame);

#ifdef __cplusplus
}
#endif

#endif /* __ADAPTER_CAN_H__ */
