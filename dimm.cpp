
#include "dimm.h"

static Dimm* handler = 0;

uint32_t latches[8];

#define DEBUG_DIMMER 0

void sync_irq()
{
    //Timer down counting, small values get small pulses at the end, big pulses start sooner
    TIM1->CCR1 = latches[0];
    TIM1->CCR2 = latches[1];
    TIM1->CCR3 = latches[2];
    TIM1->CCR4 = latches[3];
    TIM1->CR1 |= TIM_CR1_CEN;       //Start the counter for one cycle

    TIM2->CCR1 = latches[4];
    TIM2->CCR2 = latches[5];
    //TIM2->CCR3 = latches[6];
    //TIM2->CCR4 = latches[7];
    TIM2->CR1 |= TIM_CR1_CEN;       //Start the counter for one cycle

    TIM3->CCR3 = latches[6];
    TIM3->CCR4 = latches[7];
    TIM3->CR1 |= TIM_CR1_CEN;       //Start the counter for one cycle
    
    handler->intCount++;
}

Dimm::Dimm(     Serial *ps,PinName Rel,PinName Sync, 
                PinName ch1, PinName ch2, PinName ch3, PinName ch4,
                PinName ch5, PinName ch6, PinName ch7, PinName ch8
            ):
                pser(ps),
                relay(Rel),
                syncIrq(Sync),
                pwm1(ch1),
                pwm2(ch2),
                pwm3(ch3),
                pwm4(ch4),
                pwm5(ch5),
                pwm6(ch6),
                pwm7(ch7),
                pwm8(ch8)
{
    relay = 1;//Off
    intCount = 0;


    handler = this;

    pwm1 = 0;
    pwm2 = 0;
    pwm3 = 0;
    pwm4 = 0;
    pwm5 = 0;
    pwm6 = 0;
    pwm7 = 0;
    pwm8 = 0;

    //Timer 1---------------------------------------------------------
    TIM1->CR1 &= TIM_CR1_CEN;       //Stop the counter
    
    TIM1->CR1 &= ~TIM_CR1_CMS_Msk;  //stay with Edge alligned mode
    TIM1->CR1 |= TIM_CR1_OPM;       //One Pulse Mode
    TIM1->CR1 |= TIM_CR1_DIR;       //Down counting
    
    TIM1->PSC = 63;//[ 64 MHz / (63+1) ] = 1MHz => 1us
    TIM1->ARR = level::tim_per;//9900   10 ms / half period of 50 Hz

    //Timer 2---------------------------------------------------------
    TIM2->CR1 &= TIM_CR1_CEN;       //Stop the counter
    
    TIM2->CR1 &= ~TIM_CR1_CMS_Msk;  //stay with Edge alligned mode
    TIM2->CR1 |= TIM_CR1_OPM;       //One Pulse Mode
    TIM2->CR1 |= TIM_CR1_DIR;       //Down counting
    
    TIM2->PSC = 63;//[ 64 MHz / (63+1) ] = 1MHz => 1us
    TIM2->ARR = level::tim_per;//9900   10 ms / half period of 50 Hz

    //Timer 3---------------------------------------------------------
    TIM3->CR1 &= TIM_CR1_CEN;       //Stop the counter
    
    TIM3->CR1 &= ~TIM_CR1_CMS_Msk;  //stay with Edge alligned mode
    TIM3->CR1 |= TIM_CR1_OPM;       //One Pulse Mode
    TIM3->CR1 |= TIM_CR1_DIR;       //Down counting
    
    TIM3->PSC = 63;//[ 64 MHz / (63+1) ] = 1MHz => 1us
    TIM3->ARR = level::tim_per;//9900   10 ms / half period of 50 Hz
    
    //Photo exp 10 with 1200, 1400, 1600, 1800
    latches[0] = 0;
    latches[1] = 0;
    latches[2] = 0;
    latches[3] = 0;
    latches[4] = 0;
    latches[5] = 0;
    latches[6] = 0;
    latches[7] = 0;
    
}

void Dimm::init()
{

    syncIrq.rise(&sync_irq);
    syncIrq.mode(PullNone);
    NVIC_SetPriority(EXTI9_5_IRQn,1);
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
    if(channel <= 7)
    {
        latches[channel] = value;
    }
}

//delay in ISR context : 100K => 14.18 ms
void cpu_delay()
{
    volatile uint32_t count = 0;
    for(uint32_t i=0;i<100000;i++)
    {
        count++;
    }
}

void Dimm::handle_message(uint8_t *data,uint8_t size)
{
    if(size == 7)//--------------------- Light for All -----------------------------
    {
        uint16_t light_val = data[5];
        light_val <<=8;
        light_val += data[6];
        #if(DEBUG_DIMMER == 1)
        pser->printf("Light for All : %d\r",light_val);
        #endif

        set_level(0,light_val);
        set_level(1,light_val);
        set_level(2,light_val);
        set_level(3,light_val);
        set_level(4,light_val);
        set_level(5,light_val);
        set_level(6,light_val);
        set_level(7,light_val);

        if(light_val == 0 )
        {
            //we got a flash on switch off as well !!!
            cpu_delay();//wait one cycle so that we're sure all pwm pulses are at 0
            syncIrq.disable_irq();//avoid floating jitter
            relay = 1;//off
        }
        else
        {
            //switching on the relay will flash any way even with value of 1 and delaying the start of sync handling
            //switch off, no sync irg, make sure all outputs are low before switching on (set level only prepare for next sync irq)
            //TIM1->CCR1 = 0;            TIM1->CCR2 = 0;            TIM1->CCR3 = 0;            TIM1->CCR4 = 0;
            //cpu_delay();//wait more than 1 pwm period to make sure outputs are at 0
            relay = 0;//switch on
            //should wait here many cycles to avoid light flash
            //probably due to wrong zero detection on start of waves
            //with 90 ms (6 x delay) still a smale wiggle when setting 0x7D0 from 0
            //this flash comes even when setting 0x0001 from 0, spontaneous relay-Dimmer flash
            //~ 90 ms 
            cpu_delay();cpu_delay();cpu_delay();cpu_delay();cpu_delay();cpu_delay();
            
            syncIrq.enable_irq();
        }
    }
    else if(size == 8)//--------------------- One Channel -----------------------------
    {
        uint8_t chan = data[5];
        uint16_t light_val = data[6];
        light_val <<=8;
        light_val += data[7];
        #if(DEBUG_DIMMER == 1)
        pser->printf("Light only for chan %d : %d\r",chan, light_val);
        #endif
        set_level(chan,light_val);
    }
    else
    {
        uint8_t subId = data[5];
        if(subId == 0x01)//Chan Array
        {
            uint8_t NbChannels = (size -6) / 2;
            uint8_t * pData = &data[6];
            for(int i=0;i<NbChannels;i++)
            {
                uint16_t light_val = *pData++;
                light_val <<= 8;
                light_val += *pData++;
                #if(DEBUG_DIMMER == 1)
                pser->printf("chan id %d : %d\r",i, light_val);
                #endif
                set_level(i,light_val);
            }
        }
        //else chanlist not yet supported
    }

}
