# Release notes

## 1.0.0 (not released yet)

The first release. PSP2DC makes a PSP-1000 a Dreamcast controller, VMU, memory card and keyboard, through a small adapter (an RP2040-Zero) between the PSP's USB port and a Dreamcast controller port. The console and its games are as they are.

[A video of it](https://youtu.be/YzZmnof1pwY) on a real Dreamcast: Sonic Adventure and Crazy Taxi 2.

**What works, on a real Dreamcast:**

- **The controller:** d-pad, the four buttons, START, the analog nub as the stick, L and R as the triggers.
- **The VMU's screen** on the PSP, live, in six themes.
- **Memory cards** kept as files on the PSP's Memory Stick: saves survive switching the console off; cards made, renamed, backed up and changed while connected; single saves imported, exported, copied and deleted.
- **A card per game** with GDEMU and openMenu: the game's own card is put in as it starts.
- **The Dreamcast keyboard**, typed on the PSP (openMenu, Typing of the Dead).
- **The VMU by itself:** with no Dreamcast, the PSP runs the card's mini-games (Chao Adventure 2 and others).
- **For homebrew developers, the companion:** a KallistiOS library, `libpsp2dc`, with which a Dreamcast program uses the PSP as a second screen (widgets, drawing, pictures), reads the PSP's buttons and keeps files on its Memory Stick. A guide, a tutorial in seven steps and a demo game come with it (`dreamcast/tutorial/README.md`).

**What you need:** a PSP-1000 with custom firmware (made and tried on 6.60 PRO-C), a Waveshare RP2040-Zero with a Dreamcast controller cable soldered to it (the README shows how), and a mini-USB OTG cable or adapter between the two.

**Known limits:**

- **Tried on one set of hardware:** one PSP-1000, one Dreamcast (with GDEMU), one adapter. Other PSP models are not tried.
- **Few games tried so far.** Reports of what works and what does not are welcome.
- **L and R are all or nothing:** the PSP has no analog triggers, so a game that wants a trigger half pressed cannot have it.
- **Latency is worked out, not measured:** about 10 ms added on average to what an original controller has (`docs/psp2dc.md`, 5.1a).
- **The adapter and the PSP application must be of the same release.** The PSP says "update adapter firmware" when they are not.
- **The companion's library is not yet in kos-ports;** a port for it is in `dreamcast/kos-ports`.
