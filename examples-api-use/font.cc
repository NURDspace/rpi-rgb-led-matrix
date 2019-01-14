#include <algorithm>
#include <assert.h>
#include <fontconfig/fontconfig.h>
#include <unicode/ustring.h>
#include "font.h"
#include "utils.h"

//#define DEBUG
//#define DEBUG_IMG

pthread_mutex_t freetype2_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fontconfig_lock = PTHREAD_MUTEX_INITIALIZER;

FT_Library font::library;
std::map<std::string, FT_Face> font::font_cache;

void font::draw_bitmap(const FT_Bitmap *const bitmap, const int target_height, const FT_Int x, const FT_Int y, uint8_t r, uint8_t g, uint8_t b, const bool invert, const bool underline, const bool rainbow, const uint8_t bcr, const uint8_t bcb, const uint8_t bcg)
{
#if 0
	assert(x >= 0);
	assert(x < w);
	assert(y >= 0);
	assert(y < target_height);
	assert(w >= 0);
	assert(target_height >= 0);
#endif

	if (invert) {
		uint8_t cr = r, cg = g, cb = b;

		for(int yo=0; yo<h; yo++) {
			if (rainbow) {
				double dr = 0, dg = 0, db = 0;
				hls_to_rgb(double(yo) / double(h), 0.5, 0.5, &dr, &dg, &db);
				cr = dr * 255.0;
				cg = dg * 255.0;
				cb = db * 255.0;
			}

			for(unsigned int xo=0; xo<bitmap->width; xo++) {
				int o = yo * w * 3 + (x + xo) * 3;

				if (o + 2 >= bytes)
					continue;

				result[o + 0] = cr;
				result[o + 1] = cg;
				result[o + 2] = cb;
			}
		}
	}

	for(unsigned int yo=0; yo<bitmap->rows; yo++)
	{
		int yu = yo + y;

		if (yu < 0)
			continue;

		if (yu >= target_height)
			break;

		if (rainbow)
		{
			double dr = 0, dg = 0, db = 0;
			hls_to_rgb(double(yo) / double(bitmap -> rows), 0.5, 0.5, &dr, &dg, &db);
			r = dr * 255.0;
			g = dg * 255.0;
			b = db * 255.0;
		}

		for(unsigned int xo=0; xo<bitmap->width; xo++)
		{
			int xu = xo + x;

			if (xu < 0)
				continue;

			if (xu >= w)
				break;

			int o = yu * w * 3 + xu * 3;

			int pixel_v = bitmap->buffer[yo * bitmap->width + xo];

			if (invert)
				pixel_v = 255 - pixel_v;

			if (o + 2 >= bytes)
				continue;

			if (pixel_v) {
				result[o + 0] = (pixel_v * r) >> 8;
				result[o + 1] = (pixel_v * g) >> 8;
				result[o + 2] = (pixel_v * b) >> 8;
			}
		}
	}

	if (underline)
	{
		int pixel_v = invert ? 0 : 255;

		int u_height = std::max(1, h / 20);

		for(int y=0; y<u_height; y++)
		{
			for(unsigned int xo=0; xo<bitmap->width; xo++)
			{
				int o = (h - (1 + y)) * w * 3 + (x + xo) * 3;

				if (o + 2 >= bytes)
					continue;

				result[o + 0] = (pixel_v * r) >> 8;
				result[o + 1] = (pixel_v * g) >> 8;
				result[o + 2] = (pixel_v * b) >> 8;
			}
		}
	}
}

void font::init_fonts()
{
	FT_Init_FreeType(&font::library);
}

void font::uninit_fonts()
{
	pthread_mutex_lock(&freetype2_lock);

	std::map<std::string, FT_Face>::iterator it = font_cache.begin();

	while(it != font_cache.end())
		FT_Done_Face(it -> second);

	FT_Done_FreeType(font::library);

	pthread_mutex_unlock(&freetype2_lock);
}

