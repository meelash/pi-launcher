#include "launcher.h"

namespace {

// Same look as Typing Adventure.
const Color BgTop = 0xFFFFF6E0;
const Color BgBottom = 0xFFE3F1FB;
const Color Ink = 0xFF2B3045;
const Color InkSoft = 0xFF6B7190;
const Color Faint = 0xFFB9BDCF;
const Color Panel = 0xFFFFFFFF;
const Color Navy = 0xFF2F3E75;
const Color Accent = 0xFFFF7A45;
const Color Bad = 0xFFE5484D;
const Color Shadow = 0x30202848;

// Accent colour per icon.
Color IconColor(const char *icon)
{
	if (StrEq(icon, "keyboard"))
		return 0xFF4DABF7;
	if (StrEq(icon, "graph"))
		return 0xFF2EAD6B;
	if (StrEq(icon, "write"))
		return 0xFF9775FA;
	return 0xFFFF7A45;
}

// Copies the next '|'-separated field of s into out and advances s.
void NextField(const char *&s, char *out, int cap)
{
	while (*s == ' ')
		s++;
	int n = 0;
	while (*s && *s != '|') {
		if (n < cap - 1)
			out[n++] = *s;
		s++;
	}
	while (n && out[n - 1] == ' ')
		n--;
	out[n] = 0;
	if (*s == '|')
		s++;
}

} // namespace

bool Manifest::Parse(const char *text, int len)
{
	count = 0;
	autostart = 0;
	char line[200];
	int i = 0;
	while (i < len) {
		int n = 0;
		while (i < len && text[i] != '\n') {
			if (text[i] != '\r' && n < (int)sizeof line - 1)
				line[n++] = text[i];
			i++;
		}
		i++;
		line[n] = 0;
		const char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || !*p)
			continue;
		const char *eq = p;
		while (*eq && *eq != '=')
			eq++;
		if (!*eq)
			continue;
		char key[16];
		StrCopy(key, p, Min((int)sizeof key, (int)(eq - p) + 1));
		const char *val = eq + 1;
		if (StrEq(key, "autostart")) {
			u32 v;
			if (ParseUInt(val, &v))
				autostart = (int)Min<u32>(v, 600);
		} else if (StrEq(key, "app") && count < kMaxApps) {
			AppEntry &a = apps[count];
			MemZero(&a, sizeof a);
			NextField(val, a.file, sizeof a.file);
			NextField(val, a.name, sizeof a.name);
			NextField(val, a.icon, sizeof a.icon);
			if (!a.file[0])
				continue;
			if (!a.name[0])
				StrCopy(a.name, a.file, sizeof a.name);
			count++;
		}
	}
	return count > 0;
}

Launcher::Launcher(Platform *platform) : m_platform(platform) {}

void Launcher::Init()
{
	m_now = m_platform->Millis();
	m_state = StMenu;
	m_stateStart = m_now;
	m_lastKey = 0;
	m_sel = 0;
	m_countdown = false;
	m_dirty = true;
	m_diag = false;
	m_diagScroll = 0;
	m_error[0] = 0;
	m_lastFile[0] = 0;
	m_startingFrames = 0;

	static char buf[4096];
	int len = 0;
	m_manifestOK = m_platform->LoadFile(kManifestFile, buf, sizeof buf - 1, &len) &&
		       m_manifest.Parse(buf, len);
	if (!m_manifestOK) {
		m_manifest.count = 0;
		return;
	}
	for (int i = 0; i < m_manifest.count; i++) {
		AppEntry &a = m_manifest.apps[i];
		char path[64] = LAUNCHER_DIR "/";
		StrAppend(path, a.file, sizeof path);
		a.present = m_platform->FileSize(path, &a.size) && a.size > 0;
	}

	// A program asked to come back to the menu: wait for a choice.
	bool showMenu = m_platform->RemoveFile(kShowMenuFile);

	if (m_platform->LoadFile(kLastFile, buf, sizeof buf - 1, &len)) {
		while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' '))
			len--;
		buf[len] = 0;
		StrCopy(m_lastFile, buf, sizeof m_lastFile);
		for (int i = 0; i < m_manifest.count; i++)
			if (StrEq(m_manifest.apps[i].file, m_lastFile))
				m_sel = i;
	}
	bool lastKnown = m_lastFile[0] && StrEq(m_manifest.apps[m_sel].file, m_lastFile);
	if (m_manifest.autostart > 0 && !showMenu && lastKnown && m_manifest.apps[m_sel].present) {
		m_countdown = true;
		m_countdownStart = m_now;
	}
	// With a single program there is nothing to choose.
	if (m_manifest.count == 1 && m_manifest.apps[0].present && !showMenu) {
		m_countdown = m_manifest.autostart > 0;
		m_countdownStart = m_now;
	}
}

