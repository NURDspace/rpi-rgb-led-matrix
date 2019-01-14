#include <map>
#include <stdint.h>
#include <string>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

#define DEFAULT_FONT_FILE "/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf"

class textImage
{
private:
	const uint8_t *const buffer;
	const int w, h;
	bool wantFlash;
	const bool isIdle;
	const int duration;
	const int prio;
	bool endOfLine;

public:
	textImage(const uint8_t *const buffer, const int w, const int h, const bool wantFlash, const bool isIdle, const int duration, const int prio) : buffer(buffer), w(w), h(h), wantFlash(wantFlash), isIdle(isIdle), duration(duration), prio(prio) {
		endOfLine = false;
	}

	~textImage() {
		delete [] buffer;
	}

	const int getW() const { return w; }
	const int getH() const { return h; }
	const uint8_t * getBuffer() const { return buffer; }
	const bool flashStatus() { bool rc = wantFlash; wantFlash = false; return rc; }
	const bool idleStatus() const { return isIdle; }
	const int getDuration() const { return duration; }
	bool getEndOfLine() const { return endOfLine; }
	void setEndOfLine() { endOfLine = true; }
	int getPrio() { return prio; }
};

class font {
private:
	static FT_Library library;
	static std::map<std::string, FT_Face> font_cache;

	uint8_t *result;
	int bytes, w, h, max_ascender;
	bool want_flash, is_idle;
	int duration, prio;

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
