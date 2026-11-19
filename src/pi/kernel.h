// Bare-metal kernel of the boot menu: brings up only the hardware the menu
// needs (HDMI framebuffer, USB keyboard, SD card), runs the menu, then
// loads the chosen program and chain-boots into it.
// There is no operating system, no network stack and no wireless driver.
#pragma once
#include "../core/launcher.h"
#include "logbuffer.h"
#include <circle/actled.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/koptions.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <fatfs/ff.h>

class CKernel : public Platform
{
public:
	CKernel(void);

	boolean Initialize(void);
	// Returns when a program image is ready to start.
	void Run(void);

	// Platform
	u32 Millis() override;
	bool LoadFile(const char *name, char *buf, int cap, int *len) override;
	bool SaveFile(const char *name, const char *buf, int len) override;
	bool RemoveFile(const char *name) override;
	bool FileSize(const char *name, u32 *size) override;
	bool KeyboardPresent() override;
	bool StorageAvailable() override;
	int Diagnostics(const char **lines, int max) override;
	bool Launch(const char *name, char *err, int cap) override;

private:
	void Present(void);
	void AddStatus(const char *pLine);
	static void PanicHandler(void);

	// do not change this order
	CActLED m_ActLED;
	CKernelOptions m_Options;
	CDeviceNameService m_DeviceNameService;
	CLogBuffer m_LogBuffer;
	CExceptionHandler m_ExceptionHandler;
	CInterruptSystem m_Interrupt;
	CTimer m_Timer;
	CLogger m_Logger;
	CUSBHCIDevice *m_pUSBHCI;  // deleted before chain boot (stops USB DMA)
	CEMMCDevice m_EMMC;
	FATFS m_FileSystem;
	boolean m_bStorageOK;

	CBcmFrameBuffer *m_pFrameBuffer;
	boolean m_bDoubleBuffered;
	boolean m_bSwapRedBlue;
	unsigned m_nBackBuffer;
	u32 *m_pCanvas;

	char m_Status[6][80];
	int m_nStatus;

	static CKernel *s_pThis;
};
