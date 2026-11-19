// Desktop simulator: runs the launcher with scripted input against a folder
// that stands in for the SD card, and writes screenshots.
//
// Usage: sim <script> <outdir> <carddir>
// Script commands (one per line):
//   key <name>          enter esc tab space left right up down f12 1..9
//   wait <ms>           advances virtual time
//   shot <name>         writes <outdir>/<name>.png
//   expect <state>      menu | starting | done | error (fails otherwise)
//   expect-sel <n>      the selected card (1-based)
//   expect-launched <f> the program image that was started
//   expect-file <f> <yes|no>  whether a file exists on the card
//   countdown <on|off>  fails unless the countdown is in that state
//   keyboard <on|off>   whether a keyboard is plugged in (default on)
//   reboot              starts the launcher again (same card)
//   write <file> <text> writes a file on the card (\\n for line breaks)
#include "../core/launcher.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <zlib.h>

class SimPlatform : public Platform
{
public:
	u32 now = 0;
	std::string card;
	bool keyboard = true;
	std::string launched;
	std::string Path(const char *name) { return card + "/" + name; }
	u32 Millis() override { return now; }
	bool KeyboardPresent() override { return keyboard; }
	bool StorageAvailable() override { return true; }
	bool LoadFile(const char *name, char *buf, int cap, int *len) override
	{
		FILE *f = fopen(Path(name).c_str(), "rb");
		if (!f)
			return false;
		*len = (int)fread(buf, 1, (size_t)cap, f);
		fclose(f);
		return true;
	}
	bool SaveFile(const char *name, const char *buf, int len) override
	{
		FILE *f = fopen(Path(name).c_str(), "wb");
		if (!f)
			return false;
		fwrite(buf, 1, (size_t)len, f);
		fclose(f);
		return true;
	}
	bool RemoveFile(const char *name) override { return remove(Path(name).c_str()) == 0; }
	bool FileSize(const char *name, u32 *size) override
	{
		struct stat st;
		if (stat(Path(name).c_str(), &st) != 0)
			return false;
		*size = (u32)st.st_size;
		return true;
	}
	int Diagnostics(const char **lines, int max) override
	{
		static char buf[40][48];
		int n = Min(40, max);
		for (int i = 0; i < n; i++) {
			snprintf(buf[i], sizeof buf[i], "00:00:%02d.00 sim: log line %d", i / 10, i + 1);
			lines[i] = buf[i];
		}
		return n;
	}
	bool Launch(const char *name, char *err, int cap) override
	{
		u32 size;
		if (!FileSize(name, &size)) {
			StrCopy(err, "The file could not be read.", cap);
			return false;
		}
		launched = name;
		return true;
	}
};

static void WritePng(const char *path, const u32 *px, int w, int h)
{
	std::vector<unsigned char> raw;
	raw.reserve((size_t)(w * 3 + 1) * h);
	for (int y = 0; y < h; y++) {
		raw.push_back(0);
		for (int x = 0; x < w; x++) {
			u32 c = px[y * w + x];
			raw.push_back((c >> 16) & 0xFF);
			raw.push_back((c >> 8) & 0xFF);
			raw.push_back(c & 0xFF);
		}
	}
	uLongf zlen = compressBound(raw.size());
	std::vector<unsigned char> z(zlen);
	compress2(z.data(), &zlen, raw.data(), raw.size(), 6);
	FILE *f = fopen(path, "wb");
	auto be32 = [&](u32 v) {
		unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8),
				      (unsigned char)v};
		fwrite(b, 1, 4, f);
	};
	auto chunk = [&](const char *type, const unsigned char *data, u32 len) {
		be32(len);
		fwrite(type, 1, 4, f);
		if (len)
			fwrite(data, 1, len, f);
		uLong crc = crc32(0, (const Bytef *)type, 4);
		crc = crc32(crc, data, len);
		be32((u32)crc);
	};
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0};
	for (int i = 0; i < 4; i++) {
		ihdr[i] = (unsigned char)(w >> (24 - 8 * i));
		ihdr[4 + i] = (unsigned char)(h >> (24 - 8 * i));
	}
	chunk("IHDR", ihdr, 13);
	chunk("IDAT", z.data(), (u32)zlen);
	chunk("IEND", nullptr, 0);
	fclose(f);
}

