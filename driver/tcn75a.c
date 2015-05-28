
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "c_types.h"
#include "user_interface.h"
#include "driver/i2c.h"
#include "driver/tcn75a.h"

// It takes 30 ms (typical) for 9-bit data and 240 ms (typical) for 12-bit data

/*
Bits              Resolution           t CONV  (typical)
9			0.5 			30 ms
10                     0.25                     60 ms
11                    0.125                     120 ms
12                   0.0625                     240 ms
*/

// 01001XXX
#define TCN_BASE_ADDR 0x48

#define TCN_TA  0x00
#define TCN_CFG  0x01

#define TCN_CFG_ONE_SHOT (1<<7)

/*
SD  ADC RESOLUTION bits
00  =  9 bit or 0.5°C (Power-up default)
01  =  10 bit or 0.25°C
10  =  11 bit or 0.125°C
11  =  12 bit or 0.0625°C
*/

#define TCN_CFG_9BIT (0<<5)
#define TCN_CFG_10BIT (1<<5)
#define TCN_CFG_11BIT (2<<5)
#define TCN_CFG_12BIT (3<<5)


/*
FAULT QUEUE  bits
00 =  1 (Power-up default)
01 =2
10 =4
11  =6
*/

#define TCN_CFG_FQ_1BIT (0<<3)
#define TCN_CFG_FQ_2BIT (1<<3)
#define TCN_CFG_FQ_4BIT (2<<3)
#define TCN_CFG_FQ_6BIT (3<<3)

/*
ALERT POLARITY bit
1 =  Active-high
0 =  Active-low (Power-up default)
*/
#define TCN_CFG_ALERT_POLARITY (1<<2)

/*
COMP/INT bit
1 =  Interrupt mode
0 =  Comparator mode (Power-up default)
*/

#define TCN_CFG_INTERRUPT (1<<1)

/*
SHUTDOWN bit
1 =  Enable
0 =  Disable (Power-up default)
*/

#define TCN_CFG_SHUTDOWN (1<<0)


#define TCN_CONFIG  (TCN_CFG_SHUTDOWN | TCN_CFG_11BIT | TCN_CFG_FQ_1BIT)

// 30ms reserve
#define TCN_CONV_TIME (1*(120+30))

static uint16_t data[8];
static uint8_t present;

static ETSTimer tcntimer_t;
static ETSTimer tcnread_t;

#define INVALID_READING 1


static int ICACHE_FLASH_ATTR tcn75_set_cfg(int idx, uint8_t cfgval)
{
	uint8_t addr =  TCN_BASE_ADDR | idx;
	int rc = 0;
	// set in one shot mode
//	os_printf("%d:%x\n",idx,cfgval);
	i2c_start();
	i2c_writeByte(addr << 1);
	if (!i2c_check_ack()) {
//		os_printf("r1\n");
		goto out_err;
	}
	i2c_writeByte(TCN_CFG);
	if (!i2c_check_ack()) {
//		os_printf("r2\n");
		goto out_err;
	}
	// place all in powerdown mode
	i2c_writeByte(cfgval);
	if (!i2c_check_ack()) {
//		os_printf("r3\n");
		goto out_err;
	}

	rc = 1;
out_err:
	i2c_stop();
	return rc;

}

static void ICACHE_FLASH_ATTR tcn75_read_all(void *arg)
{
	int i;
	uint8 dtmp1, dtmp2;
	// set in one shot mode
	i2c_init();

	for (i=0; i < 8; i++) {
		uint8_t addr =  TCN_BASE_ADDR | i;

		if (!(present&(1<<i))) {
			continue;
		}

		i2c_start();
		i2c_writeByte(addr << 1);
		if (!i2c_check_ack()) {
			i2c_stop();
			data[i] |= INVALID_READING;
			continue;
		}
		i2c_writeByte(TCN_TA);
		if (!i2c_check_ack()) {
			i2c_stop();
			data[i] |= INVALID_READING;
			continue;
		}

		i2c_stop();
		i2c_start();
		// now read
		i2c_writeByte((addr<<1)|1); // i2c read
        	if (!i2c_check_ack()) {
   			i2c_stop();
			data[i] |= INVALID_READING;
			continue;
		}

	        dtmp1=i2c_readByte();   //read MSB
       		i2c_send_ack(1);
	        dtmp2 = i2c_readByte(); //read LSB
	       	i2c_send_ack(0);        //NACK READY FOR STOP
		data[i]= ((dtmp1<<8)|dtmp2);
		i2c_stop();
	}
}

static void ICACHE_FLASH_ATTR tcn75_start_oneshot(void *arg)
{
	int i;
    i2c_init();

	for (i=0; i < 8; i++) {
		if (present & (1<<i)) {
			if (!tcn75_set_cfg(i, TCN_CONFIG | TCN_CFG_ONE_SHOT)) {
				os_printf("TCN:%d start err\n", i);
			}
		}
	}
	// read them after 120/250ms
	if (arg) {
		os_delay_us(280*1000);
		tcn75_read_all(NULL);
	} else {
		os_timer_disarm(&tcnread_t);
		os_timer_setfn(&tcnread_t, tcn75_read_all, NULL);
		os_timer_arm(&tcnread_t, TCN_CONV_TIME, 0);
	}

}


int ICACHE_FLASH_ATTR tcn75_init(void)
{
	int i;
	// disable alarms
	i2c_init();
	present = 0;
	for (i=0; i < 8; i++) {
		present |= tcn75_set_cfg(i, TCN_CONFIG) << i;
	}

	os_timer_disarm(&tcntimer_t);
	os_timer_setfn(&tcntimer_t, (os_timer_func_t *)tcn75_start_oneshot, NULL);
	os_timer_arm(&tcntimer_t, 10000, 1);
	tcn75_start_oneshot((void *)1);

	return present;
}


uint16_t ICACHE_FLASH_ATTR *tcn75a_read(uint8_t *pre)
{
	*pre = present;
	return data;
}


float ICACHE_FLASH_ATTR tcn75_get_temp(uint16_t data)
{
	int sign = 1;
	unsigned char c1;
	unsigned char c2;
	float t;

	c1 = data >> 8;
	c2 = data&0xff;

        if (c1 & 0x80) {
                sign = -1;
                c1 &= ~0x80;
        }
        t = c2/256.f;
        t = c1 + t;
        t = t * sign;
        return t;
}

