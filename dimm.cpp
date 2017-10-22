
#include "dimm.h"

static Dimm* handler = 0;

uint32_t latches[4];

void sync_irq()
{
    //Timer down counting, small values get small pulses at the end, big pulses start sooner
    TIM1->CCR1 = latches[0];
    TIM1->CCR2 = latches[1];
    TIM1->CCR3 = latches[2];
    TIM1->CCR4 = latches[3];
    TIM1->CR1 |= TIM_CR1_CEN;       //Start the counter for one cycle

    handler->intCount++;
}

Dimm::Dimm(Serial *ps,PinName Sync, PinName ch1, PinName ch2, PinName ch3, PinName ch4):
                pser(ps),
                syncIrq(Sync),
                pwm1(ch1),
                pwm2(ch2),
                pwm3(ch3),
                pwm4(ch4)
{
    intCount = 0;


    handler = this;

    pwm1 = 0;
    pwm2 = 0;
    pwm3 = 0;
    pwm4 = 0;

    TIM1->CR1 &= TIM_CR1_CEN;       //Stop the counter
    
    TIM1->CR1 &= ~TIM_CR1_CMS_Msk;  //stay with Edge alligned mode
    TIM1->CR1 |= TIM_CR1_OPM;       //One Pulse Mode
    TIM1->CR1 |= TIM_CR1_DIR;       //Down counting
    
    TIM1->PSC = 63;//[ 64 MHz / (63+1) ] = 1MHz => 1us
    TIM1->ARR = level::tim_per;//9900   10 ms / half period of 50 Hz

    //Photo exp 10 with 1200, 1400, 1600, 1800
    latches[0] = 0;
    latches[1] = 0;
    latches[2] = 0;
    latches[3] = 0;
    
}

void Dimm::init()
{

    syncIrq.rise(&sync_irq);
    syncIrq.mode(PullNone);
    NVIC_SetPriority(EXTI15_10_IRQn,1);
    syncIrq.enable_irq();
}

void Dimm::set_level(uint8_t channel,uint16_t value)
{
    if(value < level::min)
    {
        value = level::min;
    }
    if(value > level::tim_per)
    {
        value = level::tim_per;
    }
    //uint16_t delay = level::tim_per - (value - level::min);
    if(channel <= 3)
    {
        latches[channel] = value;
    }
}
