
  #include "main.h"
  #include "Hardware.h"
  #include "rtt_log.h"
  #include "timer6_timebase.h"
  #include "Motor_hall.h"
  #include "TickTimer.h"
  #include "device_manager.h"
  #include "App_Motor_Project.h"
  #include "param_manager.h"
  #include "Gpio_io.h"
  #include "App_Comm.h"
  #include "Params.h"
  #include "App_FaultHandler.h"
  #include "rtt_manager.h"
  #include "Pwm.h"
  #include "hc32_ll_utility.h"
  #include "Adapter_Can.h"
  #include "isotp_transport.h"
  #include "uds_diagnostic.h"
  #include "flash_download.h"

  /* uds_dl_bridge.c 没有独立的 .h 文件，在此前向声明 */
  extern void uds_dl_init_fw(void);

  /*=============================================================================
   * ȫ��PWMʵ�������������ʹ�ã�???
   *=============================================================================*/
  pwm_t g_motor_pwm_ch1;  // PB6
  pwm_t g_motor_pwm_ch2;  // PB7
  pwm_t g_motor_pwm_ch3;  // PB8
  pwm_t g_motor_pwm_ch4;  // PB9

  /*=============================================================================
   * ��������
   *=============================================================================*/
  static void Motor_Pwm_Init(void);

  /*=============================================================================
   * ��ʼ����������õ�PWM��4��ͨ����ȫ������Ч��
   *=============================================================================*/
  static void Motor_Pwm_Init(void)
  {
      // ����������?4��ͨ��ȫ������Ч������תͨ��ռ�ձȷ���ʵ�֣�
      // Ƶ�ʣ�20kHz����ʼռ�ձȣ�0%

      // ����GPIO���裨�����޸�GPIO�������ã�
      LL_PERIPH_WE(LL_PERIPH_GPIO);

      // CH1: PB6 - ����Ч
      g_motor_pwm_ch1 = PWM_Init(CM_TMRA_4, FCG2_PERIPH_TMRA_4, TMRA_CH1,
                                  GPIO_PORT_B, GPIO_PIN_06, GPIO_FUNC_4,
                                  TMRA_MD_SAWTOOTH, TMRA_DIR_UP,
                                  6000, 0, PWM_ACTIVE_LOW);

      // CH2: PB7 - ����Ч
      g_motor_pwm_ch2 = PWM_Init(CM_TMRA_4, FCG2_PERIPH_TMRA_4, TMRA_CH2,
                                  GPIO_PORT_B, GPIO_PIN_07, GPIO_FUNC_4,
                                  TMRA_MD_SAWTOOTH, TMRA_DIR_UP,
                                  6000, 0, PWM_ACTIVE_LOW);

      // CH3: PB8 - ����Ч
      g_motor_pwm_ch3 = PWM_Init(CM_TMRA_4, FCG2_PERIPH_TMRA_4, TMRA_CH3,
                                  GPIO_PORT_B, GPIO_PIN_08, GPIO_FUNC_4,
                                  TMRA_MD_SAWTOOTH, TMRA_DIR_UP,
                                  6000, 0, PWM_ACTIVE_LOW);

      // CH4: PB9 - ����Ч
      g_motor_pwm_ch4 = PWM_Init(CM_TMRA_4, FCG2_PERIPH_TMRA_4, TMRA_CH4,
                                  GPIO_PORT_B, GPIO_PIN_09, GPIO_FUNC_4,
                                  TMRA_MD_SAWTOOTH, TMRA_DIR_UP,
                                  6000, 0, PWM_ACTIVE_LOW);

      // ����GPIO���裨������ú�������???
      LL_PERIPH_WP(LL_PERIPH_GPIO);

      // ����FCG���裨ʹ�ܶ�ʱ��ʱ�ӣ�
      LL_PERIPH_WE(LL_PERIPH_FCG);

      // ��������PWM��ʱ��
      PWM_Start(&g_motor_pwm_ch1);
      PWM_Start(&g_motor_pwm_ch2);
      PWM_Start(&g_motor_pwm_ch3);
      PWM_Start(&g_motor_pwm_ch4);

      // ʹ�����???
      PWM_OutputCmd(&g_motor_pwm_ch1, PWM_OUTPUT_ENABLE);
      PWM_OutputCmd(&g_motor_pwm_ch2, PWM_OUTPUT_ENABLE);
      PWM_OutputCmd(&g_motor_pwm_ch3, PWM_OUTPUT_ENABLE);
      PWM_OutputCmd(&g_motor_pwm_ch4, PWM_OUTPUT_ENABLE);

      // ����FCG����
      LL_PERIPH_WP(LL_PERIPH_FCG);

      MAIN_D("Motor PWM initialized: 4 channels, 20kHz, low active\r\n");
  }

  /*=============================================================================
   * 调试功能开关
   *=============================================================================*/

  /* CAN 心跳包：1=开启（每秒发送 0x12345678），0=关闭 */
  #define CAN_HEARTBEAT_ENABLE       (0U)

  /*=============================================================================
   * ISOTP/UDS 集成：CAN RX 回调和缓冲区
   *=============================================================================*/

  /* ISOTP 重组后的 UDS 消息输出缓冲区（最大 4096 字节） */
  static uint8_t s_uds_rx_buffer[4100];

  /*
   * ISOTP CAN RX 回调 — 由 CanIf 分发层在 RX 中断上下文外调用（main 循环中）。
   * 将 CAN 帧送入 ISOTP 重组，完成后分发给 UDS 诊断层。
   */
  static void ISOTP_RxCallback(const CanMsg_t *pMsg)
  {
      uint16_t out_len = 0;
      int8_t result = isotp_receive_frame(0, pMsg->u32ID,
                                          (uint8_t*)pMsg->au8Data, pMsg->u8DLC,
                                          s_uds_rx_buffer, &out_len);
      if (result == ISOTP_OK) {
          uds_receive_handler(0, pMsg->u32ID, s_uds_rx_buffer, out_len);
      }
  }

  /*
   * 注册 ISOTP 所需的 4 个 CAN ID 到 CanIf 过滤器。
   * ISOTP 过滤列表：0x18DA03F1, 0x18DAF103, 0x18FF8118, 0x18DBFFF0
   */
  static void ISOTP_RegisterRxFilters(void)
  {
      static const uint32_t s_isotp_can_ids[4] = {
          0x18DA03F1UL,  /* 物理寻址请求 ID (TBOX → 控制器) */
          0x18DAF103UL,  /* 物理寻址响应 ID (控制器 → TBOX) */
          0x18FF8118UL,  /* OTA 专用 ID */
          0x18DBFFF0UL   /* 功能寻址请求 ID (广播) */
      };

      CanIf_RxFilterEntry_t stcEntry;
      stcEntry.u32CanId   = 0UL;
      stcEntry.u32CanMask = 0UL;  /* 精确匹配 */
      stcEntry.u8Format   = CAN_ID_EXT;
      stcEntry.pfnCallback = &ISOTP_RxCallback;

      for (uint8_t i = 0U; i < 4U; i++) {
          stcEntry.u32CanId = s_isotp_can_ids[i];
          if (!CanIf_RegisterRxFilter(&stcEntry)) {
              MAIN_D("ISOTP: failed to register RX filter for CAN ID 0x%08X\r\n",
                     s_isotp_can_ids[i]);
          }
      }
      MAIN_D("ISOTP: 4 CAN ID RX filters registered\r\n");
  }

  /*=============================================================================
   * ������
   *=============================================================================*/
  int main(void)
  {
      Hardware_Init();

      /*=========================================================================
       * UDS 诊断栈初始化（CAN → ISOTP → UDS → FlashDownload）
       * 依赖：Hardware_Init() 中已调用 CanIf_Init()
       *=========================================================================*/
      MAIN_D("=== UDS Stack Init Start ===\r\n");

      /* 1. 初始化 ISOTP 传输层（CAN channel 0） */
      isotp_init(0);

      /* 2. 注册 ISOTP 的 4 个 CAN ID RX 过滤器 */
      ISOTP_RegisterRxFilters();

      /* 3. 初始化固件下载模块 */
      FlashDownloadConfig_t stcFwConfig = {
          .max_firmware_size     = 256UL * 1024UL,    /* 最大 256KB */
          .flash_sector_size    = 0x2000UL,           /* 8KB 扇区 */
          .user_start_addr      = 0x00034000UL,       /* 应用区起始地址 */
          .user_end_addr        = 0x00054000UL,       /* 应用区结束地址 */
          .verify_enabled       = 1U,                 /* 启用校验 */
          .auto_reset_on_complete = 0U                /* 不自动复位 */
      };
      FlashDownload_Init(&stcFwConfig);

      /* 4. 注册固件下载接口到 UDS 诊断层 */
      uds_dl_init_fw();

      /* 5. 初始化 UDS 诊断服务 */
      uds_init();

      MAIN_D("=== UDS Stack Init Done ===\r\n");

    //   /* ͨ��ջ��ʼ�� (RS485 + Modbus RTU) */
    //   static const App_Comm_Config_t comm_cfg = {
    //       .phy.baudrate     = 9600,
    //       .phy.dir_polarity = 0,
    //       .hal.rx_buf_size  = 500,
    //       .hal.tx_buf_size  = 500,
    //       .hal.rx_frame_queue_depth = 10,
    //       .hal.tx_queue_depth       = 10,
    //       .hal.frame_timeout_ms     = 0,
    //       .proto.node_id            = 1,
    //       .proto.enable_write_multi = true,
    //   };
    //   App_Comm_Init(&comm_cfg);

    //   ESystem_Init();

    //   /* ��ʼ�����ϴ����������ĵ�ѹ/�����¼������¹����룩 */
    //   FaultHandler_Init();

    //   /* ��ʼ�����PWM���ڵ���豸��ʼ��֮ǰ��??? */
    //   Motor_Pwm_Init();

    //   /*=========================================================================
    //    * �������ģʽ����������Keil Watch�������޸ģ�
    //    * 0: ֹͣ, 1: ��ת, 2: ��ת
    //    *=========================================================================*/
    //   // volatile uint8_t motor_mode = 0;

    //   // MotorDevice_t* motor = NULL;       // TODO: ��ȡ����豸ָ��???
    //   EventBus_Enable();
	
      /* 用于 1ms 节拍门控的上一 tick 值 */
      {
          static uint64_t s_last_ms_tick = 0;

          while (1)
          {
            //   ESystem_MainLoop();
            //   App_Comm_Poll();

              // 更新PWM状态（无变化时不操作）
            //   PWM_Update(&g_motor_pwm_ch1);
            //   PWM_Update(&g_motor_pwm_ch2);
            //   PWM_Update(&g_motor_pwm_ch3);
            //   PWM_Update(&g_motor_pwm_ch4);

              // // 每轮循环根据 motor_mode 设定对应方向
              // if (motor_mode == 0) {
              //     Motor_OnArbitrationStop(motor);
              // } else if (motor_mode == 1) {
              //     Motor_OnArbitrationFwd(motor, 0.0f);
              // } else if (motor_mode == 2) {
              //     Motor_OnArbitrationRev(motor, 0.0f);
              // }

              /* =============================================================
               * 1ms 节拍门控：TMR0_Unit2 ISR → tickTimer_Update() 每 1ms
               * 递增 s_tickCount。仅在 tick 变化时调用 _ms_update() 函数，
               * 保证 ISOTP/UDS 内部超时计数与实际时间一致。
               * ===========================================================*/
              {
                  uint64_t current_tick = tickTimer_GetCount();
                  if (current_tick != s_last_ms_tick) {
                      s_last_ms_tick = current_tick;
                      isotp_ms_update();     /* ISOTP RX/TX 超时管理 */
                      uds_ms_update();       /* UDS 会话超时、安全访问延迟 */
                      isotp_tx_process();    /* ISOTP TX 连续帧流控发送 */
                  }
              }

              FlashDownload_Task();    /* 固件下载状态机（每轮都可执行） */

#if CAN_HEARTBEAT_ENABLE
              /* Heartbeat: 1s interval, ID=0x12345678 */
              {
                  static NonBlockingDelay_t s_stcHbt;
                  static bool s_bHbtInited = false;
                  if (!s_bHbtInited) {
                      nbDelay_Init(&s_stcHbt, 1000U);
                      nbDelay_Start(&s_stcHbt);
                      s_bHbtInited = true;
                  }
                  if (nbDelay_IsComplete(&s_stcHbt)) {
                      CanMsg_t stcMsg;
                      uint8_t au8Data[8] = {0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78};
                      uint8_t i;
                      stcMsg.u32ID = 0x12345678UL;
                      stcMsg.u8IDE  = 1U;
                      stcMsg.u8RTR  = 0U;
                      stcMsg.u8FDF  = 0U;
                      stcMsg.u8BRS  = 0U;
                      stcMsg.u8DLC  = 8U;
                      for (i = 0U; i < 8U; i++) {
                          stcMsg.au8Data[i] = au8Data[i];
                      }
                      CanIf_Send(&stcMsg);
                      nbDelay_Start(&s_stcHbt);
                  }
              }
#endif

              /* CAN 接口轮询：分发 RX 帧到已注册的过滤器回调（包括 ISOTP） */
              CanIf_Poll();

            //   Param_PrintAllValues();
          }
      }
  }
  