u32 Launcher::CountdownLeftMs() const
{
	u32 total = (u32)m_manifest.autostart * 1000;
	u32 gone = m_now - m_countdownStart;
	return gone >= total ? 0 : total - gone;
}

void Launcher::Start(int index)
{
	if (index < 0 || index >= m_manifest.count)
		return;
	m_sel = index;
	m_countdown = false;
	if (!m_manifest.apps[index].present) {
		StrCopy(m_error, "This program is not on the SD card.", sizeof m_error);
		m_state = StError;
		m_stateStart = m_now;
		return;
	}
	m_state = StStarting;
	m_stateStart = m_now;
	m_startingFrames = 0;
}

void Launcher::OnKey(const KeyEvent &e)
{
	m_dirty = true;
	m_lastKey = m_now;
	if (e.usage == KeyF12) {
		m_diag = !m_diag;
		m_diagScroll = 0;
		m_countdown = false;
		return;
	}
	if (m_diag) {
		switch (e.usage) {
		case KeyUp: m_diagScroll++; break;
		case KeyDown: m_diagScroll = Max(0, m_diagScroll - 1); break;
		case KeyPageUp: m_diagScroll += 20; break;
		case KeyPageDown: m_diagScroll = Max(0, m_diagScroll - 20); break;
		case KeyHome: m_diagScroll = 1 << 20; break;
		case KeyEnd: m_diagScroll = 0; break;
		case KeyEscape: m_diag = false; break;
		}
		return;
	}
	if (m_state == StStarting || m_state == StDone)
		return;
	if (m_state == StError) {
		m_state = StMenu;
		return;
	}
	// Any key stops the countdown; Enter starts the selected program at once.
	bool wasCounting = m_countdown;
	m_countdown = false;
	int n = m_manifest.count;
	if (!n)
		return;
	switch (e.usage) {
	case KeyLeft:
		m_sel = (m_sel + n - 1) % n;
		return;
	case KeyRight:
	case KeyTab:
		m_sel = (m_sel + 1) % n;
		return;
	case KeyUp:
		if (n > 3)
			m_sel = Max(0, m_sel - 4);
		return;
	case KeyDown:
		if (n > 3)
			m_sel = Min(n - 1, m_sel + 4);
		return;
	case KeyEnter:
	case KeyPadEnter:
	case KeySpace:
		Start(m_sel);
		return;
	}
	if (e.usage >= Key1 && e.usage <= Key1 + 8)
		Start(e.usage - Key1);
	else if (e.usage >= KeyPad1 && e.usage <= KeyPad1 + 8)
		Start(e.usage - KeyPad1);
	(void)wasCounting;
}

