#ifndef ZEPHYR_DRIVERS_RTC_MCP79412_
#define ZEPHYR_DRIVERS_RTC_MCP79412_

#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/rtc.h>
#include <time.h>

#define MMC5983MA_BANDWIDTH_100HZ 0
#define MMC5983MA_BANDWIDTH_200HZ 1
#define MMC5983MA_BANDWIDTH_400HZ 2
#define MMC5983MA_BANDWIDTH_800HZ 3

// read only
#define MMC5983MA_REG_XOUT0 0x00
#define MMC5983MA_REG_XOUT1 0x01
#define MMC5983MA_REG_YOUT0 0x02
#define MMC5983MA_REG_YOUT1 0x03
#define MMC5983MA_REG_ZOUT0 0x04
#define MMC5983MA_REG_ZOUT1 0x05
#define MMC5983MA_REG_XYZOUT 0x06
#define MMC5983MA_REG_TOUT 0x07

// read / write
#define MMC5983MA_REG_STATUS 0x08
#define MMC5983MA_REG_CTRL0 0x09
#define MMC5983MA_REG_CTRL1 0x0A
#define MMC5983MA_REG_CTRL2 0x0B
#define MMC5983MA_REG_CTRL3 0x0C

// read only
#define MMC5983MA_REG_ID 0x2F

enum mmc5983ma_register {
	REG_XOUT0	= 0x0,
	REG_XOUT1	= 0x1,
	REG_YOUT0	= 0x2,
	REG_YOUT1	= 0x3,
	REG_ZOUT0	= 0x4,
	REG_ZOUT1	= 0x5,
	REG_XYZOUT	= 0x6,
	REG_TOUT	= 0x7,

	REG_STATUS	= 0x8,
	REG_CTRL0	= 0x9,
	REG_CTRL1	= 0xA,
	REG_CTRL2	= 0xB,
	REG_CTRL3	= 0xC,
	
	REG_ID		= 0x2F
};

struct mmc5983ma_reg_mag {
	uint8_t upper_byte : 8;
	uint8_t lower_byte : 8;
} __packed;

struct mmc5983ma_reg_mag_lowest_bits {
	uint8_t _ : 2;
	uint8_t Z : 2;
	uint8_t Y : 2;
	uint8_t X : 2;
} __packed;

struct mmc5983ma_reg_temp {
	uint8_t temp : 8;
} __packed;

struct mmc5983ma_reg_status {
	uint8_t meas_m_done : 1;
	uint8_t meas_t_done : 1;
	uint8_t resv1 : 2;
	uint8_t otp_read_done : 1;
	uint8_t resv0 : 3;
} __packed;

struct mmc5983ma_reg_ctrl0 {
	uint8_t tm_m : 1;
	uint8_t tm_t : 1;
	uint8_t int_meas_done_en : 1;
	uint8_t set : 1;
	uint8_t reset : 1;
	uint8_t auto_sr_en : 1;
	uint8_t otp_read : 1;
	uint8_t resv : 1;
} __packed;

struct mmc5983ma_reg_ctrl1 {
	uint8_t bandwith : 2;
	uint8_t x_inhibit : 1;
	uint8_t yz_inhibit : 2;
	uint8_t resv : 2;
	uint8_t sw_rst : 1;
} __packed;

struct mmc5983ma_reg_ctrl2 {
	uint8_t cm_freq : 3;
	uint8_t cmm_en : 1;
	uint8_t prd_set : 3;
	uint8_t en_prd_set : 1;
} __packed;

struct mmc5983ma_reg_ctrl3 {
	uint8_t resv0 : 1;
	uint8_t st_enp : 1;
	uint8_t st_enm : 1;
	uint8_t resv1 : 3;
	uint8_t spi_3w : 1;
	uint8_t resv2 : 1;
} __packed;

struct mmc5983ma_reg_id {
	uint8_t ID : 8;
} __packed;

struct mmc5983ma_registers {
	struct mmc5983ma_reg_mag Xout;
	struct mmc5983ma_reg_mag Yout;
	struct mmc5983ma_reg_mag Zout;
	struct mmc5983ma_reg_mag_lowest_bits XYZout;
	struct mmc5983ma_reg_temp Tout;
	struct mmc5983ma_reg_status Status;
	struct mmc5983ma_reg_ctrl0 Control0;
	struct mmc5983ma_reg_ctrl1 Control1;
	struct mmc5983ma_reg_ctrl2 Control2;
	struct mmc5983ma_reg_ctrl3 Control3;
	struct mmc5983ma_reg_id ID;
} __packed;



union mmc5983ma_bus {
    struct i2c_dt_spec i2c;
};

typedef int (*mmc5983ma_bus_check_fn)(const union mmc5983ma_bus *bus);
typedef int (*mmc5983ma_bus_init_fn)(const union mmc5983ma_bus *bus);
typedef int (*mmc5983ma_reg_read_fn)(const union mmc5983ma_bus *bus,
				  uint8_t start,
				  uint8_t *data,
				  uint16_t len);
typedef int (*mmc5983ma_reg_write_fn)(const union mmc5983ma_bus *bus,
				   uint8_t start,
				   const uint8_t *data,
				   uint16_t len);

struct mmc5983ma_bus_io {
	mmc5983ma_bus_check_fn check;
	mmc5983ma_reg_read_fn read;
	mmc5983ma_reg_write_fn write;
	mmc5983ma_bus_init_fn init;
};

struct mmc5983ma_feature_config {
	const char *name;
	const uint8_t *config_file;
	size_t config_file_len;
};

struct mmc5983ma_data {
	const struct device *mmc5983ma;
	struct mmc5983ma_registers registers;
	struct k_sem lock;

	struct k_work alarm_work;
	struct gpio_callback int_callback;

	rtc_alarm_callback rtc_handler[2];
	struct rtc_time callback_time[2];
	uint16_t alarm_mask[2];
	void *alarm_user_data[2];

	bool int_active_high;
};

#endif