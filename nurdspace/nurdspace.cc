#include "led-matrix.h"
#include "graphics.h"
using namespace rgb_matrix;

#include "font.h"
#include "utils.h"
#include "frame.h"
#include "lcdproc.h"

#include <string>
#include <thread>
#include <mutex>
#include <poll.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#define SLEEP_N 10000

volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
	interrupt_received = true;
}

static int usage(const char *progname) {
	fprintf(stderr, "usage: %s [options]\n", progname);
	fprintf(stderr, "Options:\n");
	rgb_matrix::PrintMatrixFlags(stderr);
	fprintf(stderr,
			"\t-b <brightness>   : Sets brightness percent. Default: 100.\n"
			"\t-x <x-origin>     : X-Origin of displaying text (Default: 0)\n"
			"\t-y <y-origin>     : Y-Origin of displaying text (Default: 0)\n"
	       );
	return 1;
}

frame *work = NULL;

textImage *ti_idle = NULL;
std::vector<textImage *> ti_cur;
std::mutex line_lock;
int x_orig = 0, y_orig = 0;
int x = x_orig;
int y = y_orig;
bool endOfLine = false;
time_t pf_last = 0;

frame *pf = NULL;

int fromhex(int c)
{
	c = toupper(c);

	if (c >= 'A')
		return c - 'A' + 10;

	return c - '0';
}

bool handle_command(const int fd, char *const line)
{
	if (strncmp(line, "SIZE", 4) == 0) {
		char out[32];
		int len = snprintf(out, sizeof out, "SIZE %d %d\n", pf -> width(), pf -> height());

		if (fd >= 0)
			return WRITE(fd, out, len);

		return true;
	}

	// PX 20 30 ff8800
	if (line[0] != 'P' || line[1] != 'X') {
		if (line[0])
			printf("invalid header: %d %d\n", line[0], line[1]);

		return false;
	}

	int tx = atoi(&line[3]);
	char *space = strchr(&line[4], ' ');
	if (!space) {
		printf("short 1\n");
		return false;
	}

	int ty = atoi(space + 1);

	space = strchr(space + 1, ' ');
	if (!space) {
		// requesting the current pixel value
		int r = 255, g = 0, b = 0;
		pf -> getPixel(tx, ty, &r, &g, &b);

		char buffer[64];
		int len = snprintf(buffer, sizeof buffer, "PX %d %d %02x%02x%02x\n", tx, ty, r, g, b);
		//printf("%s\n", buffer);

		if (fd >= 0)
			return WRITE(fd, buffer, len);

		return true;
	}
	//printf("%d %d %s\n", cnt, p[0], p);

	char *rgb = space + 1;
	int r = (fromhex(rgb[0]) << 4) + fromhex(rgb[1]);
	int g = (fromhex(rgb[2]) << 4) + fromhex(rgb[3]);
	int b = (fromhex(rgb[4]) << 4) + fromhex(rgb[5]);

	if (r < 0 || g < 0 || b < 0 || r > 255 || g > 255 || b > 255) {
		printf("color invalid: %s\n", rgb);
		return false;
	}

	return pf -> setPixel(tx, ty, r, g, b);
}

void tcp_pixelflut_ascii_handler_do(const int fd)
{
	char buffer[4096];
	int o = 0;

	printf("client thread started %d\n", fd);

	for(;!interrupt_received;) {
		int rc = wait_for_data(fd);
		if (rc == -1)
			break;
		if (rc == 0)
			continue;

		if (o == sizeof buffer) {
			printf("buffer overflow\n");
			break;
		}

		int n = read(fd, &buffer[o], sizeof buffer - 1 - o);
		if (n <= 0) {
			printf("read error: %d\n", errno);
			break;
		}

		buffer[o + n] = 0x00;
		// printf("received: |%s|\n", &buffer[o]);

		o += n;

		// printf("before: |%s|\n", buffer);

		for(;;) {
			char *lf = strchr(buffer, '\n');
			if (!lf)
				break;

			*lf = 0x00;

			// printf("command: %s\n", buffer);
			if (!handle_command(fd, buffer)) {
				printf("invalid PX\n");
				goto fail;
			}

			int offset_lf = lf - buffer;
			int bytes_left = o - (offset_lf + 1);

			if (bytes_left == 0) {
				o = 0;
				break;
			}

			if (bytes_left < 0) {
				printf("%d bytes left\n", bytes_left);
				goto fail;
			}

			memmove(buffer, lf + 1, bytes_left);
			o = bytes_left;
			buffer[o] = 0x00;
		}

		line_lock.lock();
		pf_last = time(NULL) + 1;
		line_lock.unlock();
		// printf("after: |%s|\n\n", buffer);
	}

fail:
	printf("client thread terminating %d\n", fd);

	close(fd);
}

