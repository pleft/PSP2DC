/*
 * PSP2DC boot: loads PSPExp's kernel USB driver (pspexp_usb.prx) and the
 * user-mode application (psp2dc_app.prx), both next to the EBOOT, then gets
 * out of the way. The same shape as PSPExp's and PSPLink's bootstrap: a user
 * module whose main thread has attribute 0, which custom firmware runs in
 * kernel mode. That is why custom firmware is required.
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PSP2DCBoot", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(0);

#define USB_MODULE "pspexp_usb.prx"
#define APP_MODULE "psp2dc_app.prx"

static int load_start(const char *dir, const char *name, SceSize args, void *argp)
{
    char path[512];
    SceUID mod;
    int status = 0;
    snprintf(path, sizeof(path), "%s%s", dir, name);
    mod = sceKernelLoadModule(path, 0, NULL);
    if (mod < 0) { pspDebugScreenPrintf("load %s: 0x%08X\n", name, mod); return -1; }
    mod = sceKernelStartModule(mod, args, argp, &status, NULL);
    if (mod < 0) { pspDebugScreenPrintf("start %s: 0x%08X\n", name, mod); return -1; }
    return 0;
}

static int boot_main(SceSize args, void *argp)
{
    char dir[384], *slash;

    pspDebugScreenInit();
    pspDebugScreenPrintf("PSP2DC boot\n");
    snprintf(dir, sizeof(dir), "%s", (const char *)argp);
    slash = strrchr(dir, '/');
    if (!slash) { pspDebugScreenPrintf("bad path %s\n", dir); goto fail; }
    slash[1] = 0;

    if (load_start(dir, USB_MODULE, 0, NULL) < 0) {
        pspDebugScreenPrintf("\nThe USB driver did not load. PSP2DC needs custom firmware\n"
                             "and %s next to its EBOOT.\n", USB_MODULE);
        goto fail;
    }
    if (load_start(dir, APP_MODULE, args, argp) < 0) goto fail;
    sceKernelSelfStopUnloadModule(1, 0, NULL);
    return 0;
fail:
    sceKernelDelayThread(6000000);
    sceKernelExitGame();
    return 0;
}

int module_start(SceSize args, void *argp)
{
    SceUID thid = sceKernelCreateThread("PSP2DCBoot", boot_main, 32, 0x10000, 0, NULL);
    if (thid < 0) return 1;
    sceKernelStartThread(thid, args, argp);
    return 0;
}

void _exit(int status) { sceKernelExitGame(); for (;;) {} }
