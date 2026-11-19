#include "kernel.h"
#include "usbinput.h"
#include <circle/bcmpropertytags.h>
#include <circle/chainboot.h>
#include <circle/string.h>
#include <circle/sysconfig.h>
#include <circle/util.h>

#ifndef DIAG_LOG_LEVEL
#define DIAG_LOG_LEVEL LogNotice
#endif

static const unsigned W = Launcher::W, H = Launcher::H;

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel(void)
:	m_Timer(&m_Interrupt),
	m_Logger(DIAG_LOG_LEVEL, &m_Timer),
	m_pUSBHCI(0),
	m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
	m_bStorageOK(FALSE),
	m_pFrameBuffer(0),
	m_bDoubleBuffered(FALSE),
	m_bSwapRedBlue(FALSE),
	m_nBackBuffer(0),
	m_pCanvas(0),
	m_nStatus(0)
{
	s_pThis = this;
}

boolean CKernel::Initialize(void)
{
	if (!m_Logger.Initialize(&m_LogBuffer) || !m_Interrupt.Initialize() || !m_Timer.Initialize())
		return FALSE;
	m_Logger.RegisterPanicHandler(PanicHandler);

	m_pFrameBuffer = new CBcmFrameBuffer(W, H, 32, W, H * 2);
	if (m_pFrameBuffer->Initialize())
		m_bDoubleBuffered = TRUE;
	else {
		delete m_pFrameBuffer;
		m_pFrameBuffer = new CBcmFrameBuffer(W, H, 32);
		if (!m_pFrameBuffer->Initialize())
			return FALSE;
	}
	m_pCanvas = new u32[W * H];

	static const u32 kGetPixelOrder = 0x00040006;
	CBcmPropertyTags Tags;
	TPropertyTagSimple PixelOrder;
	if (Tags.GetTag(kGetPixelOrder, &PixelOrder, sizeof PixelOrder))
		m_bSwapRedBlue = PixelOrder.nValue == 1;
	AddStatus(m_bDoubleBuffered ? "Screen: 1280x720, double-buffered" : "Screen: 1280x720");

	// The card first: the menu can still autostart a program without a keyboard.
	if (m_EMMC.Initialize() && f_mount(&m_FileSystem, "SD:", 1) == FR_OK)
		m_bStorageOK = TRUE;
	AddStatus(m_bStorageOK ? "SD card: mounted" : "SD card: not available");

	m_pUSBHCI = new CUSBHCIDevice(&m_Interrupt, &m_Timer, TRUE);  // TRUE: plug-and-play
	if (!m_pUSBHCI->Initialize()) {
		AddStatus("USB host controller: FAILED to start");
		return FALSE;
	}
	AddStatus("USB host controller: started");
	return TRUE;
}

void CKernel::AddStatus(const char *pLine)
{
	if (m_nStatus < 6)
		strncpy(m_Status[m_nStatus++], pLine, sizeof m_Status[0] - 1);
}

int CKernel::Diagnostics(const char **lines, int max)
{
	int n = 0;
	for (int i = 0; i < m_nStatus && n < max; i++)
		lines[n++] = m_Status[i];
	if (n < max)
		lines[n++] = "--- log ---";
	return n + m_LogBuffer.GetLines(lines + n, max - n);
}

void CKernel::PanicHandler(void)
{
	CKernel *k = s_pThis;
	if (!k || !k->m_pCanvas || !k->m_pFrameBuffer)
		return;
	Canvas c(k->m_pCanvas, W, H);
	c.Clear(0xFF1E2336);
	c.Text("The menu stopped because of an error. Please take a photo of this screen.",
	       font::Small, 40, 50, 0xFFFF8080);
	static const char *lines[48];
	int n = k->Diagnostics(lines, 48);
	int first = n > 25 ? n - 25 : 0;
	for (int i = first; i < n; i++)
		c.Text(lines[i], font::Small, 40, 90 + (i - first) * 25, 0xFFE8EAF2);
	k->m_bDoubleBuffered = FALSE;
	k->m_pFrameBuffer->SetVirtualOffset(0, 0);
	k->m_nBackBuffer = 0;
	k->Present();
}

u32 CKernel::Millis() { return (u32)(CTimer::GetClockTicks64() / 1000); }

bool CKernel::KeyboardPresent() { return usbinput::KeyboardPresent(); }

bool CKernel::StorageAvailable() { return m_bStorageOK; }

bool CKernel::RemoveFile(const char *name)
{
	if (!m_bStorageOK)
		return false;
	CString path;
	path.Format("SD:/%s", name);
	return f_unlink(path) == FR_OK;
}

bool CKernel::FileSize(const char *name, u32 *size)
{
	if (!m_bStorageOK)
		return false;
	CString path;
	path.Format("SD:/%s", name);
	FILINFO Info;
	if (f_stat(path, &Info) != FR_OK || (Info.fattrib & AM_DIR))
		return false;
	*size = (u32)Info.fsize;
	return true;
}

