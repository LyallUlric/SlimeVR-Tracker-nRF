#include <math.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>

#include "LIS2MDL.h"

static const float sensitivity = 1.5 / 1000; // ~1.5 mgauss/LSB

static uint8_t last_odr = 0xff;
static float last_time = 0;

LOG_MODULE_REGISTER(LIS2MDL, LOG_LEVEL_DBG);

int lis2_init(const struct i2c_dt_spec *dev_i2c, float time, float *actual_time)
{
	// nothing to initialize..
	last_odr = 0xff; // reset last odr
	int err = lis2_update_odr(dev_i2c, time, actual_time);
	return (err < 0 ? err : 0);
}

void lis2_shutdown(const struct i2c_dt_spec *dev_i2c)
{
	last_odr = 0xff; // reset last odr
	int err = i2c_reg_write_byte_dt(dev_i2c, LIS2MDL_CFG_REG_A, 0x20);
	if (err)
		LOG_ERR("I2C error");
}

int lis2_update_odr(const struct i2c_dt_spec *dev_i2c, float time, float *actual_time)
{
	int ODR;
	uint8_t MODR;
	uint8_t MD;
	last_time = time;

	if (time <= 0) // off
	{
		MD = MD_IDLE;
		ODR = 0;
	}
	else if (time == INFINITY) // oneshot/single
	{
//		MD = MD_SINGLE;
//		ODR = 0;
		MD = MD_IDLE; // No idea if single measurements will be fast enough, so just use continuous anyway
		ODR = 0;
	}
	else
	{
		MD = MD_CONTINUOUS;
		ODR = 1 / time;
	}

	if (MD == MD_IDLE)
	{
		MODR = 0;
		time = 0; // off
	}
	else if (ODR > 50) // TODO: this sucks
	{
		MODR = ODR_100Hz;
		time = 1.0 / 100;
	}
	else if (ODR > 20)
	{
		MODR = ODR_50Hz;
		time = 1.0 / 50;
	}
	else if (ODR > 10)
	{
		MODR = ODR_20Hz;
		time = 1.0 / 20;
	}
	else if (ODR > 0)
	{
		MODR = ODR_10Hz;
		time = 1.0 / 10;
	}
	else
	{
		MODR = 0;
		time = INFINITY;
	}

	if (last_odr == MODR)
		return -1;
	else
		last_odr = MODR;

	int err = i2c_reg_write_byte_dt(dev_i2c, LIS2MDL_CFG_REG_A, MODR << 2 | MD); // set mag ODR and MD
	if (err)
		LOG_ERR("I2C error");

	*actual_time = time;
	return err;
}

void lis2_mag_oneshot(const struct i2c_dt_spec *dev_i2c)
{
	// write MD_SINGLE again to trigger a measurement
	last_odr = 0xff;
	lis2_update_odr(dev_i2c, last_time, &last_time);
}

void lis2_mag_read(const struct i2c_dt_spec *dev_i2c, float m[3])
{
	int err = 0;
	uint8_t status;
	while ((status & 0x03) == MD_SINGLE) // wait for oneshot to complete
		err |= i2c_reg_read_byte_dt(dev_i2c, LIS2MDL_CFG_REG_A, &status);
	uint8_t rawData[6];
	err |= i2c_burst_read_dt(dev_i2c, LIS2MDL_OUTX_L_REG, &rawData[0], 6);
	if (err)
		LOG_ERR("I2C error");
	for (int i = 0; i < 3; i++) // x, y, z
	{
		m[i] = (int16_t)((((int16_t)rawData[(i * 2) + 1]) << 8) | rawData[i * 2]);
		m[i] *= sensitivity;
	}
}

float lis2_temp_read(const struct i2c_dt_spec *dev_i2c)
{
	uint8_t rawTemp[2];
	int err = i2c_burst_read_dt(dev_i2c, LIS2MDL_TEMP_OUT_L_REG, &rawTemp[0], 2);
	// The output value is expressed as a signed 16-bit byte in two’s complement.
	// The four most significant bits contain a copy of the sign bit.
	// The nominal sensitivity is 8 LSB/°C
	float temp = (int16_t)((((int16_t)rawTemp[1]) << 8) | rawTemp[0]);
	temp /= 8;
	// No value offset?
	if (err)
		LOG_ERR("I2C error");
	return temp;
}

const sensor_mag_t sensor_mag_lis2mdl = {
	*lis2_init,
	*lis2_shutdown,

	*lis2_update_odr,

	*lis2_mag_oneshot,
	*lis2_mag_read,
	*lis2_temp_read
};