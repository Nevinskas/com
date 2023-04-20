/*
 * Building: cc -o com com.c
 * Usage   : ./com /dev/device [speed]
 * Example : ./com /dev/ttyS0 [115200]
 * Keys    : Ctrl-A - exit, Ctrl-X - display control lines status
 * Darcs   : darcs get http://tinyserial.sf.net/
 * Homepage: http://tinyserial.sourceforge.net
 * Version : 2009-03-05
 *
 * Ivan Tikhonov, http://www.brokestream.com, kefeer@brokestream.com
 * Patches by Jim Kou, Henry Nestler, Jon Miner, Alan Horstmann
 *
 */

/* Copyright (C) 2007 Ivan Tikhonov

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Ivan Tikhonov, kefeer@brokestream.com

*/

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>

typedef struct {
	char *name;
	int flag;
} speed_spec;

#define MAX_EPOLL_EVENTS     5

const speed_spec speeds[] = { { "0", B0 },
			      { "50", B50 },
			      { "75", B75 },
			      { "110", B110 },
			      { "134", B134 },
			      { "150", B150 },
			      { "200", B200 },
			      { "300", B300 },
			      { "600", B600 },
			      { "1200", B1200 },
			      { "2400", B2400 },
			      { "4800", B4800 },
			      { "9600", B9600 },
			      { "19200", B19200 },
			      { "38400", B38400 },
			      { "57600", B57600 },
			      { "115200", B115200 },
			      { "230400", B230400 },
			      { "460800", B460800 },
			      { "500000", B500000 },
			      { "576000", B576000 },
			      { "921600", B921600 },
			      { "1000000", B1000000 },
			      { "1152000", B1152000 },
			      { "1500000", B1500000 },
			      { "2000000", B2000000 },
			      { "2500000", B2500000 },
			      { "3000000", B3000000 },
			      { "3500000", B3500000 },
			      { "4000000", B4000000 },
			      { NULL, 0 } };

void print_status(int fd)
{
	int status;
	unsigned int arg;
	status = ioctl(fd, TIOCMGET, &arg);
	fprintf(stderr, "[STATUS]: ");
	if (arg & TIOCM_RTS)
		fprintf(stderr, "RTS ");
	if (arg & TIOCM_CTS)
		fprintf(stderr, "CTS ");
	if (arg & TIOCM_DSR)
		fprintf(stderr, "DSR ");
	if (arg & TIOCM_CAR)
		fprintf(stderr, "DCD ");
	if (arg & TIOCM_DTR)
		fprintf(stderr, "DTR ");
	if (arg & TIOCM_RNG)
		fprintf(stderr, "RI ");
	fprintf(stderr, "\r\n");
}

#define COM_MAX_CHAR		256
static int transfer_data(int from, int to, int is_control)
{
	char c[COM_MAX_CHAR];
	int ret;

	do {
		ret = read(from, &c, COM_MAX_CHAR);
	} while (ret < 0 && errno == EINTR);

	if (ret == 0) {
		fprintf(stderr, "\nnothing to read. probably port disconnected. ret: %i\n", ret);
		return -3;
	}

	if (ret == -1) {
		perror("read");
		return -2;
	}

	//fprintf(stderr, "Read bytes: %i\n\r\n\r", ret);

	if (is_control) {
		/* okay, this is not the most elegant solution
		 * to check only the firs byte of data for control
		 * command...
		 */
		if (c[0] == '\x01') { // C-a
			return -1;
		} else if (c[0] == '\x18') { // C-x
			print_status(to);
			return 0;
		} else if (c[0] == '\x13') { // C-s
			fprintf(stderr, "sending break...\n\r");
			tcsendbreak(to, 0);
		}
	}
	while (write(to, &c, ret) == -1) {
		if (errno != EAGAIN && errno != EINTR) {
			perror("write failed");
			break;
		}
	}

	//fprintf(stderr, "\n\r\n\r");

	return 0;
}

static void usage(char *name)
{
	uint8_t i = 0;

	fprintf(stderr, "example: %s /dev/ttyS0 [115200]\n\n", name);
	fprintf(stderr, "available baud rates:\n");

	while (speeds[i].name)
		fprintf(stderr, "   %s%s", speeds[i++].name, (i % 3 == 2) ? "\n":"");

	fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
	int efd;
	struct epoll_event ev[MAX_EPOLL_EVENTS];

	int comfd;
	struct termios oldtio,
		newtio; //place for old and new port settings for serial port
	struct termios oldkey,
		newkey; //place tor old and new port settings for keyboard teletype
	char *devicename = argv[1];
	int quit = 0;
	int speed = B115200;

	if (argc < 2) {
		usage(argv[0]);
		exit(1);
	}

	comfd = open(devicename, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (comfd < 0) {
		perror(devicename);
		exit(-1);
	}

	if (argc > 2) {
		const speed_spec *s;
		for (s = speeds; s->name; s++) {
			if (strcmp(s->name, argv[2]) != 0)
				continue;

			speed = s->flag;
			fprintf(stderr, "setting speed %s\n", s->name);
			break;
		}
	}

	fprintf(stderr, "C-a exit, C-x modem lines status\n");

	tcgetattr(STDIN_FILENO, &oldkey);
	newkey.c_cflag = B9600 | CRTSCTS | CS8 | CLOCAL | CREAD;
	newkey.c_iflag = IGNPAR;
	newkey.c_oflag = 0;
	newkey.c_lflag = 0;
	newkey.c_cc[VMIN] = 1;
	newkey.c_cc[VTIME] = 0;
	tcflush(STDIN_FILENO, TCIFLUSH);
	tcsetattr(STDIN_FILENO, TCSANOW, &newkey);

	tcgetattr(comfd, &oldtio); // save current port settings
	newtio.c_cflag = speed | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;
	tcflush(comfd, TCIFLUSH);
	tcsetattr(comfd, TCSANOW, &newtio);

	print_status(comfd);

	efd = epoll_create(1);
	if (efd == -1)
		perror("epoll_create");

	ev[0].data.fd = STDIN_FILENO;
	ev[0].events = EPOLLIN;

	if (epoll_ctl(efd, EPOLL_CTL_ADD, STDIN_FILENO, &ev[0]))
		perror("epoll_ctl");

	ev[0].data.fd = comfd;
	ev[0].events = EPOLLIN;

	if (epoll_ctl(efd, EPOLL_CTL_ADD, comfd, &ev[0]))
		perror("epoll_ctl");

	while (!quit) {
		int n, i;

		n = epoll_wait(efd, ev, MAX_EPOLL_EVENTS, -1);
		if (n <= 0)
			perror("epoll_wait()");

		for (i = 0; i < n; i++) {
			struct epoll_event *e = &ev[i];

			if (e->data.fd == STDIN_FILENO) {
				quit = transfer_data(e->data.fd, comfd, 1);
			} else if (e->data.fd == comfd) {
				quit = transfer_data(e->data.fd, STDIN_FILENO, 0);
			} else {
				perror("fatal()");
				abort();
			}
		}
	}

	tcsetattr(comfd, TCSANOW, &oldtio);
	tcsetattr(STDIN_FILENO, TCSANOW, &oldkey);
	close(comfd);

	return 0;
}
