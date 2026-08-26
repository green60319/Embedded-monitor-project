#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>

int main()
{
    int fd;
    int epfd;
    int n;
    char buf[128];

    struct epoll_event ev;
    struct epoll_event events[1];

    fd = open("/dev/bme280", O_RDONLY);

    if(fd < 0)
    {
        perror("open");
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
                buf[len] = '\0';

                printf("Event received: %s", buf);
            }
        }
    }

    close(fd);
    close(epfd);

    return 0;
}
