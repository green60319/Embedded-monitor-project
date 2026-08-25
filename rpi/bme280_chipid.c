#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <termios.h>

static int read_reg(int fd, uint8_t reg, uint8_t *value){

	uint8_t tx[2] = {reg, 0x00};
	uint8_t rx[2] = {0};

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = 2,
		.bits_per_word = 8,
	};

	if(ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0){
		perror("SPI transfer");
		return -1;
	}

	*value = rx[1];
	return 0;
}

static int write_reg(int fd, uint8_t reg, uint8_t value){
	
	uint8_t tx[2] = {reg & 0x7F, value};

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.len = 2,
		.bits_per_word = 8,
	};

	if(ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0){
		perror("SPI write");
		return -1;
	}

	return 0;
}

static int read_regs(int fd, uint8_t reg, uint8_t *buf, int len){
		
	uint8_t tx[len + 1];
	uint8_t rx[len + 1];

	tx[0] = reg | 0x80;

	for(int i = 1; i <= len; i++)
		tx[i] = 0x00;

	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = len + 1,
		.bits_per_word = 8,
	};

	if(ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0){
		perror("SPI multi read");
		return -1;
	}

	for(int i = 0; i < len; i++)
		buf[i] = rx[i + 1];

	return 0;
}
int main(void)
{
    int fd;
    uint8_t mode = SPI_MODE_0;
    uint32_t speed = 500000;
    uint8_t chipid;
    uint8_t status;
    uint8_t ctrl_meas;
    uint8_t config;
    int timer_fd;
    int epfd;
    int uart_fd;

    struct epoll_event ev;
    struct epoll_event events[1];

    uint64_t expirations;
    uint8_t data[8];
    uint32_t raw_temp;
    uint16_t raw_hum;

    fd = open("/dev/spidev0.0", O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    uart_fd = open("/dev/ttyAMA2", O_RDWR | O_NOCTTY);

    if(uart_fd < 0){
	perror("UART open");
	close(fd);
	return 1;
    }

    struct termios tty;

    if(tcgetattr(uart_fd, &tty) != 0){
	perror("tcgetattr");
	close(uart_fd);
	close(fd);
	return 1;
    }

    cfmakeraw(&tty);

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CLOCAL | CREAD;

    if(tcsetattr(uart_fd, TCSANOW, &tty) != 0){
	perror("tcsetattr");
	close(uart_fd);
	close(fd);
	return 1;
    }

    timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);



    if(timer_fd < 0){
	perror("timerfd_create");
	close(fd);
	return 1;
    }

    struct itimerspec timer_spec = {0};

    timer_spec.it_value.tv_sec = 1;
    timer_spec.it_interval.tv_sec = 1;

    if(timerfd_settime(timer_fd, 0, &timer_spec, NULL) < 0){
	perror("timerfd_settime");
	close(timer_fd);
	close(fd);
	return 1;
    }

    epfd = epoll_create1(0);

    if(epfd < 0){
	perror("epoll_create1");
	close(timer_fd);
	close(fd);
	return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = timer_fd;

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev) < 0){
	perror("epoll_ctl");
	close(epfd);
	close(timer_fd);
	close(fd);
	return 1;
    }

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI mode");
        close(fd);
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI speed");
        close(fd);
        return 1;
    }

    write_reg(fd, 0xF2, 0x01);
    write_reg(fd, 0xF4, 0x27);
    write_reg(fd, 0xF5, 0x00);

    read_reg(fd, 0xD0, &chipid);
    read_reg(fd, 0xF3, &status);
    read_reg(fd, 0xF4, &ctrl_meas);
    read_reg(fd, 0xF5, &config);

    read_regs(fd, 0xF7, data, 8);

    uint8_t calib[6];
    read_regs(fd, 0x88, calib, 6);

    uint16_t dig_T1 =
	((uint16_t)calib[1] << 8) | calib[0];
   
    int16_t dig_T2 =
	((uint16_t)calib[3] << 8) | calib[2];

    int16_t dig_T3 =
	((uint16_t)calib[5] << 8) | calib[4];

    printf("dig_T1 = %u\n", dig_T1);
    printf("dig_T2 = %d\n", dig_T2);
    printf("dig_T3 = %d\n", dig_T3);

    uint8_t dig_H1;
    uint8_t hum_calib[7];

    read_reg(fd, 0xA1, &dig_H1);
    read_regs(fd, 0xE1, hum_calib, 7);

    int16_t dig_H2 =
	((int16_t)hum_calib[1] << 8) | hum_calib[0];

    uint8_t dig_H3 = hum_calib[2];

    int8_t dig_H6 = (int8_t)hum_calib[6];

    int16_t dig_H4 =
	((int16_t)hum_calib[3] << 4) |
	(hum_calib[4] & 0x0F);
   
    int16_t dig_H5 =
	((int16_t)hum_calib[5] << 4) |
	(hum_calib[4] >> 4);

    printf("dig_H1 = %u\n", dig_H1);
    printf("dig_H2 = %d\n", dig_H2);
    printf("dig_H3 = %u\n", dig_H3);
    printf("dig_H4 = %d\n", dig_H4);
    printf("dig_H5 = %d\n", dig_H5);
    printf("dig_H6 = %d\n", dig_H6);

    uint32_t raw_press =
	((uint32_t)data[0] << 12) |
	((uint32_t)data[1] << 4) |
	((uint32_t)data[2] >> 4);
    
    raw_temp =
	((uint32_t)data[3] << 12) |
	((uint32_t)data[4] << 4) |
	((uint32_t)data[5] >> 4);

    raw_hum =
	((uint16_t)data[6] << 8) |
	data[7];

    int32_t var1, var2, t_fine;
    int32_t temperature;
    int32_t temp_diff;

    var1 = ((((int32_t)raw_temp >> 3) - ((int32_t)dig_T1 << 1)) *
		((int32_t)dig_T2)) >> 11;

    temp_diff = ((int32_t)raw_temp >> 4) - (int32_t)dig_T1;

    var2 = (((temp_diff * temp_diff) >> 12) *
		(int32_t)dig_T3) >> 14;

    t_fine = var1 + var2;

    temperature = (t_fine * 5 + 128) >> 8;

    printf("Temperature = %.2f C\n", temperature / 100.0);

    float temp_c = temperature / 100.0;

    if(temp_c >= 35.0)
	printf("STATE = ALARM\n");
    else
	printf("STATE = NORMAL\n");

    printf("Raw Pressure = %u\n", raw_press);
    printf("Raw Temperature = %u\n", raw_temp);
    printf("Raw Humidity = %u\n", raw_hum);

    printf("Chip ID = 0x%02X\n", chipid);
    printf("Status  = 0x%02X\n", status);
    printf("Ctrl Meas = 0x%02X\n", ctrl_meas);
    printf("Config  = 0x%02X\n", config);
 
    while(1){
	int n = epoll_wait(epfd, events, 1, -1);

	if(n < 0){
		perror("epoll_wait");
		break;
	}

	if(events[0].data.fd == timer_fd){
		read(timer_fd, &expirations, sizeof(expirations));

 		printf("Timer event received\n");
		read_regs(fd, 0xF7, data, 8);

		raw_temp =
			((uint32_t)data[3] << 12) |
			((uint32_t)data[4] << 4) |
			((uint32_t)data[5] >> 4);

		raw_hum =
			((uint16_t)data[6] << 8) |
			data[7];

		temp_diff = ((int32_t)raw_temp >> 4) - (int32_t)dig_T1;

		var1 = ((((int32_t)raw_temp >> 3) - ((int32_t)dig_T1 << 1)) *
			((int32_t)dig_T2)) >> 11;

		var2 = (((temp_diff * temp_diff) >> 12) *
			(int32_t)dig_T3) >> 14;

		t_fine = var1 + var2;
		temperature = (t_fine * 5 + 128) >> 8;
		printf("Temperature = %.2f C\n", temperature / 100.0);

		int32_t hum;
    		hum = t_fine - 76800;

    		int32_t hum_a;
    		int32_t hum_b;

    		hum_a = (((int32_t)raw_hum << 14) -
		((int32_t)dig_H4 << 20) -
		((int32_t)dig_H5 * hum) +
		16384) >> 15;
   
    		hum_b = ((((hum * (int32_t)dig_H6) >> 10) *
			(((hum * (int32_t)dig_H3) >> 11) + 32768)) >> 10);

    		hum_b = (((hum_b + 2097152) *
			(int32_t)dig_H2 + 8192) >> 14);

    		hum = hum_a * hum_b;

    		hum = hum - (((((hum >> 15) * (hum >> 15)) >> 7) *
			(int32_t)dig_H1) >> 4);

    		if(hum < 0)
			hum = 0;

    		if(hum > 419430400)
			hum = 419430400;

    		float humidity = (hum >> 12) / 1024.0f;

    		printf("Humidity = %.2f %%\n", humidity);

		if((temperature / 100.0) >= 30.0){
			printf("STATE = ALARM\n");
			write(uart_fd, "SERVO_OFF\n", 10);
			printf("UART TX = SERVO_OFF\n");
		}
		else{
			printf("STATE = NORMAL\n");
			write(uart_fd, "SERVO_ON\n", 9);
			printf("UART TX = SERVO_ON\n");
		}
	  }
    }

    close(fd);
    return 0;
}