void tcp_pixelflut_ascii_handler(FrameCanvas *const offscreen_canvas, const int listen_port, const char *const interface)
{
	int fd = make_socket(interface, listen_port, false);

	line_lock.lock();
	const int W = offscreen_canvas -> width(), H = offscreen_canvas -> height();
	line_lock.unlock();

	printf("resolution tcp_pixelflut_ascii: %dx%d, port %d\n", W, H, listen_port);

	listen(fd, SOMAXCONN);

	for(;!interrupt_received;) {
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);

		int rc = wait_for_data(fd);
		if (rc == -1)
			break;
		if (rc == 0)
			continue;

		int new_fd = accept(fd, (struct sockaddr *)&peer, &peer_len);

		if (new_fd == -1)
			continue;

		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(peer.sin_addr), addr, INET_ADDRSTRLEN);

		printf("TCP pixelflut connection from %s: %d\n", addr, new_fd);

		try {
			std::thread t(tcp_pixelflut_ascii_handler_do, new_fd);
			t.detach();
		}
		catch(std::system_error & e) {
			printf("thread error: %s\n", e.what());
			close(new_fd);
		}
	}
}

void udp_pixelflut_ascii_handler(FrameCanvas *const offscreen_canvas, const int listen_port, const char *const interface)
{
	int fd = make_socket(interface, listen_port);

	line_lock.lock();
	const int W = offscreen_canvas -> width(), H = offscreen_canvas -> height();
	line_lock.unlock();

	printf("resolution udp_pixelflut_ascii: %dx%d, port %d\n", W, H, listen_port);

	for(;!interrupt_received;) {
		char buffer[65536];
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);

		int rc = wait_for_data(fd);
		if (rc == -1)
			break;
		if (rc == 0)
			continue;

		ssize_t n = recvfrom(fd, buffer, sizeof buffer - 1, 0, (struct sockaddr *)&peer, &peer_len);
		if (n == -1) {
			perror("recvfrom");
			exit(EXIT_FAILURE);
		}

		buffer[n] = 0x00;

		char *p = buffer;
		while(p) {
			char *lf = strchr(p, '\n');
			if (lf)
				*lf = 0x00;

			if (!handle_command(-1, p))
				break;

			if (lf)
				p = lf + 1;
			else
				p = NULL;
		}

		line_lock.lock();
		pf_last = time(NULL) + 1;
		line_lock.unlock();
	}
}

void udp_pixelflut_bin_handler(FrameCanvas *const offscreen_canvas, const int listen_port, const char *const interface)
{
	int fd = make_socket(interface, listen_port);

	line_lock.lock();
	const int W = offscreen_canvas -> width(), H = offscreen_canvas -> height();
	line_lock.unlock();

	printf("resolution udp_pixelflut_bin: %dx%d, port %d\n", W, H, listen_port);

	for(;!interrupt_received;) {
		char buffer[65536];
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);

		int rc = wait_for_data(fd);
		if (rc == -1)
			break;
		if (rc == 0)
			continue;

		ssize_t n = recvfrom(fd, buffer, sizeof buffer - 1, 0, (struct sockaddr *)&peer, &peer_len);
		if (n == -1) {
			perror("recvfrom");
			exit(EXIT_FAILURE);
		}

		buffer[n] = 0x00;

		int inc = buffer[1] ? 8 : 7;

		for(int i=2; i<n; i += inc) {
			int x = (buffer[i + 1] << 8) | buffer[i + 0];
			int y = (buffer[i + 3] << 8) | buffer[i + 2];
			int r = buffer[i + 4];
			int g = buffer[i + 5];
			int b = buffer[i + 6];

			pf -> setPixel(x, y, r, g, b);
		}

		line_lock.lock();
		pf_last = time(NULL) + 1;
		line_lock.unlock();
	}
}

