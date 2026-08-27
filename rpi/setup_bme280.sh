#!/bin/bash

# Load custom kernel module if not loaded
if ! lsmod | grep -q "^bme280_driver"; then
    insmod /home/pi/bme280_driver/bme280_driver.ko
fi

# If spi0.0 is using spidev, unbind it
if [ "$(basename "$(readlink /sys/bus/spi/devices/spi0.0/driver 2>/dev/null)")" = "spidev" ]; then
    echo spi0.0 > /sys/bus/spi/drivers/spidev/unbind
fi

# Select custom driver
echo bme280_custom > /sys/bus/spi/devices/spi0.0/driver_override

# Bind only if not already bound
if [ "$(basename "$(readlink /sys/bus/spi/devices/spi0.0/driver 2>/dev/null)")" != "bme280_custom" ]; then
    echo spi0.0 > /sys/bus/spi/drivers/bme280_custom/bind
fi

echo "BME280 setup complete"
