#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <termios.h>

int main()
{
    int fd;
    int uart_fd;
    int epfd;
    int n;
    char buf[128];
    int last_state = -1;

    struct epoll_event ev;
    struct epoll_event events[1];

    fd = open("/dev/bme280", O_RDONLY);

    if(fd < 0)
    {
        perror("open");
        return 1;
    }

    uart_fd = open("/dev/ttyAMA2", O_RDWR | O_NOCTTY);

    if(uart_fd < 0)
    {
    	perror("UART open");
    	close(fd);
    	return 1;
    }

    struct termios tty;

    if(tcgetattr(uart_fd, &tty) != 0)
    {
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

    if(tcsetattr(uart_fd, TCSANOW, &tty) != 0)
    {
    	perror("tcsetattr");
    	close(uart_fd);
    	close(fd);
    	return 1;
    }

    epfd = epoll_create1(0);

    if(epfd < 0)
    {
        perror("epoll_create1");
        close(fd);
        return 1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        perror("epoll_ctl");
        close(fd);
        close(epfd);
        return 1;
    }

    printf("Waiting for BME280 event...\n");

    while(1)
    {
        n = epoll_wait(epfd, events, 1, -1);

        if(n < 0)
        {
            perror("epoll_wait");
            break;
        }

        if(events[0].data.fd == fd)
        {
            lseek(fd, 0, SEEK_SET);

            int len = read(fd, buf, sizeof(buf) - 1);

            if(len > 0)
            {

		float temperature;
		float humidity;

                buf[len] = '\0';

                printf("Temperature/Hunidity: %s", buf);

		if(sscanf(buf,
			"Temperature=%f C Humidity=%f %%",
			&temperature,
			&humidity) == 2)
		{
			if(temperature >= 25.0)
			{
				printf("System State: ALARM\n");
				
				if(last_state != 1){			
					write(uart_fd, "SERVO_OFF\n", 10);
					printf("UART TX: SERVO_OFF\n");

					last_state = 1;
				}
			}
			else
			{
				printf("System State: NORMAL\n");

				if(last_state != 0){
					write(uart_fd, "SERVO_ON\n", 9);
					printf("UART TX: SERVO_ON\n");
				
					last_state = 0;
				}
			}
		}
            }
        }
    }

    close(fd);
    close(uart_fd);
    close(epfd);

    return 0;
}
