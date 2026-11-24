#define DT_DRV_COMPAT memsic_mmc5983ma

#define MAX_WRITE_SIZE 5 // only 5 writable registers

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include "mmc5983ma.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <math.h>
#include <inttypes.h>

LOG_MODULE_REGISTER(MMC5983MA, CONFIG_SENSOR_LOG_LEVEL);

struct mmc5983ma_config {
	struct i2c_dt_spec i2c;
	const struct gpio_dt_spec irq_gpios;
};

static void overwrite_8(void * ptr, uint8_t val) {
	*(uint8_t*)ptr = val;
}

static void overwrite_16(void * ptr, uint16_t val) {
	*(uint16_t*)ptr = val;
}

static int read_register(const struct device *dev, enum mmc5983ma_register addr, void *buf)
{
	const struct mmc5983ma_config *cfg = dev->config;
	int rc = 0;

	rc = i2c_write_read_dt(&cfg->i2c, &addr, 1U, buf, 1U);

	return rc;
}

static int read_data_block(const struct device *dev, enum mmc5983ma_register addr, void *buf, uint8_t num_bytes)
{
	const struct mmc5983ma_config *cfg = dev->config;
	int rc = 0;

	uint8_t data[1] = {addr};

	rc = i2c_write_read_dt(&cfg->i2c, data, 1U, buf, num_bytes);

	return rc;
}

static int write_register(const struct device *dev, enum mmc5983ma_register addr, void * value)
{
	const struct mmc5983ma_config *cfg = dev->config;
	int rc = 0;

	if (addr < REG_STATUS || addr > REG_CTRL3 || value == NULL) return -EINVAL;

	uint8_t data[2] = {addr, *(uint8_t*)value};

	rc = i2c_write_dt(&cfg->i2c, data, sizeof(data)/sizeof(data[0]));

	return rc;
}

/*
	static int mcp79412_alarm_is_pending(const struct device *dev, uint16_t alarm_id) {
		struct mcp79412_data *data = dev->data;
		struct mcp79412_alarm_registers *alm_regs;
		uint8_t alarm_reg_address;
		int ispending = 0;

		if (alarm_id == ALARM0_ID) {
			alarm_reg_address = REG_ALM0_WDAY;
			alm_regs = &data->alm0_registers;
		} else if (alarm_id == ALARM1_ID) {
			alarm_reg_address = REG_ALM1_WDAY;
			alm_regs = &data->alm1_registers;
		} else {
			return -EINVAL;
		}

		k_sem_take(&data->lock, K_FOREVER);

		int rc = read_register(dev, alarm_reg_address, (uint8_t *)&alm_regs->alm_weekday);

		if (rc != 0) goto out;

		ispending = alm_regs->alm_weekday.alm_if;
		alm_regs->alm_weekday.alm_if = 0;

		rc = write_register(dev, alarm_reg_address, *(uint8_t *)&alm_regs->alm_weekday);

	out:
		k_sem_give(&data->lock);

		if (rc != 0) return rc;
		return ispending;
	}

	static void mcp79412_handle_interrupt(const struct device *dev, uint16_t alarm_id)
	{
		struct mcp79412_data *data = dev->data;
		rtc_alarm_callback cb;
		struct rtc_time time = {0};
		bool fire_callback = false;

		if (!(alarm_id == ALARM0_ID || alarm_id == ALARM1_ID)) return;

		bool pending = mcp79412_alarm_is_pending(dev, alarm_id);
		k_sem_take(&data->lock, K_FOREVER);

		// Check if this alarm has a pending interrupt 
		if (pending) {
			// Fire callback
			if (data->rtc_handler[alarm_id]) {
				cb = data->rtc_handler[alarm_id];
				time = data->callback_time[alarm_id];
				fire_callback = true;
			}
		}

		k_sem_give(&data->lock);

		if (fire_callback) {
			cb(data->mcp79412, alarm_id, data->alarm_user_data[alarm_id]);
		}
	}

	static void mcp79412_work_handler(struct k_work *work)
	{
		struct mcp79412_data *data =
			CONTAINER_OF(work, struct mcp79412_data, alarm_work);

		// Check interrupt flags for both alarms
		mcp79412_handle_interrupt(data->mcp79412, ALARM0_ID);
		mcp79412_handle_interrupt(data->mcp79412, ALARM1_ID);
	}

	static void mcp79412_init_cb(const struct device *dev,
					 struct gpio_callback *gpio_cb, uint32_t pins)
	{
		struct mcp79412_data *data =
			CONTAINER_OF(gpio_cb, struct mcp79412_data, int_callback);

		ARG_UNUSED(pins);

		(void)k_work_submit(&data->alarm_work);
	}
*/

static void channel_magn_convert(struct sensor_value * val, struct mmc5983ma_reg_mag reg) {
	//TODO tempeature compensation
	double step = 0.25; //.25mG lsb
	uint16_t regval = ((*(uint16_t*)&reg)>>8) | ((*(uint16_t*)&reg)<<8);
	double dblval = step * regval - 0x2000;
	double integer;

	val->val2 = (int32_t)(modf(dblval, &integer) * 1000000);
	val->val1 = integer;
}

