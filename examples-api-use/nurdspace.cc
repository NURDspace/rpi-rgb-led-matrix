#include "led-matrix.h"
#include "graphics.h"

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
	fprintf(stderr, "usage: %s [options] <text>\n", progname);
	fprintf(stderr, "Takes text and scrolls it with speed -s\n");
	fprintf(stderr, "Options:\n");
	rgb_matrix::PrintMatrixFlags(stderr);
	fprintf(stderr,
			"\t-s <speed>        : Approximate letters per second.\n"
			"\t-l <loop-count>   : Number of loops through the text. "
			"-1 for endless (default)\n"
			"\t-f <font-file>    : Use given font.\n"
			"\t-b <brightness>   : Sets brightness percent. Default: 100.\n"
			"\t-x <x-origin>     : X-Origin of displaying text (Default: 0)\n"
			"\t-y <y-origin>     : Y-Origin of displaying text (Default: 0)\n"
			"\t-S <spacing>      : Spacing pixels between letters (Default: 0)\n"
			"\t-d <duration>     : duration in seconds to show a string (default: 10)\n"
			"\t-i <text>         : initial text to show when idle (default: nothing)\n"
			"\n"
			"\t-C <r,g,b>        : Color. Default 255,255,0\n"
			"\t-B <r,g,b>        : Background-Color. Default 0,0,0\n"
			"\t-O <r,g,b>        : Outline-Color, e.g. to increase contrast.\n"
	       );
	return 1;
}

static bool parseColor(Color *c, const char *str) {
	int r, g, b;
	bool ok =  sscanf(str, "%d,%d,%d", &r, &g, &b) == 3;
	c -> r = r;
	c -> g = g;
	c -> b = b;
	return ok;
}

static bool FullSaturation(const Color &c) {
	return (c.r == 0 || c.r == 255)
		&& (c.g == 0 || c.g == 255)
		&& (c.b == 0 || c.b == 255);
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


std::string line, idle_line;
std::mutex line_lock;
int x_orig = 0, y_orig = 0;
int x = x_orig;
int y = y_orig;
time_t start = 0;
bool with_outline = false;
int duration = 10, bduration = 10;
Color color(255, 255, 0);
Color bg_color(0, 0, 0);
Color outline_color(0, 0, 0);
bool flash = false, endOfLine = false;

Color bcolor(255, 255, 0);
Color bbg_color(0, 0, 0);
Color boutline_color(0, 0, 0);

void udp_handler()
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

		color = bcolor;
		bg_color = bbg_color;
		outline_color = boutline_color;
		duration = bduration;

		ssize_t put = 0;
		for(ssize_t i=0; i<n;)
		{
			if (buffer[i] == '$') {
				char *end = strchr(&buffer[i + 1], '$');
				if (!end)
					break;

				*end = 0x00;

				switch(buffer[i + 1]) {
					case 'C':
						printf("fg color\n");
						if (!parseColor(&color, &buffer[i + 2]))
							fprintf(stderr, "Invalid color spec: %s\n", &buffer[i + 2]);
						break;
					case 'B':
						printf("bg color\n");
						if (!parseColor(&bg_color, &buffer[i + 2]))
							fprintf(stderr, "Invalid background color spec: %s\n", &buffer[i + 2]);
						break;
					case 'O':
						printf("outline color\n");
						if (!parseColor(&outline_color, &buffer[i + 2]))
							fprintf(stderr, "Invalid outline color spec: %s\n", &buffer[i + 2]);
						with_outline = true;
						break;
					case 'o':
						printf("with outline color\n");
						with_outline = false;
						break;
					case 'd':
						printf("set duration\n");
						duration = atoi(&buffer[i + 2]);
						printf("new duration: %d\n", duration);
						break;
					case 'i':
						printf("is idle\n");
						is_idle = true;
						break;
					case 'a':
						printf("add\n");
						add = true;
						break;
					case 'f':
						printf("do flash\n");
						flash = true;
						break;
					default:
						printf("%c is not understood\n", buffer[i + 1]);
						break;
				}

				i += end - &buffer[i] + 1;
			}
			else {
				buffer[put++] = buffer[i++];
			}
		}

		printf("Remaining length %d\n", int(put));
		buffer[put] = 0x00;

		if (is_idle) {
			if (add) {
				idle_line += " ";
				idle_line += buffer;
			}
			else {
				idle_line = buffer;
			}
			x = 0;
		}
		else {
			if (add) {
				line += " ";
				line += buffer;
			}
			else {
				line = buffer;
			}
			x = x_orig;
		}

		y = y_orig;
		endOfLine = false;
		start = time(NULL);
		line_lock.unlock();
	}
}