bool CKernel::Launch(const char *name, char *err, int cap)
{
	u32 size = 0;
	if (!FileSize(name, &size) || size == 0) {
		StrCopy(err, "The program file is missing.", cap);
		return false;
	}
	// The image is copied to 0x80000 from a buffer above this kernel's own
	// area, so it must not be larger than that area.
	if (size > KERNEL_MAX_SIZE) {
		StrCopy(err, "The program file is too big.", cap);
		return false;
	}
	u8 *pImage = new u8[size];
	CString path;
	path.Format("SD:/%s", name);
	FIL File;
	UINT nRead = 0;
	bool bOK = f_open(&File, path, FA_READ | FA_OPEN_EXISTING) == FR_OK;
	if (bOK) {
		bOK = f_read(&File, pImage, size, &nRead) == FR_OK && nRead == size;
		f_close(&File);
	}
	if (!bOK) {
		delete[] pImage;
		StrCopy(err, "The program file could not be read.", cap);
		return false;
	}
	// Circle's 64-bit kernels start with "mrs x0, CurrentEL"; other AArch64
	// kernels usually with a branch. Anything else is not a program image.
	u32 nFirst = pImage[0] | pImage[1] << 8 | pImage[2] << 16 | (u32)pImage[3] << 24;
	if (nFirst != 0xD5384240 && (nFirst & 0xFC000000) != 0x14000000) {
		delete[] pImage;
		StrCopy(err, "This is not a Raspberry Pi program file.", cap);
		return false;
	}
	m_Logger.Write("launcher", LogNotice, "Starting %s (%u bytes)", name, size);
	EnableChainBoot(pImage, size);  // done when main() returns EXIT_REBOOT
	return true;
}

// Saving is crash-safe: write "<name>.new", keep the previous file as
// "<name>.bak", then rename. Loading falls back through the same names.
bool CKernel::LoadFile(const char *name, char *buf, int cap, int *len)
{
	if (!m_bStorageOK)
		return false;
	static const char *const suffix[] = {"", ".new", ".bak"};
	for (unsigned i = 0; i < 3; i++) {
		CString path;
		path.Format("SD:/%s%s", name, suffix[i]);
		FIL File;
		if (f_open(&File, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
			continue;
		UINT nRead = 0;
		FRESULT res = f_read(&File, buf, (UINT)cap, &nRead);
		f_close(&File);
		if (res == FR_OK && nRead > 0) {
			*len = (int)nRead;
			return true;
		}
	}
	return false;
}

bool CKernel::SaveFile(const char *name, const char *buf, int len)
{
	if (!m_bStorageOK)
		return false;
	CString path, tmp, bak;
	path.Format("SD:/%s", name);
	tmp.Format("SD:/%s.new", name);
	bak.Format("SD:/%s.bak", name);
	FIL File;
	bool bOK = f_open(&File, tmp, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
	if (bOK) {
		UINT nWritten = 0;
		FRESULT res = f_write(&File, buf, (UINT)len, &nWritten);
		bOK = f_close(&File) == FR_OK && res == FR_OK && nWritten == (UINT)len;
	}
	if (bOK) {
		f_unlink(bak);
		f_rename(path, bak);  // fails harmlessly on the first save
		bOK = f_rename(tmp, path) == FR_OK;
	}
	if (!bOK)
		m_bStorageOK = FALSE;
	return bOK;
}

void CKernel::Present(void)
{
	u8 *pBase = (u8 *)(uintptr)m_pFrameBuffer->GetBuffer();
	unsigned nPitch = m_pFrameBuffer->GetPitch();
	unsigned nOffsetY = m_bDoubleBuffered && m_nBackBuffer ? Launcher::H : 0;
	for (unsigned y = 0; y < (unsigned)Launcher::H; y++) {
		u32 *pDest = (u32 *)(pBase + (nOffsetY + y) * nPitch);
		const u32 *pSrc = m_pCanvas + y * Launcher::W;
		if (!m_bSwapRedBlue) {
			memcpy(pDest, pSrc, Launcher::W * 4);
			continue;
		}
		for (unsigned x = 0; x < (unsigned)Launcher::W; x++) {
			u32 c = pSrc[x];
			pDest[x] = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
		}
	}
	if (m_bDoubleBuffered) {
		m_pFrameBuffer->SetVirtualOffset(0, nOffsetY);
		m_pFrameBuffer->WaitForVerticalSync();
		m_nBackBuffer ^= 1;
	}
}

void CKernel::Run(void)
{
	static Launcher app(this);
	app.Init();
	Canvas canvas(m_pCanvas, W, H);

	while (!app.Done()) {
		usbinput::Update(m_pUSBHCI);
		unsigned char ucUsage, ucMods;
		while (usbinput::Poll(&ucUsage, &ucMods))
			app.OnKey(KeyEvent{ucUsage, ucMods});
		app.Update();
		if (app.Draw(canvas))
			Present();
		else
			m_Timer.MsDelay(5);
	}

	// Hand over a quiet machine: stop the USB host controller, which would
	// otherwise keep writing into memory that the next program now owns.
	// The "Starting..." screen stays up until the program draws its own.
	delete m_pUSBHCI;
	m_pUSBHCI = 0;

	// Stop the ARM generic timer. Circle's CTimer::Initialize() routes the
	// timer interrupt to the CPU before it sets the tick length; if our timer
	// is still running, its pending interrupt hits the next program first and
	// is re-armed with a step of 0, so it never stops firing (seen in QEMU:
	// the program hung in CTimer::Initialize). Nothing needs the timer from
	// here until the next program starts its own.
	asm volatile ("msr CNTP_CTL_EL0, %0" :: "r" (0UL));
}
