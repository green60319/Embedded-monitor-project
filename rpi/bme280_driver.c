#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/timer.h>

static struct spi_device *bme280_spi;

struct bme280_calib_data{
	u16 dig_T1;
	s16 dig_T2;
	s16 dig_T3;

	u8 dig_H1;
	s16 dig_H2;
	u8 dig_H3;
	s16 dig_H4;
	s16 dig_H5;
	s8 dig_H6;
};

static struct bme280_calib_data calib;

static DECLARE_WAIT_QUEUE_HEAD(bme280_waitq);

static int data_available = 0;

static struct timer_list bme280_timer;

static int bme280_read_reg(struct spi_device *spi, u8 reg, u8 *value){

	int ret;

	reg |= 0x80;

	ret = spi_write_then_read(spi, &reg, 1, value, 1);

	return ret;
}

static int bme280_read_regs(struct spi_device *spi, u8 reg, u8 *buf, size_t len){

	reg |= 0x80;

	return spi_write_then_read(spi, &reg, 1, buf, len);
}

static int bme280_write_reg(struct spi_device *spi, u8 reg, u8 value){

	u8 tx[2];

	tx[0] = reg & 0x7F;
	tx[1] = value;

	return spi_write(spi, tx, sizeof(tx));
}

static int bme280_read_calibration(struct spi_device *spi){

	u8 temp_calib[6];
	u8 hum_calib[7];
	int ret;
	s16 h4;
	s16 h5;

	ret = bme280_read_regs(spi, 0x88, temp_calib, 6);
	if(ret < 0)
		return ret;

	calib.dig_T1 = ((u16)temp_calib[1] << 8) |
		temp_calib[0];

	calib.dig_T2 = ((s16)temp_calib[3] << 8) |
		temp_calib[2];

	calib.dig_T3 = ((s16)temp_calib[5] << 8) |
		temp_calib[4];

	ret = bme280_read_reg(spi, 0xA1, &calib.dig_H1);
	if(ret < 0)
		return ret;

	ret = bme280_read_regs(spi, 0xE1, hum_calib, 7);
	if(ret < 0)
		return ret;

	calib.dig_H2 = ((s16)hum_calib[1] << 8) |
		hum_calib[0];

	calib.dig_H3 = hum_calib[2];

	h4 = ((s16)hum_calib[3] << 4) |
		(hum_calib[4] & 0x0F);

	h5 = ((s16)hum_calib[5] << 4) |
		(hum_calib[4] >> 4);

	if(h4 & 0x0800)
		h4 |= 0xF000;

	if(h5 & 0x0800)
		h5 |= 0xF000;

	calib.dig_H4 = h4;
	calib.dig_H5 = h5;
	calib.dig_H6 = (s8)hum_calib[6];

	return 0;
}

static int bme280_read_raw(struct spi_device *spi, u32 *raw_temp, u16 *raw_hum){

	u8 data[8];
	int ret;

	ret = bme280_read_regs(spi, 0xF7, data, 8);
	if(ret < 0)
		return ret;

	*raw_temp =
		((u32)data[3] << 12) |
		((u32)data[4] << 4) |
		((u32)data[5] >> 4);

	*raw_hum =
		((u16)data[6] << 8) |
		data[7];

	return 0;
}

static s32 bme280_compensate_temp(u32 raw_temp, s32 *t_fine){

	s32 var1;
	s32 var2;
	s32 temp_diff;
	s32 temperature;

	var1 =
		((((s32)raw_temp >> 3) -
		((s32)calib.dig_T1 << 1)) *
		((s32)calib.dig_T2)) >> 11;

	temp_diff =
		((s32)raw_temp >> 4) -
		(s32)calib.dig_T1;

	var2 =
		(((temp_diff * temp_diff) >> 12) *
		(s32)calib.dig_T3) >> 14;

	*t_fine = var1 + var2;

	temperature =
		((*t_fine * 5) + 128) >> 8;

	return temperature;
}

static u32 bme280_compensate_hum(u16 raw_hum, s32 t_fine){

	s32 hum;
	s32 hum_a;
	s32 hum_b;

	hum = t_fine - 76800;

	hum_a = 
		(((s32)raw_hum << 14) -
		((s32)calib.dig_H4 << 20) -
		((s32)calib.dig_H5 * hum) +
		16384) >> 15;

	hum_b =
		(((((hum * (s32)calib.dig_H6) >> 10) *
		(((hum * (s32)calib.dig_H3) >> 11) + 32768)) >> 10) +
		2097152) *
		((s32)calib.dig_H2 + 8192) >> 14;

	hum = hum_a * hum_b;

	hum =
		hum -
		(((((hum >> 15) * (hum >> 15)) >> 7) *
		(s32)calib.dig_H1) >> 4);

	if(hum < 0)
		hum = 0;

	if(hum > 419430400)
		hum = 419430400;

	return (u32)(hum >> 12);
}

