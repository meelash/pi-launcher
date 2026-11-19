#include "kernel.h"
#include <circle/startup.h>

int main(void)
{
	// Objects in CKernel are never destroyed (static), except the USB host
	// controller, which Run() shuts down before handing over.
	static CKernel Kernel;
	if (!Kernel.Initialize()) {
		halt();
		return EXIT_HALT;
	}
	Kernel.Run();
	// A program image was loaded (EnableChainBoot): returning EXIT_REBOOT
	// makes Circle copy it to 0x80000 and jump to it instead of rebooting.
	return EXIT_REBOOT;
}
