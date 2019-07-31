#include "led-matrix.h"
#include "graphics.h"
using namespace rgb_matrix;

#include "font.h"
#include "utils.h"
#include "frame.h"

#include <string>
#include <thread>
#include <mutex>
#include <poll.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

extern volatile bool interrupt_received;

// https://stackoverflow.com/questions/18675364/c-tokenize-a-string-with-spaces-and-quotes
bool split_in_args(std::vector<std::string>& qargs, std::string command)
{
	qargs.clear();

	int len = command.length();
	bool qot = false, sqot = false;
	int arglen = 0;

	for(int i = 0; i < len; i++) {
		int start = i;
		if (command[i] == '\"') {
			qot = true;
		}
		else if (command[i] == '\'') sqot = true;

		if (qot) {
			i++;
			start++;
			while(i<len && command[i] != '\"')
				i++;
			if(i<len)
				qot = false;
			arglen = i - start;
			i++;
		}
		else if (sqot) {
			i++;
			while(i<len && command[i] != '\'')
				i++;
			if(i<len)
				sqot = false;
			arglen = i - start;
			i++;
		}
		else {
			while(i<len && command[i]!=' ')
				i++;

			arglen = i - start;
		}

		qargs.push_back(command.substr(start, arglen));
	}

	if (qot || sqot)
		return false;

	return true;
}

frame *lcdproc = NULL;

typedef enum { LWT_STRING, LWT_HBAR, LWT_VBAR, LWT_ICON, LWT_TITLE, LWT_SCROLLER, LWT_FRAME } lcdproc_widget_type_t;

typedef struct
{
	lcdproc_widget_type_t type;
	int id;
	std::string text; // only string and title are supported currently
	int x, y;
	textImage *ti;
}
lcdproc_widget_t;

typedef struct
{
	int prio, id;
	std::string name;
	int duration;

	std::vector<lcdproc_widget_t> widgets;
}
lcdproc_screen_t;

std::mutex lpl; // lcdproc lock
std::vector<lcdproc_screen_t> screens;

std::vector<lcdproc_screen_t>::iterator find_screen(int id)
{
	lpl.lock();

	for(auto it = screens.begin(); it != screens.end(); it++) {
		if (it->id == id)
			return it;
	}

	return screens.end();
}

lcdproc_widget_t * find_widget(int screen_id, int widget_id)
{
	lpl.lock();

	for(auto it = screens.begin(); it != screens.end(); it++) {
		if (it->id == screen_id) {
			for(auto & cur : it->widgets) {
				if (cur.id == widget_id)
					return &cur;
			}

			return nullptr;
		}
	}

	return nullptr;
}

// http://lcdproc.omnipotent.net/download/netstuff.txt
bool lcdproc_command(const int fd, const char *const cmd, int font_height)
{
	std::vector<std::string> parts;
       
	if (!split_in_args(parts, cmd))
		return false;

	if (parts.at(0) == "hello") {
		WRITE(fd, "connect\n", 8);
		return true;
	}

	if (parts.at(0) == "client_set")
		return true;

	if (parts.at(0) == "client_add_key" || parts.at(0) == "client_del_key")
		return true;

	if (parts.at(0) == "screen_add" && parts.size() == 2) {
		lpl.lock();
		screens.push_back({ 255, atoi(parts.at(1).c_str()), "", 32, { } });
		lpl.unlock();
		return true;
	}

	if (parts.at(0) == "screen_del" && parts.size() == 2) {
		int id = atoi(parts.at(1).c_str());
		bool found = false;

		auto it = find_screen(id);
		if (it != screens.end()) {
			screens.erase(it);
			found = true;
		}

		lpl.unlock();

		return found;
	}

	if (parts.at(0) == "screen_set" && parts.size() >= 2) {
		int id = atoi(parts.at(1).c_str());
		bool found = false;

		auto it = find_screen(id);
		if (it != screens.end()) {
			found = true;

			for(size_t i=2; i<parts.size(); i++) {
				if (parts.at(i) == "priority")
					it->prio = atoi(parts.at(++i).c_str());
				else if (parts.at(i) == "name")
					it->name = parts.at(++i);
				else if (parts.at(i) == "duration")
					it->duration = atoi(parts.at(++i).c_str());
				else {
					found = false;
					break;
				}
			}
		}

		lpl.unlock();

		return found;
	}

	if (parts.at(0) == "widget_add" && parts.size() >= 4) {
		int screen_id = atoi(parts.at(1).c_str());
		int widget_id = atoi(parts.at(2).c_str());

		bool found = false;
		auto it = find_screen(screen_id);
		if (it != screens.end()) {
			it->widgets.push_back({LWT_STRING, widget_id, "", 0, 0, nullptr });

			found = true;
		}

		lpl.unlock();

		return found;
	}

	if (parts.at(0) == "widget_del" && parts.size() == 3) {
		int screen_id = atoi(parts.at(1).c_str());
		int widget_id = atoi(parts.at(2).c_str());

		bool found = false;
		auto it = find_screen(screen_id);
		if (it != screens.end()) {
			for(auto it2 = it->widgets.begin(); it2 != it->widgets.end(); it2++) {
				if (it2->id == widget_id) {
					it->widgets.erase(it2);
					found = true;
					break;
				}
			}	
		}

		lpl.unlock();

		return found;
	}

	if (parts.at(0) == "widget_set" && parts.size() >= 4) {
		int screen_id = atoi(parts.at(1).c_str());
		int widget_id = atoi(parts.at(2).c_str());

		bool found = false;
		lcdproc_widget_t *widget = find_widget(screen_id, widget_id);
		if (widget) {
			found = true;

			if (widget->type == LWT_TITLE)
				widget->text = parts.at(3);
			else if (widget->type == LWT_STRING) {
				widget->x = atoi(parts.at(3).c_str()); // FIXME check dimensions
				widget->y = atoi(parts.at(4).c_str());
				widget->text = parts.at(5);
			}
			else {
				found = false;
			}

			if (widget->text.empty() == false) {
				delete widget->ti;

				font font_(DEFAULT_PROP_FONT_FILE, widget->text, font_height, true);
				widget->ti = font_.getImage();
			}
		}

		lpl.unlock();

		return found;
	}

	return false;
}

void lcdproc_handler_do(const int fd, int font_height)
{
	char buffer[4096];
	int o = 0;

	printf("client thread started for lcdproc %d\n", fd);

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

		o += n;

		for(;;) {
			char *lf = strchr(buffer, '\n');
			if (!lf)
				break;

			*lf = 0x00;

			printf("lcdproc command: %s\n", buffer);

			if (!lcdproc_command(fd, buffer, font_height)) {
				// silently ignore errors for now: goto fail;
			}

			// redraw `frame' FIXME

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
	}

fail:
	printf("lcdproc client thread terminating %d\n", fd);

	close(fd);
}

// FIXME dimensions in characters w/h
void lcdproc_handler(const int listen_port, const char *const interface, int font_height)
{
	int fd = make_socket(interface, listen_port, false);

	printf("lcdproc port %d\n", listen_port);

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

		printf("lcdproc connection from %s: %d\n", addr, new_fd);

		try {
			std::thread t(lcdproc_handler_do, new_fd, font_height);
			t.detach();
		}
		catch(std::system_error & e) {
			printf("thread error: %s\n", e.what());
			close(new_fd);
		}
	}
}