void Launcher::Update()
{
	m_now = m_platform->Millis();
	m_dirty = true;  // the countdown and the selection pulse animate
	if (m_state == StMenu && m_countdown && CountdownLeftMs() == 0)
		Start(m_sel);
	// Let the "Starting..." screen reach the display before loading,
	// which takes a moment and blocks the loop.
	if (m_state == StStarting && m_startingFrames >= 2) {
		const AppEntry &a = m_manifest.apps[m_sel];
		if (!StrEq(m_lastFile, a.file)) {
			StrCopy(m_lastFile, a.file, sizeof m_lastFile);
			m_platform->SaveFile(kLastFile, a.file, StrLen(a.file));
		}
		char path[64] = LAUNCHER_DIR "/";
		StrAppend(path, a.file, sizeof path);
		if (m_platform->Launch(path, m_error, sizeof m_error))
			m_state = StDone;
		else
			m_state = StError;
		m_stateStart = m_now;
	}
}

// ---------------------------------------------------------------------------
// Drawing

void Launcher::DrawIcon(Canvas &c, const char *icon, int cx, int cy, int s)
{
	Color col = IconColor(icon);
	if (StrEq(icon, "keyboard")) {
		int w = s * 2, h = s * 6 / 5;
		c.FillRoundRect(cx - w / 2, cy - h / 2 + 6, w, h, s / 6, Darken(col, 60));
		c.FillRoundRect(cx - w / 2, cy - h / 2, w, h, s / 6, col);
		static const Color keyCols[] = {0xFFF78FB3, 0xFFFFB347, 0xFFFFE066, 0xFF7BD389, 0xFFFFFFFF};
		int k = w / 11, gap = k / 5;
		int top = cy - h / 2 + gap * 3;
		for (int row = 0; row < 3; row++) {
			int keys = 9 - row;
			int rowW = keys * k + (keys - 1) * gap;
			int x0 = cx - rowW / 2;
			for (int i = 0; i < keys; i++) {
				Color kc = (row + i) % 4 == 1 ? keyCols[(row * 3 + i) % 4] : keyCols[4];
				c.FillRoundRect(x0 + i * (k + gap), top + row * (k + gap), k, k, k / 4, kc);
			}
		}
		int spaceW = k * 5;
		c.FillRoundRect(cx - spaceW / 2, top + 3 * (k + gap), spaceW, k * 3 / 4, k / 4, 0xFFFFFFFF);
		return;
	}
	if (StrEq(icon, "graph")) {
		int w = s * 2, h = s * 3 / 2;
		int x0 = cx - w / 2, y0 = cy - h / 2;
		c.FillRoundRect(x0, y0 + 6, w, h, s / 8, Darken(col, 60));
		c.FillRoundRect(x0, y0, w, h, s / 8, 0xFFFFFFFF);
		c.StrokeRoundRect(x0, y0, w, h, s / 8, 3, col);
		c.SetClip(x0 + 4, y0 + 4, w - 8, h - 8);
		for (int i = 1; i < 8; i++)
			c.FillRect(x0 + i * w / 8, y0, 1, h, 0xFFE3E8F0);
		for (int i = 1; i < 6; i++)
			c.FillRect(x0, y0 + i * h / 6, w, 1, 0xFFE3E8F0);
		c.FillRect(cx - 1, y0, 3, h, Ink);
		c.FillRect(x0, cy - 1, w, 3, Ink);
		// A parabola and a sine wave.
		float px = 0, py = 0, sx = 0, sy = 0;
		for (int i = 0; i <= 40; i++) {
			float t = -1 + i / 20.0f;
			float qx = cx + t * w / 2, qy = cy + h * 0.42f - t * t * h * 0.8f;
			float sin = ISin((int)(t * 768)) / 1024.0f;  // 1.5 turns across the icon
			float rx = cx + t * w / 2, ry = cy - sin * h * 0.22f;
			if (i) {
				c.LineAA(px, py, qx, qy, s / 18.0f + 2, 0xFF4DABF7);
				c.LineAA(sx, sy, rx, ry, s / 18.0f + 2, Accent);
			}
			px = qx, py = qy, sx = rx, sy = ry;
		}
		c.ResetClip();
		return;
	}
	if (StrEq(icon, "write")) {
		int w = s * 3 / 2, h = s * 2;
		c.FillRoundRect(cx - w / 2 + 6, cy - h / 2 + 6, w, h, 10, Shadow);
		c.FillRoundRect(cx - w / 2, cy - h / 2, w, h, 10, 0xFFFFFFFF);
		c.StrokeRoundRect(cx - w / 2, cy - h / 2, w, h, 10, 3, col);
		for (int i = 0; i < 6; i++)
			c.FillRoundRect(cx - w / 2 + 20, cy - h / 2 + 28 + i * h / 8, i == 5 ? w / 2 : w - 40, 6, 3,
					Lighten(col, 120));
		return;
	}
	c.FillCircle(cx, cy, s, Lighten(col, 160));
	c.FillStar(cx, cy, s * 3 / 4, col);
}

