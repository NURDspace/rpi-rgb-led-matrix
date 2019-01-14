#include "led-matrix.h"
#include "graphics.h"
#include "font.h"

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
			if (offset_source < 0)
				continue;

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

int make_socket (uint16_t port)
{
	/* Create the socket. */
	int sock = socket (PF_INET, SOCK_DGRAM, 0);
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
time_t start = 0;
bool endOfLine = false;

textImage * choose_ti(std::vector<textImage *> *const elements)
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

void udp_handler(const FrameCanvas *const offscreen_canvas)
{
	int fd = make_socket(5001);

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

		buffer[n] = 0;

		bool is_idle = false, add = false;

		time_t t = time(NULL);
		printf("%s", ctime(&t));

		printf("Recv: %s\n", buffer);

		line_lock.lock();

		font font_(DEFAULT_FONT_FILE, buffer, offscreen_canvas->height(), true);
		textImage *ti = font_.getImage();

		if (ti -> idleStatus()) {
			delete ti_idle;
			ti_idle = ti;
		}
		else {
			ti_cur.push_back(ti);
		}

		x = offscreen_canvas->width();

		start = time(NULL);

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
	int brightness = 100;

	start = time(NULL);

	int opt;
	while ((opt = getopt(argc, argv, "b:x:y:")) != -1) {
		switch (opt) {
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

	std::thread t(udp_handler, offscreen_canvas);

	time_t ss = 0;

	for(;!interrupt_received;) {
		offscreen_canvas->Clear(); // clear canvas

		time_t now = time(NULL);

		if (line_lock.try_lock()) {
			textImage *use_line = choose_ti(&ti_cur);
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

				blit(offscreen_canvas, use_line, x, 0, 0, 0, use_line->getW(), offscreen_canvas->height());
				offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

				if (is_idle) { // scroll 1 pixel every second in idle mode
					if (now - ss) {
						if (--x + length <= 0)
							x = x_orig;

						ss = now;
					}
				}
				else if (--x + length < 0) {
					x = x_orig;
					use_line->setEndOfLine();
				}
			}

			line_lock.unlock();

			offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
		}

		usleep(10000);

		if (line_lock.try_lock()) {
			size_t idx = 0;
			while(idx < ti_cur.size()) {
				if (now - start > ti_cur.at(idx) -> getDuration() && ti_cur.at(idx)->getEndOfLine()) {
					printf("finish\n");

					delete ti_cur.at(idx);
					ti_cur.erase(ti_cur.begin() + idx);

					offscreen_canvas->Clear();
					offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

					x = 8; // 8 seconds before idle goes off-screen
				}
				else {
					idx++;
				}
			}

			line_lock.unlock();
		}
	}

	// Finished. Shut down the RGB matrix.
	canvas->Clear();
	delete canvas;

	return 0;
}
