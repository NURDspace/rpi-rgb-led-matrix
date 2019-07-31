#include <ctype.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "error.h"
#include "utils.h"

int hex_to_val(char c)
{
	c = tolower(c);

	if (c >= 'a')
		return c - 'a' + 10;

	return c - '0';
}

std::string format(const char *const fmt, ...)
{
	char *buffer = NULL;
        va_list ap;

	std::string result;

        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) != -1)
		result = buffer;
        va_end(ap);

	free(buffer);

	return result;
}

void hex_str_to_rgb(const std::string & in, uint8_t *const r, uint8_t *const g, uint8_t *const b)
{
	if (in.size() >= 6)
	{
		std::string work = in.size() == 7 ? in.substr(1) : in;

		*r = (hex_to_val(work.at(0)) << 4) | hex_to_val(work.at(1));
		*g = (hex_to_val(work.at(2)) << 4) | hex_to_val(work.at(3));
		*b = (hex_to_val(work.at(4)) << 4) | hex_to_val(work.at(5));
	}
}

void check_range(int *const chk_val, const int min, const int max)
{
	if (*chk_val < min)
		*chk_val = min;
	else if (*chk_val > max)
		*chk_val = max;
}

double hue_to_rgb(const double m1, const double m2, double h)
{
	while(h < 0.0)
		h += 1.0;

	while(h > 1.0)
		h -= 1.0;

	if (6.0 * h < 1.0)
		return (m1 + (m2 - m1) * h * 6.0);

	if (2.0 * h < 1.0)
		return m2;

	if (3.0 * h < 2.0)
		return (m1 + (m2 - m1) * ((2.0 / 3.0) - h) * 6.0);

	return m1;
}

void hls_to_rgb(const double H, const double L, const double S, double *const r, double *const g, double *const b)
{
	if (S == 0)
	{
		*r = *g = *b = L;
		return;
	}

	double m2 = 0;

	if (L <= 0.5)
		m2 = L * (1.0 + S);
	else
		m2 = L + S - L * S;

	double m1 = 2.0 * L - m2;

	*r = hue_to_rgb(m1, m2, H + 1.0/3.0);
	*g = hue_to_rgb(m1, m2, H);
	*b = hue_to_rgb(m1, m2, H - 1.0/3.0);
}

uint64_t get_ts()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L * 1000L  + tv.tv_usec;
}

bool WRITE(const int fd, const char *what, int len)
{
        while(len > 0) {
                int rc = write(fd, what, len);

                if (rc <= 0) {
                        printf("%s (%d)\n", strerror(errno), errno);
                        return false;
                }

                len -= rc;
                what += rc;
        }

        return true;
}

int make_socket(const char *const interface, const uint16_t port, const bool is_udp)
{
	/* Create the socket. */
	int sock = socket(PF_INET, is_udp ? SOCK_DGRAM : SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("socket");
		exit (EXIT_FAILURE);
	}

	int reuse_addr = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1) {
		perror("SO_REUSEADDR");
		exit(EXIT_FAILURE);
	}

	if (!is_udp) {
		int on = 1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(int));
	}

	/* Give the socket a name. */
	struct sockaddr_in name;
	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = inet_addr(interface);
	if (bind(sock, (struct sockaddr *) &name, sizeof (name)) < 0) {
		perror ("bind");
		exit (EXIT_FAILURE);
	}

	return sock;
}

int wait_for_data(const int fd)
{
	struct pollfd pf[1] = { { fd, POLLIN, 0 } };

	int rc = poll(pf, 1, 100);
	if (rc == -1)
		printf("poll: %s\n", strerror(errno));

	return rc;
}