static void bme280_timer_callback(struct timer_list *t){
	
	data_available = 1;

	wake_up_interruptible(&bme280_waitq);

	mod_timer(&bme280_timer,
		jiffies + msecs_to_jiffies(1000));
}

static __poll_t bme280_poll(struct file *file, poll_table *wait){
	
	__poll_t mask = 0;

	poll_wait(file, &bme280_waitq, wait);

	if(data_available)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static ssize_t bme280_read(struct file *file, char __user *buf, size_t count, loff_t *ppos){

	char kbuf[64];
	u32 raw_temp;
	u16 raw_hum;
	int ret;
	int len;
	size_t bytes_to_copy;
	s32 t_fine;
	s32 temperature;	
	u32 humidity;

	ret = bme280_read_raw(bme280_spi, &raw_temp, &raw_hum);
	if(ret < 0)
		return ret;
	
	temperature = bme280_compensate_temp(raw_temp, &t_fine);

	humidity = bme280_compensate_hum(raw_hum, t_fine);

	len = snprintf(kbuf, sizeof(kbuf), 
			"Temperature=%d.%02d C Humidity=%u.%03u %%\n", 
			temperature / 100, 
			temperature % 100,
			humidity / 1024,
			((humidity % 1024) * 1000) / 1024);

	/*if(*ppos > 0)
		return 0;*/

	bytes_to_copy = min_t(size_t, count, len);

	if(copy_to_user(buf, kbuf, bytes_to_copy))
		return -EFAULT;

	//*ppos += bytes_to_copy;

	data_available = 0;

	return bytes_to_copy;
}

static const struct file_operations bme280_fops = {
	.owner = THIS_MODULE,
	.read = bme280_read,
	.poll = bme280_poll,
};

static struct miscdevice bme280_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "bme280",
	.fops = &bme280_fops,
};

static int bme280_probe(struct spi_device *spi){
	
	int ret;

	bme280_spi = spi;

	dev_info(&spi->dev, "bme280_probe called\n");
	
	spi->mode = SPI_MODE_0;
	spi->max_speed_hz = 500000;
	spi->bits_per_word = 8;

	ret = spi_setup(spi);

	if(ret < 0){
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "SPI setup successful\n");

	u8 chip_id = 0;
	
	ret = bme280_read_reg(spi, 0xD0, &chip_id);
	if(ret < 0){
		dev_err(&spi->dev, "bme280_read_reg failed: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "Chip ID = 0x%02x\n", chip_id);

	ret = bme280_write_reg(spi, 0xF2, 0x01);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to set ctrl_hum: %d\n", ret);
		return ret;
	}

	ret = bme280_write_reg(spi, 0xF4, 0x27);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to set ctrl_meas: %d\n", ret);
		return ret;
	}

	ret = bme280_write_reg(spi, 0xF5, 0x00);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to set config: %d\n", ret);
		return ret;
	}

	ret = bme280_read_calibration(spi);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to read calibration data: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, 
		"T calib: T1=%u T2=%d T3=%d\n",
		calib.dig_T1,
		calib.dig_T2,
		calib.dig_T3);

	dev_info(&spi->dev, 
		"H calib: H1=%u H2=%d H3=%u H4=%d H5=%d H6=%d\n",
		calib.dig_H1,
		calib.dig_H2,
		calib.dig_H3,
		calib.dig_H4,
		calib.dig_H5,
		calib.dig_H6);
	
	ret = misc_register(&bme280_misc_device);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to register misc device\n");
		return ret;
	}

	timer_setup(&bme280_timer,
			bme280_timer_callback,
			0);

	mod_timer(&bme280_timer,
			jiffies + msecs_to_jiffies(1000));

	return 0;
}

static void bme280_remove(struct spi_device *spi){

	timer_delete_sync(&bme280_timer);

	misc_deregister(&bme280_misc_device);

	dev_info(&spi->dev, "BME280 driver removed\n");
}

static const struct of_device_id bme280_of_match[] = {

	{ .compatible = "bosch, bme280" },
	{ }
};
MODULE_DEVICE_TABLE(of, bme280_of_match);

static const struct spi_device_id bme280_id[] = {
	{ "bosch,bme280", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, bme280_id);

static struct spi_driver bme280_driver = {
	.driver = {
		.name = "bme280_custom",
		.of_match_table = bme280_of_match,
	},
	.probe = bme280_probe,
	.remove = bme280_remove,
	.id_table = bme280_id,
};

module_spi_driver(bme280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Custom BME280 SPI Driver");
