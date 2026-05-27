#include <ch32v00x.h>

static uint8_t level_idx = 0;
static const uint16_t brightness[] = {0, 50, 300, 900};
const uint8_t num_levels = sizeof(brightness) / sizeof(brightness[0]);

volatile uint32_t ms_ticks = 0;

volatile uint8_t is_pressing = 0;
volatile uint32_t press_start = 0;
volatile uint32_t timer_expiry = 0;
volatile uint32_t last_level_change_tick = 0;
#define ONE_HOUR_MS 3600000UL

/* Battery Monitoring Globals - Compares Vdd to 1.2V internal reference*/
volatile uint8_t battery_status = 0; // 0: OK, 1: Low (<3V), 2: Critical (<2.7V)
#define VDD_LOW_THRESHOLD      409   // (1.2V / 3V) * 1023
#define VDD_CRITICAL_THRESHOLD 444   // (1.2V / 2.7V) * 1023
#define VDD_RECOVERY_THRESHOLD 361   // (1.2V / 3.4V) * 1023
static uint8_t batt_state = 0;       // 0: Idle, 1: Waiting for ADC stabilisation
static uint32_t batt_timer = 0;      // Initialized properly in main()

/* Solar Monitoring */
#define CHARGE_RESISTOR_OHMS 2
volatile int16_t charge_current_ma = 0;
volatile uint8_t is_charging = 0;

/* Forward Declarations */
void PWM_LED(u16 duty);
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_Init_Tick(void);
uint32_t GetTick(void);
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void AWU_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void IWDG_Config(void);
void Button_Init(void);
void LED_Init(void);
void ADC_Init_Vdd(void);
uint16_t Get_ADC_Val(uint8_t ch);
uint8_t Check_Battery(void);
void Enter_Deep_Sleep(void);
void Post_System_Setup(void);

int main(void){

    /* Configure IWDG first - if init fails, system resets */
    IWDG_Config();

    SystemCoreClockUpdate();
    LED_Init();
    TIM2_Init_Tick();
    ADC_Init_Vdd();

    /* Configure AWU Interrupt in PFIC and EXTI to allow wake-up from Standby */
    // These are persistent configurations and should be set once.
    {
        NVIC_InitTypeDef NVIC_InitStructure = {0};
        NVIC_InitStructure.NVIC_IRQChannel = AWU_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        EXTI->INTENR |= EXTI_Line9; // Auto-Wakeup is routed to EXTI Line 9
    }

    Button_Init();
    __enable_irq();
    
    /* Wait for power rail and internal reference stabilization on cold boot (approx 100ms) */
    for(volatile uint32_t i=0; i<400000; i++); 
    Post_System_Setup(); // Perform initial setup and battery check
    while(1){
        IWDG_ReloadCounter(); /* Feed the watchdog every loop iteration */

        /* 1. Periodic Battery Check State Machine (Non-blocking) */
        if (batt_state == 0) {
            if (GetTick() - batt_timer > 10000) {
                ADC_Cmd(ADC1, ENABLE); // Turn on ADC to stabilise
                batt_timer = GetTick();
                batt_state = 1;
            }
        } else if (batt_state == 1) {
            if (GetTick() - batt_timer >= 1) { // 1ms stabilisation wait via ms_ticks
                battery_status = Check_Battery(); 
                ADC_Cmd(ADC1, DISABLE); // Shut down ADC immediately to save power
                
                /* If battery is low, cap current brightness to level 2 (300) */
                if (battery_status == 1 && level_idx > 2) {
                    level_idx = 2;
                    PWM_LED(brightness[level_idx]);
                    last_level_change_tick = GetTick();
                }
                batt_timer = GetTick();
                batt_state = 0;
            }
        }

        /* 2. Critical Battery Shutdown Handling */
        static uint32_t shutdown_tick = 0;
        if (battery_status == 2) {
            // If charging, do not enter shutdown handling. Let the charge blip run (via TIM2_IRQHandler).
            if (is_charging) {
                shutdown_tick = 0; // Reset shutdown timer
                // The main loop will then hit __WFI() and stay in shallow sleep.
            } else { // Battery is critical and not charging
                // If main light is off, immediately enter deep sleep without flashing
                if (level_idx == 0) {
                    shutdown_tick = 0; // Reset for next time
                    Enter_Deep_Sleep();
                    continue; // Skip the rest of the loop iteration
                }

                // If light is on, flash the critical battery LED for a short period, then enter deep sleep
                if (shutdown_tick == 0) {
                    shutdown_tick = GetTick();
                    PWM_LED(0);   // Stop main light immediately
                    level_idx = 0; // Reset state
                    last_level_change_tick = GetTick();
                }

                uint32_t elapsed = GetTick() - shutdown_tick;
                if (elapsed < 480) { /* 3 cycles of (60ms ON, 100ms OFF) = 480ms */
                    GPIO_WriteBit(GPIOC, GPIO_Pin_2, (elapsed % 160 < 60) ? Bit_SET : Bit_RESET);
                } else {
                    IWDG_ReloadCounter(); // Final feed before sleep
                    shutdown_tick = 0;
                    Enter_Deep_Sleep();
                    continue; /* Re-evaluate battery after wake-up button press */
                }
            }
        } else {
            shutdown_tick = 0;
        }

        /* Check if the user timer has expired */
        if (timer_expiry != 0 && (int32_t)(GetTick() - timer_expiry) >= 0)
        {
            PWM_LED(0);
            level_idx = 0;
            timer_expiry = 0;
            GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_RESET);
            last_level_change_tick = GetTick();
        }

        /* 3. Automatic Dimming (When no user timer is set) */
        if (timer_expiry == 0 && level_idx > 0 && !is_pressing) {
            if (GetTick() - last_level_change_tick >= (ONE_HOUR_MS / 3)) {
                level_idx--;
                PWM_LED(brightness[level_idx]);
                last_level_change_tick = GetTick();
            }
        }

        /* 4. Stuck-Button / Debris Protection */
        if (is_pressing && (GetTick() - press_start > 30000)) {
            is_pressing = 0; // Reset state if button held > 30s
        }

        /* 5. Idle Standby Handling (When light is off and no active timer) */
        /* Stay in shallow Sleep mode (WFI) if charging to allow the indicator blip logic to run.
         * If not charging and light is off, enter Deep Sleep (Standby).
         * Note: Changed to 0mA threshold to detect any charge. */
        if (level_idx == 0 && timer_expiry == 0 && !is_pressing &&
            battery_status != 2 && !is_charging) {
            Enter_Deep_Sleep();
        }

        __WFI(); // Wait For Interrupt: Puts CPU to sleep until the next interrupt
    }
}

