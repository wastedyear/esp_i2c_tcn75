/*
 *  Examples using Temperature Sensor TCN75A
 *
 *  http://ww1.microchip.com/downloads/en/DeviceDoc/21935D.pdf
 *
 */

#include "ets_sys.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/tcn75a.h"
#include "osapi.h"
#include "os_type.h"
#include "user_interface.h"
#include "user_config.h"

os_event_t user_procTaskQueue[user_procTaskQueueLen];
extern int ets_uart_printf(const char *fmt, ...);
static void user_procTask(os_event_t *events);
static volatile os_timer_t sensor_timer;

void sensor_timerfunc(void *arg)
{
    int i;
    uint16_t *readings;
    uint8_t present = 0;
    float t;
    char temp[80];

    ets_uart_printf("Get temperatures...\r\n");
    readings = tcn75a_read(&present);
    for (i=0; i < 8; i++) {
	if (present & (1<<i)) {
 		t = tcn75_get_temp(readings[i]);
		os_sprintf(temp,"%d: %d.%d C\n",
			(int)(t),(int)((t - (int)t)*100));
                ets_uart_printf(temp);
        }
    }
}

static void ICACHE_FLASH_ATTR
user_procTask(os_event_t *events)
{
    os_delay_us(5000);
}

void user_init(void)
{
    int present;
    //Init uart
    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    os_delay_us(1000);

    ets_uart_printf("Booting...\r\n");

    // Init
    present = tcn75_init();
    if (present) {
    	ets_uart_printf("TCN75A init done.\r\n");
        //Disarm timer
        os_timer_disarm(&sensor_timer);
        //Setup timer
        os_timer_setfn(&sensor_timer, (os_timer_func_t *)sensor_timerfunc, NULL);
        //Arm timer for every 10 sec.
        os_timer_arm(&sensor_timer, 5000, 1);
    }
    else {
    	ets_uart_printf("TCN75A init error.\r\n");
    }

    system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
}