void Launcher::DrawCard(Canvas &c, int i, int x, int y, int w, int h)
{
	const AppEntry &a = m_manifest.apps[i];
	bool sel = i == m_sel && m_state != StError;
	Color col = IconColor(a.icon);
	int lift = sel ? 10 : 0;
	y -= lift;
	c.FillRoundRect(x + 4, y + 10 + lift, w, h, 28, Shadow);
	c.FillRoundRect(x, y, w, h, 28, Panel);
	if (sel)
		c.StrokeRoundRect(x - 6, y - 6, w + 12, h + 12, 32, 6, col);
	// Coloured band behind the icon.
	c.SetClip(x, y, w, h * 62 / 100);
	c.FillRoundRect(x, y, w, h * 62 / 100 + 30, 28, Lighten(col, 190));
	c.ResetClip();
	DrawIcon(c, a.icon, x + w / 2, y + h * 31 / 100, Min(w, h) / 4);
	// Number badge for quick start.
	char num[4] = {(char)('1' + i), 0};
	c.FillCircle(x + 34, y + 34, 20, sel ? col : Faint);
	c.Text(num, font::Small, x + 34, y + 42, 0xFFFFFFFF, AlignCenter);

	font::Size nameSize = font::Title;
	while (nameSize > font::Tiny && Canvas::Measure(a.name, nameSize) > w - 30)
		nameSize = (font::Size)(nameSize - 1);
	bool small = h < 300;
	c.TextCentered(a.name, nameSize, x + w / 2, y + h * 62 / 100, h * (small ? 20 : 22) / 100,
		       a.present ? Ink : Faint, AlignCenter);
	int statusY = y + h - (small ? 16 : 26);
	if (!a.present) {
		c.Text("Not on the card", font::Small, x + w / 2, statusY, Bad, AlignCenter);
	} else if (sel && m_countdown) {
		// Progress bar that fills up as the countdown runs.
		u32 total = (u32)m_manifest.autostart * 1000;
		int bw = w - 60;
		int filled = (int)((unsigned long long)(total - CountdownLeftMs()) * (u32)bw / Max<u32>(total, 1));
		c.FillRoundRect(x + 30, statusY - 14, bw, 14, 7, Lighten(col, 190));
		c.FillRoundRect(x + 30, statusY - 14, Max(14, filled), 14, 7, col);
	} else if (sel) {
		c.Text("Press Enter", font::Small, x + w / 2, statusY, col, AlignCenter);
	}
}

void Launcher::DrawMessage(Canvas &c, const char *title, const char *detail, Color color)
{
	c.FillRoundRect(240, 250, 800, 220, 30, Panel);
	c.StrokeRoundRect(240, 250, 800, 220, 30, 4, color);
	c.Text(title, font::Title, W / 2, 330, color, AlignCenter);
	if (detail && detail[0])
		c.Text(detail, font::Body, W / 2, 395, Ink, AlignCenter);
}