/**
 * @brief Configures Independent Watchdog (IWDG)
 * Timeout is approx 4 seconds (LSI @ 128kHz / 64 / 4000)
 */
void IWDG_Config(void) {
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_128); // 128kHz / 128 = 1000 Hz
    IWDG_SetReload(4000);                 // 4000 / 1000 = 4 seconds
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void PWM_LED(u16 duty){

        GPIO_InitTypeDef GPIO_InitStructure={0};
        TIM_OCInitTypeDef TIM_OCInitStructure={0};
        TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure={0};

        /* If turning off, disable the timer and its clock to save power */
        if (duty == 0) {
            TIM_Cmd(TIM1, DISABLE);
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, DISABLE);
            return;
        }

        RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE );

        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
        GPIO_Init( GPIOA, &GPIO_InitStructure );

        /* Frequency = 48MHz / (48 * 1000) = 1000Hz (1kHz) */
        TIM_TimeBaseInitStructure.TIM_Period = 1000-1;
        TIM_TimeBaseInitStructure.TIM_Prescaler = 48-1; 
        TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInit( TIM1, &TIM_TimeBaseInitStructure);

        /* Enforce Low Battery Brightness Limit (Max level 2) */
        if(battery_status == 1 && duty > 300) duty = 300;

        TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
        TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
        TIM_OCInitStructure.TIM_Pulse = duty;
        TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
        TIM_OC2Init( TIM1, &TIM_OCInitStructure );

        TIM_CtrlPWMOutputs(TIM1, ENABLE );
        TIM_OC2PreloadConfig( TIM1, TIM_OCPreload_Disable );
        TIM_ARRPreloadConfig( TIM1, ENABLE );
        TIM_Cmd( TIM1, ENABLE );
}