static void channel_temp_convert(struct sensor_value * val, struct mmc5983ma_reg_temp reg) {
	double lowest_temp 	= -75; // ˚C
	double step 		= 0.8; // ˚C
	double temp = lowest_temp + reg.temp * step;
	double integer;

	val->val2 = (int32_t)(modf(temp, &integer) * 1000000);
	val->val1 = integer;
}

static int mmc5983ma_channel_get( 	const struct device * dev,
									enum sensor_channel chan,
									struct sensor_value * val ) {
	struct mmc5983ma_data *data = dev->data;
	k_sem_take(&data->lock, K_FOREVER);
	if (chan == SENSOR_CHAN_MAGN_X) {
		channel_magn_convert(val, data->registers.Xout);
	} else if (chan == SENSOR_CHAN_MAGN_Y) {
		channel_magn_convert(val, data->registers.Yout);
	} else if (chan == SENSOR_CHAN_MAGN_Z) {
		channel_magn_convert(val, data->registers.Zout);
	} else if (chan == SENSOR_CHAN_MAGN_XYZ) {
		channel_magn_convert(&val[0], data->registers.Xout);
		channel_magn_convert(&val[1], data->registers.Yout);
		channel_magn_convert(&val[2], data->registers.Zout);
	} else if (chan == SENSOR_CHAN_DIE_TEMP) {
		channel_temp_convert(val, data->registers.Tout);
	} else {
		k_sem_give(&data->lock);
		return -ENOTSUP;
	}
	k_sem_give(&data->lock);
	return 0;
}

static int mmc5983ma_sample_fetch(const struct device *dev, enum sensor_channel chan) {
	struct mmc5983ma_data *data = dev->data;
	uint8_t buf[8];
	int ret = -ENOTSUP;

	k_sem_take(&data->lock, K_FOREVER);
	if (chan == SENSOR_CHAN_ALL) {
		ret = read_data_block(dev, REG_XOUT0, buf, 8);
		if (ret == 0) {
			overwrite_16(&data->registers.Xout, (int16_t)sys_get_le16(&buf[0]));
			overwrite_16(&data->registers.Yout, (int16_t)sys_get_le16(&buf[2]));
			overwrite_16(&data->registers.Zout, (int16_t)sys_get_le16(&buf[4]));
			overwrite_8(&data->registers.XYZout, buf[6]);
			overwrite_8(&data->registers.Tout, buf[7]);
		} else {
			LOG_ERR("Failed to get sample, ret: %d", ret);
			overwrite_16(&data->registers.Xout, 0);
			overwrite_16(&data->registers.Yout, 0);
			overwrite_16(&data->registers.Zout, 0);
			overwrite_8(&data->registers.Tout, 0);
		}
	} else if (chan == SENSOR_CHAN_MAGN_X) {
		ret = read_data_block(dev, REG_XOUT0, buf, 2);
		if (ret == 0) {
			overwrite_16(&data->registers.Xout, (int16_t)sys_get_le16(&buf[0]));
		} else {
			LOG_ERR("Failed to get sample, ret: %d", ret);
			overwrite_16(&data->registers.Xout, 0);
		}
	} else if (chan == SENSOR_CHAN_MAGN_Y) {
		ret = read_data_block(dev, REG_YOUT0, buf, 2);
		if (ret == 0) {
			overwrite_16(&data->registers.Yout, (int16_t)sys_get_le16(&buf[0]));
		} else {
			LOG_ERR("Failed to get sample, ret: %d", ret);
			overwrite_16(&data->registers.Yout, 0);
		}
	} else if (chan == SENSOR_CHAN_MAGN_Z) {
		ret = read_data_block(dev, REG_ZOUT0, buf, 2);
		if (ret == 0) {
			overwrite_16(&data->registers.Zout, (int16_t)sys_get_le16(&buf[0]));
		} else {
			LOG_ERR("Failed to get sample, ret: %d", ret);
			overwrite_16(&data->registers.Zout, 0);
		}
	} else if (chan == SENSOR_CHAN_DIE_TEMP) {
		ret = read_register(dev, REG_TOUT, buf);
		if (ret == 0) {
			overwrite_8(&data->registers.Tout, buf[0]);
		} else {
			LOG_ERR("Failed to get sample, ret: %d", ret);
			overwrite_8(&data->registers.Tout, 0);
		}
	}
	k_sem_give(&data->lock);
	return ret;
}

