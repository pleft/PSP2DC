# PSP2DC: Optimisations for Later

Things that would make PSP2DC faster or lighter, but that nothing needs now. Each says what it would gain, what it would cost, and what is known. They are added as they are found and done only when chosen.

| # | What | Gain | Cost | Known |
|---|---|---|---|---|
| 1 | **Pictures kept on the Memory Stick** (companion, phase 3's rest; [companion E.8](psp2dc-companion.md)) | A game's sprites cross the cable once ever, not at every start or replug: 300 KB of pictures is about 5 s each time now | The library asks first by the picture's checksum; the PSP saves what it was sent and loads what it has. PSP application, library, one more record each way | Designed, not built. The risk is a wrong picture from a stale or damaged file: each file carries the key and a checksum of its own, and a picture is copied whole before it is saved |
| 2 | **Files written faster** (companion, phase 4) | Writes measured 38 KB a second on a Dreamcast against 54 for reads and 55 in the simulator | Unknown until measured | Cause not known. One guess: the Memory Stick taking the bytes 990 at a time (one `sceIoWrite` per piece); a buffer of several pieces would test it |
| 3 | **More than one companion command a vblank** | The Dreamcast to PSP path is one command a vblank today, about 60 KB a second at most; two would double it | Library only, if KallistiOS sends a second frame of the library's own in the same vblank | Possible in KallistiOS's queue by its code, not tried on hardware ([companion A.2](psp2dc-companion.md)) |
| 4 | **A larger read window** (companion, phase 4) | Reads are at 54 KB a second with the 3 KB window, near the 60 KB ceiling of one command a vblank; more only with 3 | The adapter's 4 KB queue towards the Dreamcast made larger, so the window can grow: firmware | Worth it only after 3 |