void TIM2_IRQHandler(void)
{
    static uint8_t led_state = 0;

    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        ms_ticks++;

        /* Provide visual feedback while holding the button for the timer */
        if (is_pressing)
        {
            uint32_t hold_duration = ms_ticks - press_start;
            /* After 800ms, blink PC1 once per second to count up to 4 hours */
            if (hold_duration >= 800 && hold_duration < 4800) {
                uint32_t phase = (hold_duration - 800) % 1000;
                GPIO_WriteBit(GPIOC, GPIO_Pin_1, (phase < 400) ? Bit_SET : Bit_RESET);
            }
        }
        
        /* Blink Battery LED (PC2) if battery is low (1Hz) */
        if (battery_status == 1 && level_idx > 0) { // Only blink if main LED is active
            if (ms_ticks % 500 == 0) {
                led_state = !led_state;
                GPIO_WriteBit(GPIOC, GPIO_Pin_2, led_state ? Bit_SET : Bit_RESET);
            }
        } else if (is_charging) {
            uint32_t phase = ms_ticks % 4000;
            if (charge_current_ma > 10) {
                /* Rapid Charge (>10mA): Double blink (150ms ON, 150ms OFF, 150ms ON every 4s) */
                if (phase < 150 || (phase >= 300 && phase < 450)) GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_SET);
                else GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
            } else {
                /* Trickle Charge: Low-power single 'blip' (200ms ON every 4 seconds) */
                if (phase < 200) GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_SET);
                else GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
            }
        } else if (battery_status != 2) {
            /* Only force off if not blinking or blipping. 
             * Let the main loop handle the PC2 state if in Critical Shutdown (status 2). */
            GPIO_WriteBit(GPIOC, GPIO_Pin_2, Bit_RESET);
        }

        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

void AWU_IRQHandler(void)
{
    /* Placeholder handler to allow AWU to wake the core from Standby */
}

void TIM2_Init_Tick(void) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure={0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = (SystemCoreClock / 1000000) - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    TIM_Cmd(TIM2, ENABLE);
}

uint32_t GetTick(void) {
    return ms_ticks;
}

void EXTI7_0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line4) != RESET)
    {
        uint32_t now = GetTick();
        uint8_t pin_state = GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_4);

        /* If battery is critical, skip normal logic (MCU will go back to sleep in main) */
        if (battery_status == 2) {
            EXTI_ClearITPendingBit(EXTI_Line4);
            return;
        }

        if(pin_state == Bit_RESET) // Button Pressed (Switch pulls to ground)
        {
            if(!is_pressing)
            {
                press_start = now;
                is_pressing = 1;
            }
        }
        else // Button Released (Rising Edge)
        {
            if(is_pressing)
            {
                uint32_t duration = now - press_start;
                if(duration > 800) // Long press (> 800ms)
                {
                    /* Calculate hours: 1 blink = 1hr, 2 = 2hr, 3 = 3hr, 4 = 4hr */
                    uint32_t hours = (duration >= 3800) ? 4 : (duration >= 2800) ? 3 : (duration >= 1800) ? 2 : 1;

                    /* Turn light to first level if it was off */
                    if(level_idx == 0) {
                        level_idx = 1;
                        PWM_LED(brightness[level_idx]);
                        last_level_change_tick = now;
                    }

                    timer_expiry = now + (hours * ONE_HOUR_MS);
                    GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_SET); // Mode LED indicates Timer active
                }
                else if(duration > 20) // Valid short press (30ms - 800ms)
                {
                    timer_expiry = 0; // Manual adjustment cancels the timer
                    GPIO_WriteBit(GPIOC, GPIO_Pin_1, Bit_RESET);

                    /* Smart Off: If ON for > 2s, next press turns it OFF */
                    if (level_idx != 0 && (now - last_level_change_tick) > 3000) {
                        level_idx = 0;
                    } else {
                        level_idx = (level_idx + 1) % num_levels;
                    }

                    last_level_change_tick = now;
                    PWM_LED(brightness[level_idx]);
                }
                is_pressing = 0;
            }
        }
        EXTI_ClearITPendingBit(EXTI_Line4);
    }
}

void Button_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure={0};
    EXTI_InitTypeDef EXTI_InitStructure={0};
    NVIC_InitTypeDef NVIC_InitStructure={0};


    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO , ENABLE );
    RCC_APB1PeriphClockCmd( RCC_APB1Periph_TIM2, ENABLE );

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init( GPIOC, &GPIO_InitStructure );

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource4);

    EXTI_InitStructure.EXTI_Line = EXTI_Line4;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void LED_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure={0};
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOC, ENABLE );

    /* PC1: Mode LED, PC2: Battery LED */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init( GPIOC, &GPIO_InitStructure );
}

