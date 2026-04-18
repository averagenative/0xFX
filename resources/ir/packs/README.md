# Cabinet IR Packs

Mirrored, redistributable cabinet impulse response packs. Users can unzip any
pack and bulk-import its contents from the cab detail view in 0xFX
(`Import IR... → Folder (bulk, recursive)...`).

## 650-assorted-cabinet-impulses.zip

**Source:** https://musical-artifacts.com/artifacts/252
**License:** Public domain (released by zoyd)
**Size:** ~12 MB (~30 MB uncompressed, 651 WAV files)

A community-assembled pack covering a wide range of cabinets, mics, and
mic positions. Filenames reference real-world gear (Ampeg, Marshall,
Fender, etc.) as originally named by the pack's contributors — 0xFX only
redistributes them as-is; our own code, API, and UI use non-trademarked
naming throughout.

### How to use

1. Unzip anywhere on disk.
2. In 0xFX, click on a Cabinet node.
3. Click **Import IR...** → **Folder (bulk, recursive)...**
4. Point at the unzipped folder.
5. Every `.wav` is added to your custom cab library (up to 512 entries).
   Scroll-wheel the Cab Type dropdown to cycle through them, or open the
   dropdown and pick by name.

All IRs are validated on load — sample rate must be 44.1/48 kHz, duration
under 2 seconds. Anything that fails validation is rejected with a toast
message.
