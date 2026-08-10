#include "encoder.h"
#include "hardware/adc.h"
static uint16_t buf[FILTER_DEPTH]; static uint32_t sum; static uint8_t idx,stable; static position_t instant,confirmed; static volatile bool changed; static uint16_t rawv,avgv;
static position_t classify(uint16_t v){if(v<=BAND_P1_MAX)return 1;if(v<=BAND_P2_MAX)return 2;if(v<=BAND_P3_MAX)return 3;if(v<=BAND_P4_MAX)return 4;if(v<=BAND_P5_MAX)return 5;return POS_UNKNOWN;}
void encoder_init(void){adc_init();adc_gpio_init(PIN_SENSE);adc_select_input(ADC_CHANNEL);sum=0;idx=0;for(int i=0;i<FILTER_DEPTH;i++){buf[i]=adc_read();sum+=buf[i];}avgv=sum/FILTER_DEPTH;instant=confirmed=classify(avgv);stable=DEBOUNCE_MS;changed=false;}
void encoder_tick(void){rawv=adc_read();sum-=buf[idx];buf[idx]=rawv;sum+=rawv;idx=(idx+1)%FILTER_DEPTH;avgv=sum/FILTER_DEPTH;position_t p=classify(avgv);if(p!=instant){instant=p;stable=1;}else if(stable<255)stable++;if(stable>=DEBOUNCE_MS&&confirmed!=instant){confirmed=instant;changed=true;}}
uint16_t encoder_raw(void){return rawv;} uint16_t encoder_average(void){return avgv;} position_t encoder_instant(void){return instant;} position_t encoder_confirmed(void){return confirmed;} bool encoder_take_change(position_t*out){if(!changed)return false;*out=confirmed;changed=false;return true;}
