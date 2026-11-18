/* SPDX-License-Identifier: MIT */
/* TIM PWM: the LED "breathes": TIM2 channel 1 drives PA5 (AF2) with a 1 kHz PWM whose duty
 * cycle ramps up and down. Board: NUCLEO-G0B1RE, LD4 on PA5 = TIM2_CH1.
 * Timer clock 16 MHz (PCLK after reset, APB prescaler 1): PSC 15 -> 1 MHz counter, ARR 999 ->
 * 1 kHz period, CCR1 0..1000 = duty in 0.1 %.
 * Shows: configure with the counter stopped, TIM_GenerateUpdate loads PSC / ARR before the start
 * (driver caller duty), preloaded compare register (changes take effect at the next period). */
#include "ex_board.h"

#define PWM_TIM TIM2
#define PWM_CHANNEL 1U
#define PWM_PERIOD 1000U

int main(void)
{
    GPIO_InitTypeDef pin;
    TIM_BaseInitTypeDef base;
    TIM_OCInitTypeDef oc;
    uint32_t duty = 0U;
    uint8_t up = 1U;

    Ex_Init();
    Ex_Print("\r\nex_tim_pwm: LD4 breathes (TIM2 CH1 PWM on PA5)\r\n");

    /* PA5 from GPIO output (ex_board) to alternate function 2 = TIM2_CH1. */
    GPIO_StructInit(&pin);
    pin.pins = 1UL << EX_LED_PIN;
    pin.mode = (uint32_t)GPIO_MODE_AF;
    pin.output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
    pin.speed = (uint32_t)GPIO_SPEED_LOW;
    pin.alternate = 2U;
    EX_CHECK(GPIO_Init(EX_LED_PORT, &pin));

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_TIM2));
    base.prescaler = 15U;
    base.period = PWM_PERIOD - 1U;
    base.auto_reload_preload = 1U;
    EX_CHECK(TIM_BaseInit(PWM_TIM, &base));

    oc.mode = (uint32_t)TIM_OC_PWM1; /* output high while CNT < CCR1 */
    oc.compare = 0U;
    oc.inverted = 0U;
    oc.preload = 1U;
    EX_CHECK(TIM_OutputCompareInit(PWM_TIM, PWM_CHANNEL, &oc));
    EX_CHECK(TIM_ChannelEnable(PWM_TIM, PWM_CHANNEL, 1U));

    EX_CHECK(TIM_GenerateUpdate(PWM_TIM)); /* load PSC / ARR / CCR1 now */
    EX_CHECK(TIM_Enable(PWM_TIM, 1U));

    for (;;)
    {
        EX_CHECK(TIM_SetCompare(PWM_TIM, PWM_CHANNEL, (duty * duty) / PWM_PERIOD)); /* ~gamma */
        if (up != 0U)
        {
            duty += 10U;
            up = (duty < PWM_PERIOD) ? 1U : 0U;
        }
        else
        {
            duty -= 10U;
            up = (duty == 0U) ? 1U : 0U;
        }
        Ex_DelayMs(10U);
    }
}
