#include <stdio.h>	// printf, gets fgets....
#include <stdlib.h> // atoi, itoa....
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>


#define DEVICE_NAME "/dev/my_custom_device_driver"

// write command address only ( read = write + 1 )
// #define ADDR_SECONDS	0x80
// #define ADDR_MINUTES	0x82
// #define ADDR_HOURS		0x84
// #define ADDR_DATE		0x86
// #define ADDR_MONTH		0x88
// #define ADDR_DAYOFWEEK	0x8A
// #define ADDR_YEAR		0x8C
// #define ADDR_WRITEPROTECTED	0x8E

typedef struct
{
	uint8_t	seconds;
	uint8_t	minutes;
	uint8_t	hours;
	uint8_t	date;
	uint8_t	month;
	uint8_t	dayofweek;	// 1 : SUN , 2 : MON
	uint8_t	year;
	uint8_t ampm;		// 1 : PM , 2 : AM
	uint8_t	hourmode;	// 0 : 24hr, 1 : 12hr
}t_ds1302;

int main(void)
{
	int fd;
	int ret, len;
	char buff[200];

	t_ds1302 ds_time;

	printf("[MY_CUSTOM_DEVICE_DRIVER] ====== Start ======\n");
	
	while ((fd = open(DEVICE_NAME, O_RDWR|O_NONBLOCK)) < 0)
	{
		fprintf(stderr, "open error : %s \n", DEVICE_NAME);
		// return -1;
		sleep(2);
		// continue;
	}

	ds_time.year	= 25;
	ds_time.month	= 12;
	ds_time.date	= 29;
	ds_time.dayofweek	= 2;
	ds_time.hours	= 10;
	ds_time.minutes	= 20;
	ds_time.seconds = 30;

	len = snprintf(buff, sizeof(buff), "%02d%02d%02d%02d%02d%02d\n", ds_time.year, ds_time.month, ds_time.date, ds_time.hours, ds_time.minutes, ds_time.seconds);
	printf("  [write] %s", buff);
	ret = write(fd, buff, len);
	printf("  [write] ret: %d\n", ret);

	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	pfd.events = POLLIN;

	while (1)
	{
		// if ((fd = open(DEVICE_NAME, 0)) < 0)
		// {
		// 	fprintf(stderr, "open error : %s \n", DEVICE_NAME);
		// 	// return -1;
		// 	sleep(2);
		// 	continue;
		// }

		memset(buff, 0, sizeof(buff));
		// ret = read(fd, buff, sizeof(buff) - 1);
		ret = poll(&pfd, 1, 1000);
		if (ret < 0)
		{
			if (errno == EINTR) continue;
			perror("poll");
			break;
		}
		if (ret == 0)
		{
			ret = read(fd, buff, sizeof(buff) - 1);
			printf("[TIMEOUT] %s", buff);
			continue;
		}

		// if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        //     fprintf(stderr, "poll revents error: 0x%x\n", pfd.revents);
        //     break;
        // }

		if (pfd.revents & POLLIN)
		{
			// buff[ret] = '\0';
			// printf("[MY_CUSTOM_DEVICE_DRIVER] %s", buff);
			ret = read(fd, buff, sizeof(buff) - 1);
			printf("[POLL] %s", buff);
		}
		// sleep(2);
		// usleep(100000);		// 100 ms
	}
	close(fd);
	return 0;
}
