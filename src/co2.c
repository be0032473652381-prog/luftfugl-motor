#include "co2.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "power_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCD41_ADDRESS 0x62u
#define FILTER_SAMPLES 7u
enum { CMD_START=0x21b1, CMD_READ=0xec05, CMD_STOP=0x3f86,
  CMD_SET_OFFSET=0x241d, CMD_GET_OFFSET=0x2318, CMD_SET_ALTITUDE=0x2427,
  CMD_GET_ALTITUDE=0x2322, CMD_SET_ASC=0x2416, CMD_GET_ASC=0x2313,
  CMD_READY=0xe4b8, CMD_SERIAL=0x3682, CMD_SELFTEST=0x3639,
  CMD_SINGLE=0x219d, CMD_POWER_DOWN=0x36e0, CMD_WAKE=0x36f6 };

static bool detected, measuring, sample_valid, powered=true, single_mode, warming;
static co2_variant_t variant;
static uint32_t next_poll_ms, frames;
static uint16_t ppm, humidity_tenths, serial_words[3], offset_raw, altitude;
static int16_t temperature_tenths;
static bool asc, ready_latched;
static uint16_t samples[FILTER_SAMPLES];
static uint8_t sample_count;
static bool filtered_valid;
static int32_t filtered_milli;

static uint8_t crc8(const uint8_t *p,size_t n){uint8_t c=0xff;while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c&0x80)?(uint8_t)((c<<1)^0x31):(uint8_t)(c<<1);}return c;}
static bool command(uint16_t c){uint8_t b[2]={(uint8_t)(c>>8),(uint8_t)c};return i2c_write_blocking(i2c0,SCD41_ADDRESS,b,2,false)==2;}
static bool command_word(uint16_t c,uint16_t w){uint8_t b[5]={(uint8_t)(c>>8),(uint8_t)c,(uint8_t)(w>>8),(uint8_t)w,0};b[4]=crc8(b+2,2);return i2c_write_blocking(i2c0,SCD41_ADDRESS,b,5,false)==5;}
static bool words(uint16_t c,uint16_t *w,size_t n,uint32_t delay){uint8_t b[9];if(!command(c))return false;sleep_ms(delay);if(i2c_read_blocking(i2c0,SCD41_ADDRESS,b,n*3,false)!=(int)(n*3))return false;for(size_t i=0;i<n;i++){if(crc8(b+i*3,2)!=b[i*3+2])return false;w[i]=(uint16_t)(b[i*3]<<8)|b[i*3+1];}return true;}
static bool claim(void){return power_monitor_i2c_claim();} static void release(void){power_monitor_i2c_release();}
static bool idle_begin(bool *restart){*restart=measuring;if(!claim())return false;if(*restart&&!command(CMD_STOP)){release();return false;}if(*restart){sleep_ms(500);measuring=false;}return true;}
static void idle_end(bool restart){if(restart){measuring=command(CMD_START);next_poll_ms=to_ms_since_boot(get_absolute_time())+5000;}release();}
static void filter_push(uint16_t v){samples[sample_count++]=v;if(sample_count<7)return;for(unsigned i=1;i<7;i++){uint16_t x=samples[i];unsigned j=i;while(j&&samples[j-1]>x){samples[j]=samples[j-1];j--;}samples[j]=x;}int32_t m=(int32_t)samples[3]*1000;filtered_milli=filtered_valid?(3*m+7*filtered_milli+5)/10:m;filtered_valid=true;sample_count=0;}
static bool read_sample(void){uint16_t w[3];if(!words(CMD_READ,w,3,1))return false;ppm=w[0];temperature_tenths=(int16_t)(-450+(1750L*w[1]+32767)/65535L);humidity_tenths=(uint16_t)((1000UL*w[2]+32767)/65535UL);sample_valid=true;ready_latched=true;frames++;filter_push(ppm);return true;}
static void refresh_config(void){uint16_t w;if(words(CMD_GET_ASC,&w,1,1))asc=w==1;words(CMD_GET_OFFSET,&offset_raw,1,1);words(CMD_GET_ALTITUDE,&altitude,1,1);}

