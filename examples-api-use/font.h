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
	bool want_flash, is_idle;

public:
	textImage(const uint8_t *const buffer, const int w, const int h, const bool want_flash, const bool is_idle) : buffer(buffer), w(w), h(h), want_flash(want_flash), is_idle(is_idle) {
	}

	~textImage() {
		delete [] buffer;
	}

	const int getw() const { return w; }
	const int geth() const { return h; }
	const uint8_t * getbuffer() const { return buffer; }
	const bool flash_status() { bool rc = want_flash; want_flash = false; return rc; }
	const bool idle_status() { return is_idle; }
};

class font {
private:
	static FT_Library library;
	static std::map<std::string, FT_Face> font_cache;

	uint8_t *result;
	int bytes, w, h, max_ascender;
	bool want_flash, is_idle;

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