static const struct
{
	const char *name;
	u8 usage;
} kKeyNames[] = {
	{"enter", KeyEnter}, {"esc", KeyEscape}, {"tab", KeyTab},   {"space", KeySpace}, {"left", KeyLeft},
	{"right", KeyRight}, {"up", KeyUp},       {"down", KeyDown}, {"f12", KeyF12},     {"x", KeyLetter('x')},
};

static const char *kStates[] = {"menu", "starting", "done", "error"};

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: sim <script> <outdir> <carddir>\n");
		return 2;
	}
	SimPlatform plat;
	plat.card = argv[3];
	static u32 pixels[Launcher::W * Launcher::H];
	Canvas canvas(pixels, Launcher::W, Launcher::H);
	static Launcher *app = new Launcher(&plat);
	app->Init();

	FILE *script = fopen(argv[1], "r");
	if (!script) {
		perror(argv[1]);
		return 2;
	}
	auto step = [&](u32 ms) {
		plat.now += ms;
		app->Update();
		app->Draw(canvas);
	};
	char line[1024];
	int lineNo = 0, failures = 0;
	auto fail = [&](const char *what, const char *want, const char *got) {
		fprintf(stderr, "%s:%d: %s: expected %s, got %s\n", argv[1], lineNo, what, want, got);
		failures++;
	};
	while (fgets(line, sizeof line, script)) {
		lineNo++;
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		char *arg = strchr(line, ' ');
		if (arg)
			*arg++ = 0;
		else
			arg = line + strlen(line);
		if (!strcmp(line, "key")) {
			u8 usage = 0;
			if (arg[0] >= '1' && arg[0] <= '9' && !arg[1])
				usage = (u8)(Key1 + arg[0] - '1');
			for (auto &kn : kKeyNames)
				if (!strcmp(kn.name, arg))
					usage = kn.usage;
			if (!usage)
				fprintf(stderr, "%d: unknown key %s\n", lineNo, arg);
			app->OnKey(KeyEvent{usage, 0});
			step(60);
		} else if (!strcmp(line, "wait")) {
			for (int ms = atoi(arg); ms > 0; ms -= 33)
				step((u32)Min(ms, 33));
		} else if (!strcmp(line, "shot")) {
			step(16);
			std::string p = std::string(argv[2]) + "/" + arg + ".png";
			WritePng(p.c_str(), pixels, Launcher::W, Launcher::H);
			printf("shot %s\n", p.c_str());
		} else if (!strcmp(line, "keyboard")) {
			plat.keyboard = strcmp(arg, "off") != 0;
		} else if (!strcmp(line, "reboot")) {
			plat.launched.clear();
			app->Init();
			step(16);
		} else if (!strcmp(line, "expect")) {
			if (strcmp(kStates[app->State()], arg))
				fail("state", arg, kStates[app->State()]);
		} else if (!strcmp(line, "expect-sel")) {
			char got[16];
			snprintf(got, sizeof got, "%d", app->Selected() + 1);
			if (strcmp(got, arg))
				fail("selection", arg, got);
		} else if (!strcmp(line, "expect-launched")) {
			if (plat.launched != arg)
				fail("launched", arg, plat.launched.empty() ? "(nothing)" : plat.launched.c_str());
		} else if (!strcmp(line, "expect-file")) {
			char *want = strchr(arg, ' ');
			*want++ = 0;
			u32 size;
			const char *got = plat.FileSize(arg, &size) ? "yes" : "no";
			if (strcmp(got, want))
				fail(arg, want, got);
		} else if (!strcmp(line, "countdown")) {
			const char *got = app->CountingDown() ? "on" : "off";
			if (strcmp(got, arg))
				fail("countdown", arg, got);
		} else if (!strcmp(line, "write")) {  // write <file> <text>
			char *text = strchr(arg, ' ');
			*text++ = 0;
			std::string body;  // "\n" in the script is a line break
			for (const char *t = text; *t; t++)
				if (t[0] == '\\' && t[1] == 'n')
					body += '\n', t++;
				else
					body += *t;
			plat.SaveFile(arg, body.data(), (int)body.size());
		} else {
			fprintf(stderr, "%d: unknown command %s\n", lineNo, line);
			failures++;
		}
	}
	fclose(script);
	return failures ? 1 : 0;
}