font::font(const std::string & filename, const std::string & text, const int target_height, const bool antialias) {
	// this sucks a bit but apparently freetype2 is not thread safe
	pthread_mutex_lock(&freetype2_lock);

	result = NULL;

	FT_Face face = NULL;
	std::map<std::string, FT_Face>::iterator it = font_cache.find(filename);
	if (it == font_cache.end())
	{
		int rc = FT_New_Face(library, filename.c_str(), 0, &face);
		if (rc) {
			printf("cannot open %s: %x\n", filename.c_str(), rc);
			throw format("cannot open font file %s: %x", filename.c_str(), rc);
		}

		font_cache.insert(std::pair<std::string, FT_Face>(filename, face));
	}
	else
	{
		face = it -> second;
	}

	FT_Set_Char_Size(face, target_height * 64, target_height * 64, 72, 72); /* set character size */
	FT_GlyphSlot slot = face->glyph;

	{
		// FreeType uses Unicode as glyph index; so we have to convert string from UTF8 to Unicode(UTF32)
		int utf16_buf_size = text.size() + 1; // +1 for the last '\0'
		UChar *utf16_str = new UChar[utf16_buf_size];
		UErrorCode err = U_ZERO_ERROR;
		int utf16_length;
		u_strFromUTF8(utf16_str, utf16_buf_size, &utf16_length, text.c_str(), text.size(), &err);
		if (err != U_ZERO_ERROR) {
			fprintf(stderr, "u_strFromUTF8() failed: %s\n", u_errorName(err));
			return;
		}

		int utf32_buf_size = utf16_length + 1; // +1 for the last '\0'
		UChar32 *utf32_str = new UChar32[utf32_buf_size];
		int utf32_length;
		u_strToUTF32(utf32_str, utf32_buf_size, &utf32_length, utf16_str, utf16_length, &err);
		if (err != U_ZERO_ERROR) {
			fprintf(stderr, "u_strToUTF32() failed: %s\n", u_errorName(err));
			return;
		}
	}

	w = 0;

	bool use_kerning = FT_HAS_KERNING(face);
#ifdef DEBUG
	printf("Has kerning: %d\n", use_kerning);
#endif

	max_ascender = 0;

	int max_descender = 0;
	int prev_glyph_index = -1;
	for(unsigned int n = 0; n < text.size();)
	{
		char c = text.at(n);

		if (c == '$')
		{
			char c2 = n < text.size() - 1 ? text.at(++n) : 0;

			if (c2 == '$')
				goto just_draw1;

			n += 1;

			continue;
		}

just_draw1:
		int glyph_index = FT_Get_Char_Index(face, c);

		FT_Vector akern = { 0, 0 };
		if (use_kerning && prev_glyph_index != -1 && glyph_index)
		{
			if (FT_Get_Kerning(face, prev_glyph_index, glyph_index, FT_KERNING_DEFAULT, &akern))
				w += akern.x;
		}

		if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | (antialias ? 0 : FT_LOAD_MONOCHROME)))
		{
			n++;
			continue;
		}

		w += face -> glyph -> metrics.horiAdvance;

		max_ascender = std::max(max_ascender, int(face -> glyph -> metrics.horiBearingY));
		max_descender = std::max(max_descender, int(face -> glyph -> metrics.height - face -> glyph -> metrics.horiBearingY));

#ifdef DEBUG
		printf("char %c w×h = %.1fx%.1f ascender %.1f bearingx %.1f bitmap: %dx%d left/top: %d,%d akern %ld,%ld\n",
				c,
				face -> glyph -> metrics.horiAdvance / 64.0, face -> glyph -> metrics.height / 64.0, // wxh
				face -> glyph -> metrics.horiBearingY / 64.0, // ascender
				face -> glyph -> metrics.horiBearingX / 64.0, // bearingx
				face -> glyph -> bitmap.width, face -> glyph -> bitmap.rows, // bitmap wxh
				face -> glyph -> bitmap_left, face -> glyph -> bitmap_top,
				akern.x, akern.y);
#endif

		prev_glyph_index = glyph_index;

		n++;
	}

	h = max_ascender + max_descender;
#ifdef DEBUG
	printf("bitmap dimensions w×h = %.1f×%.1f\n", w / 64.0, h / 64.0);