textImage * choose_ti_higher_prio(std::vector<textImage *> *const elements)
{
	int best_prio = -1;
	textImage *sel = NULL;

	for(textImage *cur : *elements) {
		int cur_prio = cur -> getPrio();
		if (cur_prio > best_prio) {
			best_prio = cur_prio;
			sel = cur;
		}
	}

	return sel;
}

ssize_t choose_ti_same_prio(std::vector<textImage *> *const elements, const int cmp_prio)
{
	for(size_t nr=0; nr<elements -> size(); nr++) {
		int cur_prio = elements -> at(nr) -> getPrio();
		if (cur_prio == cmp_prio)
			return nr;
	}

	return -1;
}

void udp_textmsgs_handler(const FrameCanvas *const offscreen_canvas, const int listen_port, const char *const interface)
{
	int fd = make_socket(interface, listen_port);

	printf("udp ascii/utf-8 text, listening on %s:%d\n", interface, listen_port);

	for(;!interrupt_received;) {
		char buffer[65536];
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);

		int rc = wait_for_data(fd);
		if (rc == -1)
			break;
		if (rc == 0)
			continue;

		ssize_t n = recvfrom(fd, buffer, sizeof buffer, 0, (struct sockaddr *)&peer, &peer_len);
		if (n == -1) {
			perror("recvfrom");
			exit(EXIT_FAILURE);
		}

		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(peer.sin_addr), addr, INET_ADDRSTRLEN);

		buffer[n] = 0;

		time_t t = time(NULL);
		printf("%s %s", addr, ctime(&t));

		printf("Recv: %s\n", buffer);

		int cur_prio = -1;
		char *prio = strstr(buffer, "$p");
		if (prio)
			cur_prio = atoi(prio + 2);

		line_lock.lock();

		ssize_t nr = choose_ti_same_prio(&ti_cur, cur_prio);

		try {
			if (nr >= 0) {
				font font_(DEFAULT_FONT_FILE, std::string(buffer) + " " + ti_cur.at(nr) -> getOrg(), offscreen_canvas->height(), true);
				textImage *ti = font_.getImage();

				delete ti_cur.at(nr);
				ti_cur.at(nr) = ti;
			}
			else {
				font font_(DEFAULT_FONT_FILE, buffer, offscreen_canvas->height(), true);
				textImage *ti = font_.getImage();

				if (ti -> idleStatus()) {
					delete ti_idle;
					ti_idle = ti;
				}
				else {
					ti_cur.push_back(ti);
				}
			}
		}
		catch(const std::string & err) {
			printf("Failed rendering text: %s\n", err.c_str());
		}

		x = offscreen_canvas->width();

		line_lock.unlock();

//		pf -> clear();
	}
}

void pixelflut_announcer(const int port, const char *const interface, const int width, const int height)
{
	struct sockaddr_in send_addr;
	int trueflag = 1;

	int fd;
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		perror("socket");

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &trueflag, sizeof trueflag) == -1)
		perror("setsockopt");

	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) == -1)
		perror("setsockopt");

	memset(&send_addr, 0, sizeof send_addr);
	send_addr.sin_family = AF_INET;
	send_addr.sin_port = (in_port_t) htons(5006);
	send_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	constexpr int PROTOCOL_VERSION = 1;

	char sbuf[4096];
	int len = snprintf(sbuf, sizeof(sbuf), "pixelvloed:%d.00 %s:%d %d*%d", PROTOCOL_VERSION, interface, port, width, height);

	for(;!interrupt_received;) {
		if (sendto(fd, sbuf, len, 0, (struct sockaddr*) &send_addr, sizeof send_addr) == -1)
			perror("send");

		sleep(1);
	}

	close(fd);
}

