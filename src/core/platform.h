// What the launcher needs from the hardware. Implemented by the Pi kernel
// (src/pi) and by the desktop simulator (src/sim).
#pragma once
#include "base.h"

class Platform
{
public:
	virtual ~Platform() {}
	virtual u32 Millis() = 0;
	// Files on the SD card (paths relative to its root, e.g. "apps/apps.txt").
	virtual bool LoadFile(const char *name, char *buf, int cap, int *len) = 0;
	virtual bool SaveFile(const char *name, const char *buf, int len) = 0;
	// Deletes a file; true if it existed.
	virtual bool RemoveFile(const char *name) = 0;
	virtual bool FileSize(const char *name, u32 *size) = 0;
	virtual bool KeyboardPresent() = 0;
	virtual bool StorageAvailable() = 0;
	// Status and log lines (oldest first).
	virtual int Diagnostics(const char **lines, int max) { return 0; }
	// Reads a program image and arranges to start it when the main loop
	// ends (see Launcher::Done). On failure writes a reason into err.
	virtual bool Launch(const char *name, char *err, int cap) = 0;
};
