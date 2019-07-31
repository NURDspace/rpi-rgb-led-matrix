#include "led-matrix.h"
#include "graphics.h"
using namespace rgb_matrix;
#include "frame.h"

#include <string.h>

frame::frame(const int w, const int h) : w(w), h(h), nb(w * h * 3) {
	pixels = (uint8_t *)malloc(nb);
}

frame::~frame() {
	free(pixels);
}

bool frame::getPixel(int x, int y, int *const r, int *const g, int *const b) const {
	if (x < 0 || y < 0)
		return false;

	if (x >= w || y >= h)
		return true;

	int o = y * w * 3 + x * 3;

	*r = pixels[o + 0];
	*g = pixels[o + 1];
	*b = pixels[o + 2];

	return true;
}

bool frame::setPixel(int x, int y, int r, int g, int b) {
	if (x < 0 || y < 0)
		return false;

	if (x >= w || y >= h)
		return true;

	int o = y * w * 3 + x * 3;

	pixels[o + 0] = r;
	pixels[o + 1] = g;
	pixels[o + 2] = b;

	return true;
}

void frame::setContents(const frame & in) {
	// FIXME handle different dimensions

	memcpy(pixels, in.getLow(), nb);
}

void frame::overlay(const frame & in) {
	for(int y=0; y<h; y++) {
		for(int x=0; x<w; x++) {
			int r = 0, g = 0, b = 0;

			in.getPixel(x, y, &r, &g, &b);

			if (r || g || b)
				setPixel(x, y, r, g, b);
		}
	}
}

void frame::clear() {
	memset(pixels, 0x00, nb);
}

void frame::fade() {
	for(int i=0; i<nb; i++)
		pixels[i] = (pixels[i] * 123) / 124;
}

void frame::put(FrameCanvas *const offscreen_canvas) {
	for(int y=0; y<h; y++) {
		for(int x=0; x<w; x++) {
			int o = y * w * 3 + x * 3;

			offscreen_canvas -> SetPixel(x, y, pixels[o + 0], pixels[o + 1], pixels[o + 2]);
		}
	}
}
