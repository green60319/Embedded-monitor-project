# Raspberry Pi 5 Embedded Linux Sensor Control System

## Overview

This project is an Embedded Linux sensor control system built with Raspberry Pi 5, BME280, Raspberry Pi Pico W, and SG90 Servo.

The system reads temperature and humidity data from BME280 through SPI, processes events in Linux Kernel/User Space, and sends UART commands to Pico W for servo control.

## System Architecture

BME280  
↓ SPI  
Linux Kernel Driver  
↓ /dev/bme280  
wait queue / poll  
↓  
User Space epoll  
↓  
Threshold Decision  
↓ UART2  
Pico W  
↓ PWM  
SG90 Servo

## Key Technologies

- C
- Embedded Linux
- Linux Kernel Driver
- Character Device
- SPI
- wait queue
- poll / epoll
- UART
- PWM / GPIO
- Buildroot

## Hardware

- Raspberry Pi 5
- BME280
- Raspberry Pi Pico W
- SG90 Servo

## Features

- Custom BME280 SPI Linux Kernel Driver
- Register access and sensor calibration / compensation
- `/dev/bme280` Character Device
- wait queue and `.poll()` support
- User Space `epoll_wait()` event-driven monitoring
- 33°C NORMAL / ALARM threshold decision
- UART2 communication from Raspberry Pi 5 to Pico W
- 50Hz PWM servo control
- Debian and Buildroot system integration

## Project Structure

```text
Embedded-monitor-project/
├── rpi/
│   ├── bme280_driver.c
│   ├── bme280_monitor.c
│   ├── bme280_chipid.c
│   ├── epoll_test.c
│   ├── bme280-overlay.dts
│   ├── bme280-setup.service
│   └── setup_bme280.sh
│
├── pico_uart_rx.c
├── CMakeLists.txt
└── pico_sdk_import.cmake
