// Software rendering onto a 32-bit ARGB canvas (matches the Pi framebuffer).
// Shapes from pi-typing; text is Latin-only (pre-rendered Andika).
#pragma once
#include "base.h"
#include "font.h"

typedef u32 Color;  // 0xAARRGGBB; alpha < 0xFF blends

inline Color Rgb(u32 rgb) { return 0xFF000000u | rgb; }
inline Color WithAlpha(Color c, u32 a) { return (c & 0x00FFFFFFu) | (Min<u32>(a, 255) << 24); }
Color Mix(Color a, Color b, int t256);  // t256=0 -> a, 256 -> b
Color Lighten(Color c, int t256);
Color Darken(Color c, int t256);

enum Align { AlignLeft, AlignCenter, AlignRight };

struct Point
{
	int x, y;
};

// Next codepoint of a UTF-8 string (advances s); 0 at the end.
u32 Utf8Next(const char *&s);

class Canvas
{
public:
	Canvas(u32 *pixels, int w, int h) : m_px(pixels), m_w(w), m_h(h) { ResetClip(); }

	int Width() const { return m_w; }
	int Height() const { return m_h; }
	u32 *Pixels() { return m_px; }

	void SetClip(int x, int y, int w, int h);
	void ResetClip() { m_cx0 = 0, m_cy0 = 0, m_cx1 = m_w, m_cy1 = m_h; }

	void Clear(Color c);
	void FillRect(int x, int y, int w, int h, Color c);
	void FillRoundRect(int x, int y, int w, int h, int r, Color c);
	void StrokeRoundRect(int x, int y, int w, int h, int r, int thickness, Color c);
	void FillCircle(int cx, int cy, int r, Color c);
	void FillEllipse(int cx, int cy, int rx, int ry, Color c);
	void FillPolygon(const Point *pts, int n, Color c);  // anti-aliased, small shapes
	void FillStar(int cx, int cy, int r, Color c);
	void VGradient(int x, int y, int w, int h, Color top, Color bottom);
	void Line(int x0, int y0, int x1, int y1, int thickness, Color c);
	// Anti-aliased line with sub-pixel end points and round caps.
	void LineAA(float x0, float y0, float x1, float y1, float width, Color c);

	// Text. y is the baseline. Returns the drawn width.
	int Text(const char *utf8, font::Size size, int x, int y, Color c, Align align = AlignLeft);
	// Text vertically centred in a box of height h starting at top.
	int TextCentered(const char *utf8, font::Size size, int x, int top, int h, Color c,
			 Align align = AlignLeft);
	static int Measure(const char *utf8, font::Size size);
	// Height of capital letters, for centring.
	static int CapHeight(font::Size size) { return font::g_Latin[size].ascent * 3 / 4; }

	void BlendPixel(int x, int y, Color c, u32 coverage255);

private:
	inline void Blend(u32 *p, Color c, u32 a);
	void DrawGlyph(const font::Glyph *g, const font::Face &face, int x, int y, Color c);

	u32 *m_px;
	int m_w, m_h;
	int m_cx0, m_cy0, m_cx1, m_cy1;
};