void co2_init(void){detected=false;variant=CO2_VARIANT_NONE;sample_valid=false;frames=0;sample_count=0;filtered_valid=false;powered=true;single_mode=false;warming=false;if(!claim())return;command(CMD_WAKE);sleep_ms(30);(void)command(CMD_STOP);sleep_ms(500);detected=words(CMD_SERIAL,serial_words,3,1);if(detected){variant=CO2_VARIANT_SCD41;refresh_config();measuring=command(CMD_START);next_poll_ms=to_ms_since_boot(get_absolute_time())+5000;}release();}
void co2_tick(void){if(!detected||!powered)return;uint32_t now=to_ms_since_boot(get_absolute_time());if(warming){if((int32_t)(now-next_poll_ms)<0)return;if(!claim())return;measuring=command(CMD_START);warming=!measuring;next_poll_ms=now+5000;release();return;}if(!measuring||(int32_t)(now-next_poll_ms)<0)return;next_poll_ms=now+1000;if(!claim())return;uint16_t r;if(words(CMD_READY,&r,1,1)&&(r&0x7ff)){read_sample();next_poll_ms=now+5000;}release();}
bool co2_detected(void){return detected;} co2_variant_t co2_variant(void){return variant;} bool co2_sample_valid(void){return sample_valid;} uint16_t co2_ppm(void){return ppm;} int16_t co2_temperature_tenths(void){return temperature_tenths;} uint16_t co2_humidity_tenths(void){return humidity_tenths;} uint32_t co2_frames_read(void){return frames;}
static const char *zone(uint16_t v){return v<600?"excellent":v<800?"good":v<1000?"moderate":v<=2000?"poor":"hazardous";}
void co2_format_menu(char l[18][81]) {
  uint64_t sn = ((uint64_t)serial_words[0] << 32) |
                ((uint64_t)serial_words[1] << 16) | serial_words[2];
  const char *waiting = powered && warming ? "thermal stabilisation waiting" :
                        powered ? "pending" : "sensor off";
  snprintf(l[0], 81, "- serial      = %llu", (unsigned long long)sn);
  snprintf(l[1], 81, "- i2c address = 0x62");
  snprintf(l[2], 81, "  SDC41 = %s", powered ? (warming ? "WARMING UP" : "ON") : "OFF");
  if (sample_valid && !warming && powered) {
    uint16_t filtered = filtered_valid ?
        (uint16_t)((filtered_milli + 500) / 1000) : ppm;
    snprintf(l[3], 81, "- co2         = %u ppm ->>> co2 zone: '%s'", filtered,
             zone(filtered));
    snprintf(l[4], 81, "- co2 raw     = %u ppm", ppm);
    snprintf(l[6], 81, "- temperature = %d.%03d degrees C",
             temperature_tenths / 10, abs(temperature_tenths % 10) * 100);
    snprintf(l[7], 81, "- humidity    = %u.%03u %%RH",
             humidity_tenths / 10, (humidity_tenths % 10) * 100u);
  } else {
    snprintf(l[3], 81, "- co2         = %s", waiting);
    snprintf(l[4], 81, "- co2 raw     = %s", waiting);
    snprintf(l[6], 81, "- temperature = %s", waiting);
    snprintf(l[7], 81, "- humidity    = %s", waiting);
  }
  snprintf(l[5], 81, "- filter      = %u/7 samples%s", sample_count,
           filtered_valid ? ", median + EMA active" : " collected");
  snprintf(l[8], 81, "- asc         = %s", asc ? "on" : "off");
  snprintf(l[9], 81, "- offset      = %.3f degrees C",
           offset_raw * 175.0 / 65536.0);
  snprintf(l[10], 81, "- altitude    = %u m", altitude);
  snprintf(l[11], 81, "- mode        = %s", single_mode ? "single" : "periodic");
  snprintf(l[12], 81, "- data ready  = %s", ready_latched ? "yes" : "no");
  snprintf(l[13], 81, "  Commands:");
  snprintf(l[14], 81, "  co2 | ready | serial | selftest | asc [on|off]");
  snprintf(l[15], 81, "  offset [degrees] | altitude [metres] | mode [periodic|single]");
  snprintf(l[16], 81, "  status | sdc41 <on|off> | menu | help [command]");
  l[17][0] = '\0';
}

