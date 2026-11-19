// USB HID keyboard usage codes and modifier bits (raw HID mode: the app
// does its own layout handling, there is no OS keymap).
#pragma once
#include "base.h"

enum : u8 {
	KeyA = 0x04,  // ... KeyZ = 0x1D
	Key1 = 0x1E,  // ... Key9 = 0x26
	Key0 = 0x27,
	KeyEnter = 0x28,
	KeyEscape = 0x29,
	KeyBackspace = 0x2A,
	KeyTab = 0x2B,
	KeySpace = 0x2C,
	KeyMinus = 0x2D,
	KeyEqual = 0x2E,
	KeyLeftBracket = 0x2F,
	KeyRightBracket = 0x30,
	KeyBackslash = 0x31,
	KeySemicolon = 0x33,
	KeyApostrophe = 0x34,
	KeyGrave = 0x35,
	KeyComma = 0x36,
	KeyPeriod = 0x37,
	KeySlash = 0x38,
	KeyCapsLock = 0x39,
	KeyF1 = 0x3A,  // ... KeyF12 = 0x45
	KeyF12 = 0x45,
	KeyInsert = 0x49,
	KeyHome = 0x4A,
	KeyPageUp = 0x4B,
	KeyDelete = 0x4C,
	KeyEnd = 0x4D,
	KeyPageDown = 0x4E,
	KeyRight = 0x4F,
	KeyLeft = 0x50,
	KeyDown = 0x51,
	KeyUp = 0x52,
	KeyPadSlash = 0x54,
	KeyPadStar = 0x55,
	KeyPadMinus = 0x56,
	KeyPadPlus = 0x57,
	KeyPadEnter = 0x58,
	KeyPad1 = 0x59,  // ... KeyPad9 = 0x61
	KeyPad0 = 0x62,
	KeyPadDot = 0x63,
};

inline u8 KeyF(int n) { return (u8)(KeyF1 + n - 1); }
inline u8 KeyLetter(char c) { return (u8)(KeyA + (c - 'a')); }

enum : u8 {
	ModLCtrl = 0x01,
	ModLShift = 0x02,
	ModLAlt = 0x04,
	ModRCtrl = 0x10,
	ModRShift = 0x20,
	ModRAlt = 0x40,
	ModCtrl = ModLCtrl | ModRCtrl,
	ModShift = ModLShift | ModRShift,
	ModAlt = ModLAlt | ModRAlt,
};

struct KeyEvent
{
	u8 usage;
	u8 mods;
};
