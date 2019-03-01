#include "led-matrix.h"
#include "graphics.h"
#include "font.h"
#include "utils.h"

#include <string>
#include <thread>
#include <mutex>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

using namespace rgb_matrix;

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

static bool FullSaturation(const Color &c) {
	return (c.r == 0 || c.r == 255)
		&& (c.g == 0 || c.g == 255)
		&& (c.b == 0 || c.b == 255);
}

void blit(FrameCanvas *const canvas, const textImage *const in, const int target_x, const int target_y, int source_x, int source_y, const int source_w, const int source_h)
{
	int endx = source_x + source_w;
	if (endx > in->getW())
		endx = in->getW();

	int endy = source_y + source_h;
	if (endy > in->getH())
		endy = in->getH();

	if (source_x < 0)
		source_y = 0;

	if (source_y < 0)
		source_y = 0;

	const uint8_t *const buffer = in -> getBuffer();

	for(int y=source_y; y<endy; y++) {
		for(int x=source_x; x<endx; x++) {
			const int offset_source = y * in->getW() * 3 + x * 3;

			int tx = target_x + (x - source_x);
			int ty = target_y + (y - source_y);

			if (tx < 0)
				continue;
			if (ty < 0)
				continue;

			if (tx >= canvas->width())
				break;

			if (ty >= canvas->height())
				break;

			canvas -> SetPixel(tx, ty, buffer[offset_source + 0], buffer[offset_source + 1], buffer[offset_source + 2]);
		}
	}
}

int make_socket(const uint16_t port)
{
	/* Create the socket. */
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
	{
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	int reuse_addr = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse_addr), sizeof reuse_addr) == -1) {
		perror("SO_REUSEADDR");
		exit(EXIT_FAILURE);
	}

	/* Give the socket a name. */
	struct sockaddr_in name;
	name.sin_family = AF_INET;
	name.sin_port = htons (port);
	name.sin_addr.s_addr = htonl (INADDR_ANY);
	if (bind (sock, (struct sockaddr *) &name, sizeof (name)) < 0)
	{
		perror ("bind");
		exit (EXIT_FAILURE);
	}

	return sock;
}

textImage *ti_idle = NULL;
std::vector<textImage *> ti_cur;
std::mutex line_lock;
int x_orig = 0, y_orig = 0;
int x = x_orig;
int y = y_orig;
bool endOfLine = false;
time_t pixelflut_end = 0;
uint8_t *pixelflutbuf = NULL;

int fromhex(int c)
{
	c = toupper(c);

	if (c >= 'A')
		return c - 'A' + 10;

	return c - '0';
}

void putPixelBuf(FrameCanvas *const offscreen_canvas) {
	const int W = offscreen_canvas -> width(), H = offscreen_canvas -> height();

	for(int y=0; y<H; y++) {
		for(int x=0; x<W; x++) {
			int o = y * W * 3 + x * 3;

			offscreen_canvas -> SetPixel(x, y, pixelflutbuf[o + 0], pixelflutbuf[o + 1], pixelflutbuf[o + 2]);
		}
	}
}