int main(int argc, char *argv[]) {
	RGBMatrix::Options matrix_options;
	rgb_matrix::RuntimeOptions runtime_opt;
	if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
				&matrix_options, &runtime_opt)) {
		return usage(argv[0]);
	}

	const char *bdf_font_file = NULL;
	/* x_origin is set just right of the screen */
	x_orig = (matrix_options.chain_length * matrix_options.cols) + 5;
	y_orig = 0;
	int brightness = 100;
	int letter_spacing = 0;
	float speed = 7.0f;
	int loops = -1;

	start = time(NULL);

	int opt;
	while ((opt = getopt(argc, argv, "i:d:x:y:f:C:B:O:b:S:s:l:")) != -1) {
		switch (opt) {
			case 'i': idle_line = optarg; break;
			case 'd': duration = atoi(optarg); break;
			case 's': speed = atof(optarg); break;
			case 'l': loops = atoi(optarg); break;
			case 'b': brightness = atoi(optarg); break;
			case 'x': x_orig = atoi(optarg); break;
			case 'y': y_orig = atoi(optarg); break;
			case 'f': bdf_font_file = strdup(optarg); break;
			case 'S': letter_spacing = atoi(optarg); break;
			case 'C':
				  if (!parseColor(&color, optarg)) {
					  fprintf(stderr, "Invalid color spec: %s\n", optarg);
					  return usage(argv[0]);
				  }
				  break;
			case 'B':
				  if (!parseColor(&bg_color, optarg)) {
					  fprintf(stderr, "Invalid background color spec: %s\n", optarg);
					  return usage(argv[0]);
				  }
				  break;
			case 'O':
				  if (!parseColor(&outline_color, optarg)) {
					  fprintf(stderr, "Invalid outline color spec: %s\n", optarg);
					  return usage(argv[0]);
				  }
				  with_outline = true;
				  break;
			default:
				  return usage(argv[0]);
		}
	}

	bcolor = color;
	bbg_color = bg_color;
	boutline_color = outline_color;
	bduration = duration;

	for (int i = optind; i < argc; ++i) {
		line.append(argv[i]).append(" ");
	}

	if (line.empty()) {
		fprintf(stderr, "Add the text you want to print on the command-line.\n");
		return usage(argv[0]);
	}

	if (bdf_font_file == NULL) {
		fprintf(stderr, "Need to specify BDF font-file with -f\n");
		return usage(argv[0]);
	}

	/*
	 * Load font. This needs to be a filename with a bdf bitmap font.
	 */
	rgb_matrix::Font font;
	if (!font.LoadFont(bdf_font_file)) {
		fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
		return 1;
	}

	/*
	 * If we want an outline around the font, we create a new font with
	 * the original font as a template that is just an outline font.
	 */
	rgb_matrix::Font *outline_font = NULL;
	if (with_outline) {
		outline_font = font.CreateOutlineFont();
	}

	if (brightness < 1 || brightness > 100) {
		fprintf(stderr, "Brightness is outside usable range.\n");
		return 1;
	}

	RGBMatrix *canvas = rgb_matrix::CreateMatrixFromOptions(matrix_options,
			runtime_opt);
	if (canvas == NULL)
		return 1;

	canvas->SetBrightness(brightness);

	signal(SIGTERM, InterruptHandler);
	signal(SIGINT, InterruptHandler);

	printf("CTRL-C for exit.\n");

	// Create a new canvas to be used with led_matrix_swap_on_vsync
	FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();

	int delay_speed_usec = 1000000 / speed / font.CharacterWidth('W');
	if (delay_speed_usec < 0)
		delay_speed_usec = 2000;

	std::thread t(udp_handler);

	time_t ss = 0;

	while (!interrupt_received && loops != 0) {
		offscreen_canvas->Clear(); // clear canvas

		time_t now = time(NULL);

		if (line_lock.try_lock()) {

			if (flash) {
				printf("FLASH\n");
				for(int i=0; i<5; i++) {
					offscreen_canvas->Fill(color.r, color.g, color.b);
					offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
					usleep(101000);
					offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);
					offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
					usleep(101000);
				}

				flash = false;
				now = time(NULL);
			}

			std::string use_line = line.empty() ? (idle_line.empty() ? "" : idle_line) : line;
			bool is_idle = line.empty() && !idle_line.empty();

			if (!use_line.empty()) {
				int length = 0;

				if (outline_font) {
					rgb_matrix::DrawText(offscreen_canvas, *outline_font,
							x - 1, y + font.baseline(),
							outline_color, &bg_color,
							use_line.c_str(), letter_spacing - 2);
				}

				length = rgb_matrix::DrawText(offscreen_canvas, font,
						x, y + font.baseline(),
						color, outline_font ? NULL : &bg_color,
						use_line.c_str(), letter_spacing);

				if (is_idle) { // scroll 1 pixel every second in idle mode
					if (now - ss) {
						if (--x + length <= 0)
							x = x_orig;

						ss = now;
					}
				}
				else if (--x + length < 0) {
					x = x_orig;
					endOfLine = true;
					if (loops > 0)
						--loops;
				}
			}

			line_lock.unlock();

			offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
		}

		usleep(delay_speed_usec);

		if (line_lock.try_lock()) {
			if (!line.empty()) {
				if (duration && now - start > duration && endOfLine) {
					printf("finish\n");

					line.clear();

					offscreen_canvas->Clear();
					offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);

					color = bcolor;
					bg_color = bbg_color;
					outline_color = boutline_color;
					duration = bduration;

					x = 8; // 8 seconds before idle goes off-screen
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