void Launcher::DrawDiagnostics(Canvas &c, bool viewer)
{
	int top = viewer ? 20 : 300, h = H - top - 20;
	c.FillRoundRect(20, top, W - 40, h, 16, 0xF01E2336);
	static const char *lines[400];
	int n = m_platform->Diagnostics(lines, 400);
	int rows = (h - 60) / 24;
	int scroll = Clamp(m_diagScroll, 0, Max(0, n - rows));
	int first = Max(0, n - rows - scroll);
	c.Text(viewer ? "System log  (arrows / PgUp / PgDn to scroll, Esc or F12 to close)"
		      : "No keyboard found yet. Plug one in. System log:",
	       font::Small, 40, top + 34, 0xFFFFE27A);
	for (int i = first; i < n && i < first + rows; i++)
		c.Text(lines[i], font::Tiny, 40, top + 64 + (i - first) * 24, 0xFFE8EAF2);
}

bool Launcher::Draw(Canvas &c)
{
	if (!m_dirty)
		return false;
	m_dirty = false;
	c.VGradient(0, 0, W, H, BgTop, BgBottom);
	// Soft decorative circles.
	c.FillCircle(90, 640, 160, 0x18FF7A45);
	c.FillCircle(1210, 90, 130, 0x184DABF7);

	if (!m_manifestOK) {
		c.Text("Choose a program", font::Title, W / 2, 90, Navy, AlignCenter);
		DrawMessage(c, "No programs found",
			    m_platform->StorageAvailable() ? "The file apps/apps.txt is missing or empty."
							   : "The SD card could not be read.",
			    Bad);
		if (m_diag)
			DrawDiagnostics(c, true);
		return true;
	}

	int n = m_manifest.count;
	if (m_countdown) {
		char line[128] = "Starting ";
		StrAppend(line, m_manifest.apps[m_sel].name, sizeof line);
		StrAppend(line, " in ", sizeof line);
		StrAppendUInt(line, (CountdownLeftMs() + 999) / 1000, sizeof line);
		StrAppend(line, "...", sizeof line);
		c.Text(line, font::Title, W / 2, 90, Navy, AlignCenter);
		c.Text("Press any key to choose something else", font::Body, W / 2, 140, InkSoft, AlignCenter);
	} else {
		c.Text("Choose a program", font::Title, W / 2, 100, Navy, AlignCenter);
	}

	// Cards: one row of up to 3 big cards, or rows of 4 smaller ones.
	int perRow = n <= 3 ? n : 4;
	int cw = n <= 3 ? 340 : 270, ch = n <= 3 ? 380 : 220, gap = n <= 3 ? 50 : 26;
	int rows = (n + perRow - 1) / perRow;
	int top = n <= 3 ? 190 : (rows > 1 ? 160 : 230);
	for (int i = 0; i < n; i++) {
		int row = i / perRow, col = i % perRow;
		int inRow = Min(perRow, n - row * perRow);
		int rowW = inRow * cw + (inRow - 1) * gap;
		DrawCard(c, i, (W - rowW) / 2 + col * (cw + gap), top + row * (ch + gap), cw, ch);
	}
	(void)rows;

	const char *hint = n > 1 ? "\xE2\x86\x90 \xE2\x86\x92  choose        Enter  start        number keys  quick start"
				 : "Enter  start";
	c.Text(hint, font::Small, W / 2, H - 26, InkSoft, AlignCenter);

	if (m_state == StStarting || m_state == StDone) {
		c.FillRect(0, 0, W, H, 0x90FFF6E0);
		char line[96] = "Starting ";
		StrAppend(line, m_manifest.apps[m_sel].name, sizeof line);
		StrAppend(line, "...", sizeof line);
		DrawMessage(c, line, "", IconColor(m_manifest.apps[m_sel].icon));
		m_startingFrames++;
	} else if (m_state == StError) {
		DrawMessage(c, "Could not start the program", m_error, Bad);
		c.Text("Press any key", font::Small, W / 2, 445, InkSoft, AlignCenter);
	}

	if (m_diag)
		DrawDiagnostics(c, true);
	else if (!m_countdown && m_state == StMenu && !m_platform->KeyboardPresent() &&
		 m_now - m_stateStart > kNoKeyboardMs)
		DrawDiagnostics(c, false);
	return true;
}