void udp_pixelflut_handler(FrameCanvas *const offscreen_canvas, const int listen_port)
{
	int fd = make_socket(listen_port);

	line_lock.lock();
	const int W = offscreen_canvas -> width(), H = offscreen_canvas -> height();
	line_lock.unlock();

	printf("resolution pixelflut: %dx%d, port %d\n", W, H, listen_port);

	for(;;) {
		char buffer[4096];
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(peer);

		ssize_t n = recvfrom(fd, buffer, sizeof buffer - 1, 0, (struct sockaddr *)&peer, &peer_len);
		if (n == -1) {
			perror("recvfrom");
			exit(EXIT_FAILURE);
		}

		buffer[n] = 0x00;

int cnt = 0;

		char *p = buffer;
		while(p) {
			char *lf = strchr(p, '\n');
			if (lf)
				*lf = 0x00;

			// PX 20 30 ff8800
			if (p[0] != 'P' || p[1] != 'X')
				break;

			int tx = atoi(&p[3]);
			char *space = strchr(&p[4], ' ');
			if (!space)
				break;

			int ty = atoi(space + 1);

			space = strchr(space + 1, ' ');
			if (!space)
				break;
			//printf("%d %d %s\n", cnt, p[0], p);

cnt++;

			char *rgb = space + 1;
			int r = (fromhex(rgb[0]) << 4) + fromhex(rgb[1]);
			int g = (fromhex(rgb[2]) << 4) + fromhex(rgb[3]);
			int b = (fromhex(rgb[4]) << 4) + fromhex(rgb[5]);

			pixelflutbuf[ty * W * 3 + tx * 3 + 0] = r;
			pixelflutbuf[ty * W * 3 + tx * 3 + 1] = g;
			pixelflutbuf[ty * W * 3 + tx * 3 + 2] = b;

			if (lf)
				p = lf + 1;
			else
				p = NULL;
		}
		//printf("%d\n", cnt);

		line_lock.lock();

		delete ti_idle;
		ti_idle = NULL;

		putPixelBuf(offscreen_canvas);

		pixelflut_end = time(NULL) + 1;

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
	for(ssize_t nr=0; nr<elements -> size(); nr++) {
		int cur_prio = elements -> at(nr) -> getPrio();
		if (cur_prio == cmp_prio)
			return nr;
	}

	return -1;
}

void udp_textmsgs_handler(const FrameCanvas *const offscreen_canvas, const int listen_port)
{
	int fd = make_socket(listen_port);

	for(;;) {
		char buffer[4096];
		struct sockaddr_in peer;
		socklen_t peer_len = sizeof(buffer) - 1;

		printf("\nWaiting for message\n");

		ssize_t n = recvfrom(fd, buffer, sizeof buffer, 0, (struct sockaddr *)&peer, &peer_len);
		if (n == -1) {
			perror("recvfrom");
			exit(EXIT_FAILURE);
		}

		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(peer.sin_addr), addr, INET_ADDRSTRLEN);

		buffer[n] = 0;

		bool is_idle = false, add = false;

		time_t t = time(NULL);
		printf("%s %s", addr, ctime(&t));

		printf("Recv: %s\n", buffer);

		int cur_prio = -1;
		char *prio = strstr(buffer, "$p");
		if (prio)
			cur_prio = atoi(prio + 2);

		line_lock.lock();

		ssize_t nr = choose_ti_same_prio(&ti_cur, cur_prio);

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

		x = offscreen_canvas->width();

		line_lock.unlock();
	}
}

int main(int argc, char *argv[]) {
	RGBMatrix::Options matrix_options;
	rgb_matrix::RuntimeOptions runtime_opt;
	if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv, &matrix_options, &runtime_opt))
		return usage(argv[0]);

	const char *bdf_font_file = NULL;
	/* x_origin is set just right of the screen */
	x_orig = (matrix_options.chain_length * matrix_options.cols) + 5;
	y_orig = 0;
	int brightness = 100, listen_port = 5001, listen_port3 = 5003;

	int opt;
	while ((opt = getopt(argc, argv, "t:b:x:y:")) != -1) {
		switch (opt) {
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

	signal(SIGTERM, InterruptHandler);
	signal(SIGINT, InterruptHandler);

	printf("CTRL-C for exit.\n");

	// Create a new canvas to be used with led_matrix_swap_on_vsync
	FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

	font::init_fonts();

	pixelflutbuf = (uint8_t *)calloc(1, canvas->width() * canvas->height() * 3);

	std::thread t(udp_textmsgs_handler, offscreen_canvas, listen_port);

	std::thread t3(udp_pixelflut_handler, offscreen_canvas, listen_port3);

	time_t ss = 0;

	for(;!interrupt_received;) {
		uint64_t render_start = get_ts();

		time_t now = time(NULL);

		if (line_lock.try_lock()) {
			textImage *use_line = choose_ti_higher_prio(&ti_cur);
			if (!use_line)
				use_line = ti_idle;
			bool is_idle = use_line ? use_line -> idleStatus() : false;

			if (use_line) {
				putPixelBuf(offscreen_canvas);

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

				blit(offscreen_canvas, use_line, x, 0, 0, 0, use_line->getW(), offscreen_canvas->height());
				offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

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

			offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

			line_lock.unlock();
		}

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

					putPixelBuf(offscreen_canvas);
					offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

					x = 8; // 8 seconds before idle goes off-screen
				}
				else {
					idx++;
				}
			}

			int n = canvas -> width() * canvas -> height() * 3;
			for(int i=0; i<n; i++)
				pixelflutbuf[i] = (int(pixelflutbuf[i]) * 123) / 124;
			putPixelBuf(offscreen_canvas);
			offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
			offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

			line_lock.unlock();
		}
	}

	// Finished. Shut down the RGB matrix.
	canvas->Clear();
	delete canvas;

	return 0;
}
