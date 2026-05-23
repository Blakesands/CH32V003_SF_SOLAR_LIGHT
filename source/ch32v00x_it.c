/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32v00x_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/25
 * Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include <ch32v00x_it.h>

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
//void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      EXTI7_0_IRQHandler
 * @brief   Handles PC2 Button Interrupt (EXTI Line 2)
 */
//void EXTI7_0_IRQHandler(void)
//{
//    static uint32_t press_start = 0;
//    static uint32_t last_interrupt_time = 0;
//    static uint8_t level_idx = 0;
//    static const uint16_t levels[] = {100, 200, 400, 650, 950};
//    const uint8_t num_levels = sizeof(levels) / sizeof(levels[0]);
//
//    if(EXTI_GetITStatus(EXTI_Line4) != RESET)
//    {
//        uint32_t now = SysTick->CNT; // CH32V003 internal 64-bit counter (simplified)
//
//        // Basic software debounce: ignore triggers within 20ms of each other
//        if ((now - last_interrupt_time) > (SystemCoreClock / 50))
//        {
//            if(GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_4) == Bit_RESET)
//            {
//                // Falling Edge (Press)
//                press_start = now;
//            }
//            else
//            {
//                // Rising Edge (Release)
//                uint32_t duration = now - press_start;
//                if(duration > (SystemCoreClock))
//                {
//                    // Optional: Long press action (e.g., turn off or reset)
//                    PWM_LED( 0 );
//                }
//                else if (duration > (SystemCoreClock / 20))
//                {
//                    // Short Press: Cycle through levels
//                    level_idx = (level_idx + 1) % num_levels;
//
//                    PWM_LED( levels[level_idx] );
//                }
//            }
//            last_interrupt_time = now;
//        }
//        EXTI_ClearITPendingBit(EXTI_Line4);
//    }
//}
/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
  NVIC_SystemReset();
  while (1)
  {
  }
}
