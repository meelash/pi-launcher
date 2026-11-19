// The boot menu: lists the programs in apps/apps.txt and starts the chosen one.
// Platform independent; the Pi kernel and the desktop simulator both drive it.
#pragma once
#include "base.h"
#include "gfx.h"
#include "keys.h"
#include "platform.h"

// Files shared with the programs (keep in sync with their READMEs).
#define LAUNCHER_DIR "apps"
static const char kManifestFile[] = LAUNCHER_DIR "/apps.txt";
static const char kLastFile[] = LAUNCHER_DIR "/last.txt";
// A program writes this file before rebooting to come back to the menu, so
// the menu waits for a choice instead of starting the last program again.
static const char kShowMenuFile[] = LAUNCHER_DIR "/show-menu";

struct AppEntry
{
	char file[48];   // image name inside apps/, e.g. "typing.img"
	char name[48];   // shown on the card
	char icon[16];   // keyboard, graph, write, star
	bool present;    // the image exists on the card
	u32 size;
};

struct Manifest
{
	static const int kMaxApps = 8;
	AppEntry apps[kMaxApps];
	int count;
	int autostart;  // seconds before the last program starts by itself; 0 = never

	// Parses "key=value" lines; returns false if no program is listed.
	bool Parse(const char *text, int len);
};

enum LauncherState { StMenu, StStarting, StDone, StError };

class Launcher
{
public:
	static const int W = 1280, H = 720;
	static const u32 kNoKeyboardMs = 6000;

	explicit Launcher(Platform *platform);
	void Init();
	void OnKey(const KeyEvent &e);
	void Update();
	// Renders a frame if anything changed; returns false when nothing was drawn.
	bool Draw(Canvas &c);
	// True once a program was loaded: the kernel then leaves its main loop and starts it.
	bool Done() const { return m_state == StDone; }

	// Introspection for the simulator / tests.
	LauncherState State() const { return m_state; }
	int Selected() const { return m_sel; }
	bool CountingDown() const { return m_countdown; }

private:
	void Start(int index);
	void DrawCard(Canvas &c, int i, int x, int y, int w, int h);
	void DrawIcon(Canvas &c, const char *icon, int cx, int cy, int s);
	void DrawDiagnostics(Canvas &c, bool viewer);
	void DrawMessage(Canvas &c, const char *title, const char *detail, Color color);
	u32 CountdownLeftMs() const;

	Platform *m_platform;
	Manifest m_manifest;
	bool m_manifestOK;
	LauncherState m_state;
	int m_sel;
	bool m_countdown;
	u32 m_now, m_countdownStart, m_stateStart, m_lastKey;
	int m_startingFrames;
	bool m_dirty;
	bool m_diag;
	int m_diagScroll;
	char m_error[160];
	char m_lastFile[48];
};
