 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

static ssize_t bme280_read(struct file *file, char __user *buf, size_t count, loff_t *ppos){

	char kbuf[64];
	int len;
	size_t bytes_to_copy;

	len = snprintf(kbuf, sizeof(kbuf), "BME280 driver read test\n");

	if(*ppos > 0)
		return 0;

	bytes_to_copy = min_t(size_t, count, len);

	if(copy_to_user(buf, kbuf, bytes_to_copy))
		return -EFAULT;

	*ppos += bytes_to_copy;

	return bytes_to_copy;
}

static const struct file_operations bme280_fops = {
	.owner = THIS_MODULE,
	.read = bme280_read,
};

static struct miscdevice bme280_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "bme280",
	.fops = &bme280_fops,
};

static int bme280_probe(struct spi_device *spi){
	
	int ret;

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

	u8 reg = 0xD0;
	u8 chip_id = 0;
	
	ret = spi_write_then_read(spi, &reg, 1, &chip_id, 1);
	if(ret < 0){
		dev_err(&spi->dev, "spi_write_then_read failed: %d\n", ret);
		return ret;
	}

	dev_info(&spi->dev, "Chip ID = 0x%02x\n", chip_id);

	ret = misc_register(&bme280_misc_device);
	if(ret < 0){
		dev_err(&spi->dev, "Failed to register misc device\n");
		return ret;
	}

	return 0;
}

static void bme280_remove(struct spi_device *spi){

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