int main(int argc, char *argv[])
{
	RGBMatrix::Options matrix_options;
	rgb_matrix::RuntimeOptions runtime_opt;
	if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt))
		return usage(argv[0]);

	/* x_origin is set just right of the screen */
	x_orig = (matrix_options.chain_length * matrix_options.cols) + 5;
	y_orig = 0;
	int brightness = 100, listen_port = 5001, listen_port3 = 5003, listen_port4 = 5004, listen_port5 = 13666;
	const char *interface = "127.0.0.1";
	std::string lcdproc_cfg = "$C00ff00$";

	int opt;
	while ((opt = getopt(argc, argv, "i:t:b:x:y:")) != -1) {
		switch (opt) {
			case 'i': interface = optarg; break;
			case 't': listen_port = atoi(optarg); break;
			case 'b': brightness = atoi(optarg); break;
			case 'x': x_orig = atoi(optarg); break;
			case 'y': y_orig = atoi(optarg); break;
			default:
				  return usage(argv[0]);
		}
	}

	if (brightness < 1 || brightness > 100) {
		fprintf(stderr, "Brightness is outside usable range.\n");
		return 1;
	}

	RGBMatrix *canvas = rgb_matrix::CreateMatrixFromOptions(matrix_options, runtime_opt);
	if (canvas == NULL)
		return 1;

	canvas->SetBrightness(brightness);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, InterruptHandler);
	signal(SIGINT, InterruptHandler);
	signal(SIGUSR1, InterruptHandler);

	printf("CTRL-C for exit\n");

	FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

	work = new frame(offscreen_canvas->width(), offscreen_canvas->height());
	lcdproc = new frame(offscreen_canvas->width(), offscreen_canvas->height());
	pf = new frame(offscreen_canvas->width(), offscreen_canvas->height());

	font::init_fonts();

	std::thread t(udp_textmsgs_handler, offscreen_canvas, listen_port, interface);

	std::thread t3(udp_pixelflut_ascii_handler, offscreen_canvas, listen_port3, interface);

	std::thread t4(udp_pixelflut_bin_handler, offscreen_canvas, listen_port4, interface);

	std::thread t6(tcp_pixelflut_ascii_handler, offscreen_canvas, listen_port4, interface);

	std::thread t5(pixelflut_announcer, listen_port4, interface, offscreen_canvas->width(), offscreen_canvas->height());

	int lp_font_height = 16; //offscreen_canvas->height() / 3; // 3 rows of text
	std::thread t7(lcdproc_handler, listen_port5, interface, lp_font_height, lcdproc_cfg);

	time_t ss = 0;

	for(;!interrupt_received;) {
		uint64_t render_start = get_ts();

		time_t now = time(NULL);

		if (line_lock.try_lock()) {
			work -> setContents(*pf);

			work -> overlay(*lcdproc);

			textImage *use_line = choose_ti_higher_prio(&ti_cur);
			if (!use_line)
				use_line = ti_idle;
			bool is_idle = use_line ? use_line -> idleStatus() : false;

			if (use_line) {
				if (use_line->flashStatus()) {
					printf("FLASH\n");
					for(int i=0; i<5; i++) {
						offscreen_canvas->Fill(255, 255, 255);
						offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
						usleep(101000);
						offscreen_canvas->Fill(0, 0, 0);
						offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
						usleep(101000);
					}

					now = time(NULL);
				}

				int length = use_line->getW();

				blit(work, use_line, x, 0, 0, 0, use_line->getW(), offscreen_canvas->height(), use_line->getTransparent());

				if (is_idle) { // scroll 1 pixel every second in idle mode
					if (now - ss) {
						if (--x + length <= 0)
							x = x_orig;

						ss = now;
					}
				}
				else if (use_line -> getScrollRequired() && --x + length < 0) {
					x = x_orig;
					use_line->setEndOfLine();
				}

				use_line->decreaseDurationLeft(SLEEP_N);
			}

			line_lock.unlock();
		}

		work -> put(offscreen_canvas);
		offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

		uint64_t render_took = get_ts() - render_start;
		int64_t sleep_us = SLEEP_N - render_took;
		if (sleep_us > 0)
			usleep(sleep_us);

		if (line_lock.try_lock()) {
			size_t idx = 0;
			while(idx < ti_cur.size()) {
				if (ti_cur.at(idx) -> getDurationLeft() <= 0 && ti_cur.at(idx)->getEndOfLine()) {
					time_t t = time(NULL);
					printf("%s", ctime(&t));
					printf("finish %s\n", ti_cur.at(idx) -> getOrg().c_str());

					delete ti_cur.at(idx);
					ti_cur.erase(ti_cur.begin() + idx);

					x = 8; // 8 seconds before idle goes off-screen
				}
				else {
					idx++;
				}
			}

			line_lock.unlock();

			if (time(NULL) > pf_last)
				pf -> fade();
		}
	}

	t7.join();
	t6.join();
	t5.join();
	t4.join();
	t3.join();
	t.join();

	for(size_t idx=0; idx < ti_cur.size(); idx++)
		delete ti_cur.at(idx);

	delete pf;
	delete work;

	canvas->Clear();

	delete canvas;

	return 0;
}
