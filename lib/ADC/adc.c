#include "adc.h"

#define ADCx ADC1
#define ADC_Channel_n ADC_Channel_7
#define RCC_APBnPeriph_ADCx RCC_APB2Periph_ADC1
#define ADC_RCC_APBnPeriphClockCmd RCC_APB2PeriphClockCmd
#define ADC_ExternalTrigConv_Tn_trigger ADC_ExternalTrigConv_T3_TRGO

#define GPIOx GPIOA
#define GPIO_Pin_n GPIO_Pin_7
#define RCC_APBnPeriph_GPIOx RCC_APB2Periph_GPIOA
#define GPIO_RCC_APBnPeriphClockCmd RCC_APB2PeriphClockCmd

#define TIMn TIM3
#define TIM_RCC_APBnPeriphClockCmd RCC_APB1PeriphClockCmd
#define RCC_APBnPeriph_TIMn RCC_APB1Periph_TIM3


#define DMAn_Channeln_IRQn DMA1_Channel1_IRQn
#define DMAn_IT_TCn DMA1_IT_TC1
#define DMAn_Channeln DMA1_Channel1
#define RCC_AHBPeriph_DMAn RCC_AHBPeriph_DMA1

static u16 adcDmaData[ADC_DATA_SIZE];

static u16 freqMode = 10;

#define FREQ_MODES_COUNT 17

const u16 timerConfigs[34] = {
//  	Period Prescaler Freq
        49999, 7,   //      5
        31999, 4,   //   12,5
        15999, 4,   //     25
        39999, 0,   //     50
        15999, 0,   //    125
        7999, 0,    //    250
        3999, 0,    //    500
        1599, 0,    //   1250
        799, 0,     //   2500
        399, 0,     //   5000
        159, 0,     //  12500
        79, 0,      //  25000
        39, 0,      //  50000
        15, 0,      // 125000
        7, 0,       // 250000
        3, 0,       // 500000
        1, 0        //1000000
};

const char *freqModes[] = {
        "5Hz (5s)",
        "12,5Hz (80ms)",
        "25Hz (40ms)",
        "50Hz (20ms)",
        "125Hz (8ms)",
        "250Hz (4ms)",
        "500Hz (2ms)",
        "1,25kHz (800us)",
        "2,5kHz (400us)",
        "5kHz (200us)",
        "12,5kHz (80us)",
        "25kHz (40us)",
        "50kHz (20us)",
        "125kHz (8us)",
        "250kHz (4us)",
        "500kHz (2us)",
        "1MHz (1us)"
};

inline u16 *ADC_getData() {
    return adcDmaData;
}

static void timer_config(void) {
    TIM_TimeBaseInitTypeDef tim;
    TIM_RCC_APBnPeriphClockCmd(RCC_APBnPeriph_TIMn, ENABLE);

    /* Time base configuration */
    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Period        = 0;
    tim.TIM_Prescaler     = 0;
    tim.TIM_ClockDivision = 0;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIMn, &tim);

    TIM_SelectOutputTrigger(TIMn, TIM_TRGOSource_Update);
}

inline static void timer_start(void) {
    TIM_Cmd(TIMn, ENABLE);
}

inline static void timer_stop(void) {
    TIM_Cmd(TIMn, DISABLE);
}

static void dma_init() {
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMAn, ENABLE);
    // DMA
    DMA_InitTypeDef dma;
    DMA_StructInit(&dma);

    dma.DMA_BufferSize         = ADC_DATA_SIZE;
    dma.DMA_MemoryBaseAddr     = (u32) adcDmaData;
    dma.DMA_PeripheralBaseAddr = (u32) &(ADCx->DR);
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_Init(DMAn_Channeln, &dma);

    NVIC_EnableIRQ(DMAn_Channeln_IRQn);
    DMA_ITConfig(DMAn_Channeln, DMA_IT_TC, ENABLE);
    DMA_Cmd(DMAn_Channeln, ENABLE);
}

static void gpio_init() {
    GPIO_RCC_APBnPeriphClockCmd(RCC_APBnPeriph_GPIOx, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = GPIO_Pin_n;
    gpio.GPIO_Mode  = GPIO_Mode_AIN;
    GPIO_Init(GPIOx, &gpio);
}

static void adc_init() {

//clock for ADC (max 14MHz --> 72/6=12MHz)
    //TODO: check for maximum clock
    RCC_ADCCLKConfig(RCC_PCLK2_Div2);
    ADC_RCC_APBnPeriphClockCmd(RCC_APBnPeriph_ADCx, ENABLE);

    // ADC
    ADC_InitTypeDef adc;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_Tn_trigger;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADCx, &adc);

    ADC_RegularChannelConfig(ADCx, ADC_Channel_n, 1, ADC_SampleTime_1Cycles5);

    ADC_Cmd(ADCx, ENABLE);   //enable ADCx

    //Calibration
    ADC_ResetCalibration(ADCx);   // Reset previous calibration
    while (ADC_GetResetCalibrationStatus(ADCx));
    ADC_StartCalibration(ADCx);   // Start new calibration (ADC must be off at that time)
    while (ADC_GetCalibrationStatus(ADCx));

    ADC_ExternalTrigConvCmd(ADCx, ENABLE);
    ADC_DMACmd(ADCx, ENABLE);
    ADC_Cmd(ADCx, ENABLE);
}

inline const char *ADC_getFreqMode() {
    return freqModes[freqMode];
}

void ADC_changeFreq(u32 n) {
    freqMode = (u16) (n % FREQ_MODES_COUNT);

    timer_stop();

    TIM_TimeBaseInitTypeDef tim;

    tim.TIM_Period        = timerConfigs[freqMode * 2];
    tim.TIM_Prescaler     = timerConfigs[freqMode * 2 + 1];
    tim.TIM_ClockDivision = 0;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIMn, &tim);

    TIM_ARRPreloadConfig(TIMn, ENABLE);
    timer_start();
}

void ADC_init() {
    gpio_init();
    dma_init();
    adc_init();
    timer_config();
    ADC_changeFreq(10);
}

static u8 dataAvailable = 0;

inline u8 ADC_isDataAvailable() {
    return dataAvailable;
}

inline void ADC_markDataUsed() {
    dataAvailable = 0;
    timer_start();
}

void DMA1_Channel1_IRQHandler(void) {
    if (DMA_GetITStatus(DMAn_IT_TCn) == SET) {
        DMA_ClearITPendingBit(DMAn_IT_TCn);
        timer_stop();
        dataAvailable = 1;
    }
}
