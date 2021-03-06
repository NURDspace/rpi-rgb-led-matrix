#pragma once

#include <map>
#include <stdint.h>
#include <string>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

//#define DEFAULT_FONT_FILE "/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf"
//#define DEFAULT_FONT_FILE "seguiemj.ttf"
//#define DEFAULT_FONT_FILE "EmojiOneColor-SVGinOT-1.3/EmojiOneColor-SVGinOT.ttf"
#define DEFAULT_FONT_FILE "/usr/share/fonts/truetype/unifont/unifont.ttf"
#define DEFAULT_PROP_FONT_FILE "/usr/share/fonts/truetype/msttcorefonts/Courier_New.ttf"

class textImage
{
private:
	const uint8_t *const buffer;
	const int w, h;
	bool wantFlash;
	const bool isIdle;
	int64_t durationLeft;
	const int prio;
	bool endOfLine;
	std::string org;
	bool scroll, transparent;

public:
	textImage(const uint8_t *const buffer, const int w, const int h, const bool wantFlash, const bool isIdle, const int64_t durationLeft, const int prio, const std::string & org, const bool scroll, const bool t) : buffer(buffer), w(w), h(h), wantFlash(wantFlash), isIdle(isIdle), durationLeft(durationLeft), prio(prio), org(org), scroll(scroll), transparent(t) {
		endOfLine = false;

		printf("New: %dx%d, flash: %d, idle: %d, duration: %f, prio: %d, scroll: %d, text: %s\n",
			w, h, wantFlash, isIdle, durationLeft / 1000000.0, prio, scroll, org.c_str());
	}

	~textImage() {
		delete [] buffer;
	}

	int getW() const { return w; }
	int getH() const { return h; }
	const uint8_t * getBuffer() const { return buffer; }
	bool flashStatus() { bool rc = wantFlash; wantFlash = false; return rc; }
	bool idleStatus() const { return isIdle; }
	int64_t getDurationLeft() const { return durationLeft; }
	void decreaseDurationLeft(const uint64_t hm) { durationLeft -= hm; }
	bool getEndOfLine() const { return endOfLine; }
	void setEndOfLine() { endOfLine = true; }
	int getPrio() const { return prio; }
	const std::string & getOrg() const { return org; }
	bool getScrollRequired() const { return scroll; }
	bool getTransparent() const { return transparent; }
};

class font {
private:
	static FT_Library library;
	static std::map<std::string, FT_Face> font_cache;

	uint8_t *result;
	int bytes, w, h, max_ascender;
	bool want_flash, is_idle;
	int64_t duration;
	int prio;
	std::string org;
	bool scroll, transparent;

	void draw_bitmap(const FT_Bitmap *const bitmap, const int target_height, const FT_Int x, const FT_Int y, uint8_t r, uint8_t g, uint8_t b, const bool invert, const bool underline, const bool rainbow, const uint8_t bcr, const uint8_t bcb, const uint8_t bcg);

public:
	font(const std::string & filename, const std::string & text, const int target_height, const bool antialias);
	virtual ~font();

	textImage * getImage();
	int getMaxAscender() const;

	static void init_fonts();
	static void uninit_fonts();
};

std::string find_font_by_name(const std::string & font_name, const std::string & default_font_file);