#endif

	w /= 64;
	h /= 64;

	want_flash = false;
	is_idle = false;
	duration = 10 * 1000 * 1000;
	prio = 0;
	org = text;

	// target_height!!
	bytes = w * target_height * 3;
	result = new uint8_t[bytes];

	uint8_t color_r = 0xff, color_g = 0xff, color_b = 0xff;
	uint8_t bcr = 0x00, bcg = 0x00, bcb = 0x00;
	bool invert = false, underline = false, rainbow = false;

	memset(result, 0x00, bytes);

	double x = 0.0;

	prev_glyph_index = -1;
	for(unsigned int n = 0; n < text.size();)
	{
		const char c = text.at(n);

		if (c == '$')
		{
			const std::string::size_type eo = text.find('$', ++n);
			if (eo == std::string::npos)
				break;

			char c2 = text.at(n++);

			if (c2 == 'i')
				invert = !invert;

			else if (c2 == 'I')
				is_idle = true;

			else if (c2 == 'u')
				underline = !underline;

			else if (c2 == 'f')
				want_flash = true;

			else if (c2 == 'd') {
				std::string temp = text.substr(n, eo - n);
				duration = int64_t(atof(temp.c_str()) * 1000 * 1000);
			}

			else if (c2 == 'p') {
				std::string temp = text.substr(n, eo - n);
				prio = atoi(temp.c_str());
			}

			else if (c2 == 'r')
				rainbow = !rainbow;

			else if (c2 == 'C') {
				hex_str_to_rgb(text.substr(n, 6), &color_r, &color_g, &color_b);
			}

			else if (c2 == 'B') {
				hex_str_to_rgb(text.substr(n, 6), &bcr, &bcg, &bcb);
			}
			else {
				printf("%c not understood\n", c2);
				break;
			}

			n = eo + 1;
			continue;
		}

just_draw2:
		int glyph_index = FT_Get_Char_Index(face, c);

		if (use_kerning && prev_glyph_index != -1 && glyph_index)
		{
			FT_Vector akern = { 0, 0 };
			FT_Get_Kerning(face, prev_glyph_index, glyph_index, FT_KERNING_DEFAULT, &akern);
			x += akern.x;
		}

		if (FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER))
		{
			n++;
			continue;
		}

		draw_bitmap(&slot->bitmap, target_height, x / 64.0, max_ascender / 64.0 - slot -> bitmap_top, color_r, color_g, color_b, invert, underline, rainbow, bcr, bcg, bcb);

		x += face -> glyph -> metrics.horiAdvance;

		prev_glyph_index = glyph_index;

		n++;
	}

	pthread_mutex_unlock(&freetype2_lock);
}

font::~font()
{
	delete [] result;
}

textImage * font::getImage()
{
	textImage *ti = new textImage(result, this -> w, this -> h, want_flash, is_idle, duration, prio, org, true);
	result = NULL; // transfer ownership of buffer
	return ti;
}

int font::getMaxAscender() const
{
	return max_ascender;
}

// from http://stackoverflow.com/questions/10542832/how-to-use-fontconfig-to-get-font-list-c-c
std::string find_font_by_name(const std::string & font_name, const std::string & default_font_file)
{
	std::string fontFile = default_font_file;

	pthread_mutex_lock(&fontconfig_lock);

	FcConfig* config = FcInitLoadConfigAndFonts();

	// configure the search pattern, 
	// assume "name" is a std::string with the desired font name in it
	FcPattern* pat = FcNameParse((const FcChar8*)(font_name.c_str()));

	if (pat)
	{
		if (FcConfigSubstitute(config, pat, FcMatchPattern))
		{
			FcDefaultSubstitute(pat);

			// find the font
			FcResult result = FcResultNoMatch;
			FcPattern* font = FcFontMatch(config, pat, &result);
			if (font)
			{
				FcChar8* file = NULL;
				if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch && file != NULL)
				{
					// save the file to another std::string
					fontFile = (const char *)file;
				}

				FcPatternDestroy(font);
			}
		}

		FcPatternDestroy(pat);
	}

	pthread_mutex_unlock(&fontconfig_lock);

	return fontFile;
}

#if defined(DEBUG) || defined(DEBUG_IMG)
int main(int argc, char *argv[])
{
	printf("%s\n", find_font_by_name("Arial").c_str());

	const int h = 100;
	font f(FONT, "_g$iq$ite#12ff56$$$ut$u1$i2$i3$$", h);

	uint8_t *p = NULL;
	int w = 0;
	f.getImage(&w, &p);

#ifdef DEBUG_IMG
	printf("P6 %d %d %d\n", w, h, 255);

	for(int y=0; y<h; y++)
	{
		for(int x=0; x<w; x++)
		{
			int o = y * w * 3 + x * 3;
			printf("%c%c%c", p[o + 0], p[o + 1], p[o + 2]);
		}
	}
#else
	printf("%p %dx%d\n", p, w, h);
#endif

	return 0;
}
#endif