static int mmc5983ma_set_samp_freq(const struct device *dev, uint16_t freq) {
	struct mmc5983ma_data *data = dev->data;
	int ret = -EINVAL;
	uint8_t Cmm_en = 0;
	uint16_t avail_freq[] = {	0, 
								1, 
								10,
								20, 
								50, 
								100, 
								200, // 200 -> bw = 01 or 11
								1000}; // 1000 -> bw = 11
	for (uint8_t i=0;i<sizeof(avail_freq)/sizeof(avail_freq[0]);i++) {
		if (freq == avail_freq[i]) {
			k_sem_take(&data->lock, K_FOREVER);
			uint8_t bw = data->registers.Control1.bandwith;
			if (freq == 200 && bw == 0) {
				LOG_ERR("Frequency too high for configured bandwidth (200Hz requires bandwith of 200Hz or higher)");
			} else if (freq == 1000 && bw != 3) {
				LOG_ERR("Frequency too high for configured bandwidth (1000Hz requires bandwith of 800Hz)");
			} else {
				data->registers.Control2.cm_freq = i;
				data->registers.Control2.cmm_en = Cmm_en;
				ret = write_register(dev, REG_CTRL2, &data->registers.Control2);
			}
			k_sem_give(&data->lock);
			break;
		}
		Cmm_en = 1;
	}
	return ret;
}

// set sensor resolution / bandwidth (0 = 100Hz, 1 = 200Hz, 2 = 400Hz, 3 = 800Hz)
static int mmc5983ma_set_res(const struct device *dev, uint8_t res) {
	struct mmc5983ma_data *data = dev->data;
	uint16_t avail_freq[] = {	0, 
								1, 
								10,
								20, 
								50, 
								100, 
								200, // 200 -> bw = 01 or 11
								1000}; // 1000 -> bw = 11
	if (res > 3) return -EINVAL;
	uint8_t ret = -EINVAL;
	k_sem_take(&data->lock, K_FOREVER);
	if (avail_freq[data->registers.Control2.cm_freq] == 200 && res == 0) {
		LOG_ERR("Frequency too high for configured bandwidth (200Hz requires bandwith of 200Hz or higher)");
	} else if (avail_freq[data->registers.Control2.cm_freq] == 1000 && res != 3) {
		LOG_ERR("Frequency too high for configured bandwidth (1000Hz requires bandwith of 800Hz)");
	} else {
		data->registers.Control1.bandwith = res;
		ret = write_register(dev, REG_CTRL1, &data->registers.Control1);
	}
	k_sem_give(&data->lock);
	return ret;
}

static int mmc5983ma_attr_set(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, const struct sensor_value *val)
{
	int ret = -ENOTSUP;

	if ((chan == SENSOR_CHAN_MAGN_X) || (chan == SENSOR_CHAN_MAGN_Y)
	    || (chan == SENSOR_CHAN_MAGN_Z)
	    || (chan == SENSOR_CHAN_MAGN_XYZ)) {
		switch (attr) {
			case SENSOR_ATTR_SAMPLING_FREQUENCY:
				if ((val->val2 != 0)) {
					ret = -EINVAL;
					break;
				}
				uint8_t freq = val->val1;
				ret = mmc5983ma_set_samp_freq(dev, freq);
				break;
			case SENSOR_ATTR_RESOLUTION:
				if ((val->val2 != 0)) {
					ret = -EINVAL;
					break;
				}
				uint8_t res = val->val1;
				ret = mmc5983ma_set_res(dev, res);
				break;
			default:
				ret = -ENOTSUP;
		}
	}

	return ret;
}

static int mmc5983ma_init(const struct device *dev)
{
	const struct mmc5983ma_config *cfg = dev->config;
	struct mmc5983ma_data *data = dev->data;
	int rc = 0;

	// Initialize and take the lock
	k_sem_init(&data->lock, 0, 1);


	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("Device not ready\n");
		rc = -ENODEV;
		goto out;
	}
	
	rc = read_register(dev, REG_ID, &data->registers.ID);
	if (data->registers.ID.ID != 0x30 || rc != 0) {
		LOG_ERR("Failed to read device ID, expected 0x30, got %01x, retcode %d\n", data->registers.ID.ID, rc);
		rc = -EIO;
		goto out;
	}

	data->registers.Control0.auto_sr_en = 1;
	rc = write_register(dev, REG_CTRL0, &data->registers.Control0);

	LOG_INF("MMC5983MA init complete");

	out:
	k_sem_give(&data->lock);
	return rc;
}

static DEVICE_API(sensor, mmc5983ma_api) = {
	.sample_fetch = mmc5983ma_sample_fetch,
	.channel_get = mmc5983ma_channel_get,
	.attr_set = mmc5983ma_attr_set,
#if defined(CONFIG_MMC5983MA_TRIGGER)
	.trigger_set = mmc5983ma_trigger_set,
#endif
};

#define INST_DT_MMC5983MA(index)                                                         \
											\
	static struct mmc5983ma_data mmc5983ma_data_##index;				\
											\
	static const struct mmc5983ma_config mmc5983ma_config_##index = {			\
		.i2c = I2C_DT_SPEC_INST_GET(index),					\
		.irq_gpios = GPIO_DT_SPEC_INST_GET_OR(index, irq_gpios, {0}),		\
	};										\
											\
	DEVICE_DT_INST_DEFINE(index, mmc5983ma_init, NULL,				\
		    &mmc5983ma_data_##index,						\
		    &mmc5983ma_config_##index,						\
		    POST_KERNEL,							\
		    CONFIG_SENSOR_INIT_PRIORITY,					\
		    &mmc5983ma_api);

DT_INST_FOREACH_STATUS_OKAY(INST_DT_MMC5983MA);