void ADC_Init_Vdd(void) {
    ADC_InitTypeDef ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div12); // 48MHz/12 = 4MHz (More stable at low VDD)

    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
    
    /* PA2 (Pin 3) as Analog Input for Solar Voltage Monitoring */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_Cmd(ADC1, ENABLE);
    
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    ADC_Cmd(ADC1, DISABLE); // Keep off until needed
}

/* This function is called after a reset (cold boot or wake from Standby) */
void Post_System_Setup(void) {
    SystemInit(); 
    ADC_Init_Vdd(); // Re-initialize ADC clock/config lost during Standby/SystemInit

    /* Perform initial blocking battery check before allowing interaction or sleep */
    ADC_Cmd(ADC1, ENABLE);
    battery_status = Check_Battery();
    ADC_Cmd(ADC1, DISABLE);

    batt_timer = GetTick(); 
    batt_state = 0;
    /* Do not reset flags here! They were just correctly set by Check_Battery() above. 
     * Resetting them would cause the MCU to exit charging mode and enter Deep Sleep immediately. */

    PFIC->SCTLR &= ~(1<<2); // Clear SLEEPDEEP bit
    TIM_Cmd(TIM2, ENABLE);
}



uint16_t Get_ADC_Val(uint8_t ch) {
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_30Cycles);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    return ADC_GetConversionValue(ADC1);
}

uint8_t Check_Battery(void) {
    uint32_t vref_sum = 0;
    uint32_t va2_sum = 0;
    
    /* Sample internal reference and Solar Divider (A0/PA2) */
    for(int i=0; i<4; i++) { 
        vref_sum += Get_ADC_Val(ADC_Channel_Vrefint);
        va2_sum += Get_ADC_Val(ADC_Channel_0); 
    }
    uint16_t vref_avg = vref_sum >> 2; // Divide by 4
    uint16_t va2_avg = va2_sum >> 2;   // Divide by 4

    /* 1. Calculate Voltages in mV */
    /* VDD = (1.2V * 1023) / ADC_Vref */
    uint32_t vdd_mv = (1200 * 1023) / vref_avg;
    
    /* Vsolar = 2 * (ADC_A0 * VDD / 1023) 
     * Simplified: (2400 * ADC_A0) / ADC_Vref */
    uint32_t vsolar_mv = (2400 * (uint32_t)va2_avg) / vref_avg;

    /* 2. Estimate Charge Current */
    if (vsolar_mv > vdd_mv) {
        charge_current_ma = (vsolar_mv - vdd_mv) / CHARGE_RESISTOR_OHMS;
        is_charging = 1;
    } else {
        charge_current_ma = 0; // No charging if panel voltage < battery
        is_charging = 0;
    }

    /* Hysteresis: If in Critical state, stay there until voltage reaches 3.4V (ADC < 361) */
    if (battery_status == 2) {
        if (vref_avg > VDD_RECOVERY_THRESHOLD) return 2;
    }

    if (vref_avg > VDD_CRITICAL_THRESHOLD) return 2; // VDD < 2.7V
    if (vref_avg > VDD_LOW_THRESHOLD) return 1;      // VDD < 3V
    return 0;
}

void Enter_Deep_Sleep(void) {
    PWM_LED(0);
    GPIO_WriteBit(GPIOC, GPIO_Pin_1 | GPIO_Pin_2, Bit_RESET);
    
    TIM_Cmd(TIM2, DISABLE); // Stop tracking time while clocks are frozen
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    /* Configure Auto-Wakeup (AWU) to check solar status every ~10 seconds */
    /* LSI 128kHz / 61440 (PSC=1111) = 2.08Hz. Window 21 = ~10.08 second intervals */
    PWR->AWUPSC = 0x0F; 
    PWR->AWUWR = 21;
    PWR->AWUCSR |= (1<<1); // AWUEN
    /* Set Standby mode bit (PDDS) and Kernel Deep Sleep bit */
    PWR->CTLR |= PWR_CTLR_PDDS; // Set Standby mode bit
    PFIC->SCTLR |= (1<<2);      // Set Kernel Deep Sleep bit
    
    __WFI(); // MCU stops here until button press or AWU timer
    
    Post_System_Setup(); // Call common post-wakeup initialization
}
