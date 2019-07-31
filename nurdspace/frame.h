class frame
{
	private:
		const int w, h, nb;
		uint8_t *pixels;

	public:
		frame(const int w, const int h);
		~frame();
		int width() const { return w; }
		int height() const { return h; }
		bool getPixel(int x, int y, int *const r, int *const g, int *const b) const;
		bool setPixel(int x, int y, int r, int g, int b);
		uint8_t *getLow() const { return pixels; }
		void setContents(const frame & in);
		void overlay(const frame & in);
		void clear();
		void fade();
		void put(FrameCanvas *const offscreen_canvas);
};