static bool noargs(const char*a){return !a||!*a;}
bool co2_command(const char*c,const char*a,char*o,size_t z){bool restart=false,ok=false;if(!a)a="";
if(!strcmp(c,"co2")){if(*a){snprintf(o,z,"co2 takes no arguments");return false;}if(single_mode){if(!claim()){snprintf(o,z,"I2C busy; retry");return false;}ok=command(CMD_SINGLE);if(ok){sleep_ms(5000);ok=read_sample();}release();}else ok=sample_valid;if(ok)snprintf(o,z,"co2 %u ppm; raw %u; temp %d.%01d C; humidity %u.%01u %%RH",filtered_valid?(unsigned)((filtered_milli+500)/1000):ppm,ppm,temperature_tenths/10,abs(temperature_tenths%10),humidity_tenths/10,humidity_tenths%10);else snprintf(o,z,"no measurement available yet");return ok;}
if(!strcmp(c,"ready")){if(!noargs(a)){snprintf(o,z,"ready takes no arguments");return false;}snprintf(o,z,"data ready: %s",ready_latched?"yes":"no");ready_latched=false;return true;}
if(!strcmp(c,"serial")){if(!idle_begin(&restart)){snprintf(o,z,"I2C busy or stop failed; retry");return false;}ok=words(CMD_SERIAL,serial_words,3,1);idle_end(restart);if(ok)snprintf(o,z,"serial: %04X%04X%04X",serial_words[0],serial_words[1],serial_words[2]);else snprintf(o,z,"serial read failed");return ok;}
if(!strcmp(c,"selftest")){if(!idle_begin(&restart)){snprintf(o,z,"I2C busy or stop failed; retry");return false;}uint16_t w=1;ok=words(CMD_SELFTEST,&w,1,10000);idle_end(restart);snprintf(o,z,ok?(w?"self-test FAIL: sensor malfunction":"self-test PASS"):"self-test execution failed");return ok&&!w;}
if(!strcmp(c,"asc")){bool set=*a,v=asc;if(set&&strcmp(a,"on")&&strcmp(a,"off")){snprintf(o,z,"usage: asc [on|off]");return false;}if(set)v=!strcmp(a,"on");if(!idle_begin(&restart)){snprintf(o,z,"I2C busy or stop failed; retry");return false;}uint16_t w=0;ok=set?command_word(CMD_SET_ASC,v):words(CMD_GET_ASC,&w,1,1);idle_end(restart);if(!set&&ok)v=w==1;if(ok)asc=v;snprintf(o,z,ok?"ASC: %s%s":"ASC operation failed",set?"set ":"",v?"on":"off");return ok;}
if(!strcmp(c,"offset")){char*e=NULL;double v=*a?strtod(a,&e):0;bool set=*a;if(set&&(*e||v<0||v>20)){snprintf(o,z,"offset range is 0 to 20 degrees C");return false;}uint16_t w=set?(uint16_t)(v*65536.0/175.0+0.5):0;if(!idle_begin(&restart)){snprintf(o,z,"I2C busy or stop failed; retry");return false;}ok=set?command_word(CMD_SET_OFFSET,w):words(CMD_GET_OFFSET,&w,1,1);idle_end(restart);if(ok)offset_raw=w;snprintf(o,z,ok?"offset: %s%.3f C":"offset operation failed",set?"set ":"",w*175.0/65536.0);return ok;}
if(!strcmp(c,"altitude")){char*e=NULL;long v=*a?strtol(a,&e,10):0;bool set=*a;if(set&&(*e||v<0||v>3000)){snprintf(o,z,"altitude range is 0 to 3000 metres");return false;}uint16_t w=(uint16_t)v;if(!idle_begin(&restart)){snprintf(o,z,"I2C busy or stop failed; retry");return false;}ok=set?command_word(CMD_SET_ALTITUDE,w):words(CMD_GET_ALTITUDE,&w,1,1);idle_end(restart);if(ok)altitude=w;if(ok)snprintf(o,z,"altitude: %s%u m",set?"set ":"",w);else snprintf(o,z,"altitude operation failed");return ok;}
if(!strcmp(c,"mode")){if(!*a){snprintf(o,z,"mode: %s",single_mode?"single":"periodic");return true;}if(strcmp(a,"single")&&strcmp(a,"periodic")){snprintf(o,z,"usage: mode [periodic|single]");return false;}bool s=!strcmp(a,"single");if(s==single_mode){snprintf(o,z,"mode: already %s",a);return true;}if(!claim()){snprintf(o,z,"I2C busy; retry");return false;}ok=command(s?CMD_STOP:CMD_START);if(ok){single_mode=s;measuring=!s;}release();snprintf(o,z,ok?"mode: set %s":"mode switch failed",a);return ok;}
if(!strcmp(c,"sdc41")){if(strcmp(a,"on")&&strcmp(a,"off")){snprintf(o,z,"usage: sdc41 <on|off>");return false;}if((!strcmp(a,"on")&&powered)||(!strcmp(a,"off")&&!powered)){snprintf(o,z,"SCD41 already %s",powered?"on":"off");return false;}if(!claim()){snprintf(o,z,"I2C busy; retry");return false;}if(!strcmp(a,"off")){ok=!measuring||command(CMD_STOP);if(measuring)sleep_ms(500);if(ok)ok=command(CMD_POWER_DOWN);if(ok){powered=false;measuring=false;warming=false;}}else{command(CMD_WAKE);sleep_ms(30);powered=true;warming=true;measuring=false;next_poll_ms=to_ms_since_boot(get_absolute_time())+60000;ok=true;}release();snprintf(o,z,ok?"SCD41: %s":"SCD41 power command failed",powered?"on (60-second thermal warm-up begins)":"off");return ok;}
if(!strcmp(c,"status")||!strcmp(c,"menu")){snprintf(o,z,"SCD41 %s; mode %s; ASC %s; samples %lu; ready %s",powered?"on":"off",single_mode?"single":"periodic",asc?"on":"off",(unsigned long)frames,ready_latched?"yes":"no");return true;}snprintf(o,z,"unknown SDC41 command");return false;}
const char *co2_command_help(const char*c){static const struct{const char*n,*h;}x[]={{"co2","co2: latest periodic reading or 5-second single shot"},{"ready","ready: show and clear latched data-ready state"},{"serial","serial: read the 48-bit SCD41 serial number"},{"selftest","selftest: stop measurement and run the approximately 10-second sensor test"},{"asc","asc [on|off]: read or set automatic self-calibration"},{"offset","offset [degrees]: read or set 0..20 C temperature offset"},{"altitude","altitude [metres]: read or set 0..3000 m"},{"mode","mode [periodic|single]: read or select measurement mode"},{"status","status: show SCD41 mode, ASC, samples, and ready state"},{"sdc41","sdc41 <on|off>: protocol power-down or wake/start"},{"menu","menu: redraw Page 5 live SCD41 menu"}};for(size_t i=0;i<sizeof x/sizeof x[0];i++)if(!strcmp(c,x[i].n))return x[i].h;return NULL;}
