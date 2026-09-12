# DeusExHRVR

Experimental VR mod for **Deus Ex: Human Revolution — Director's Cut**, using the game's native AMD HD3D stereo renderer. **Both eyes are rendered in the same game frame. No AER.**

The initial gameplay build has been tested in a headset with stereo depth, headset-driven camera movement, OpenXR projection views, and corrected HUD alignment. This is an early prerelease; broader testing of missions, menus, cutscenes, culling, aiming, and effects remains outstanding. Motion-controller input is not implemented.

## Download and install

Download the packaged build from [Releases](https://github.com/farmerarmor/DeusExHRVR/releases). GitHub's automatic source ZIP does not contain compiled DLLs.

Supported game: **Steam Director's Cut 2.0.66.0**, executable SHA256:

```text
8266B6B4A5BF25F2F4E8DE068AA3720F6289C962BB1C2BB70A7B1C111BA510A1
```

Other executable versions and the original non-Director's Cut release are not supported by this build. Requires Windows, DirectX 11, a PC-connected headset, and an active OpenXR runtime. The initial test used the Oculus runtime. Installation does not change the system runtime.

1. Extract the release archive.
2. Close Deus Ex and connect the headset.
3. Open PowerShell in the extracted `DeusExHRVR` folder and run the following, replacing the example with your installation path:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1 -GameDirectory "E:\SteamLibrary\steamapps\common\Deus Ex Human Revolution Director's Cut"
   ```

4. Launch `DXHRDC.exe`, load a save, face forward, and press **F6**.

Run the installer as the Windows user who plays the game. It backs up replaced files and original graphics settings to `DeusExHRVR-backup` in the game folder. It enables DX11/native stereo and disables VSync and antialiasing for the tested configuration.

Keep the companion in the game's `DeusExHRVR/DeusExHRVRHost.exe` subfolder so it cannot load the game's 32-bit proxy DLLs.

## Headset resolution and refresh rate

Connect the headset before launching. The mod queries the active OpenXR runtime at startup and renders each eye at its recommended resolution, including the runtime's current resolution setting. It does not upscale the old desktop-sized image. Restart the game after changing the headset's resolution setting. If the startup query fails, the mod logs the failure and retains the game's original resolution for that run.

Complete native stereo pairs are paced by OpenXR frame requests. The desktop mirror uses a windowed, nonblocking presentation path so desktop VSync and a 60 Hz monitor do not throttle VR. The headset's selected refresh rate is retained; the mod does not switch it to the highest available rate. Runtime reprojection and GPU/CPU limits can still lower the rate of newly rendered frames.

The initial live check confirmed 1344 × 1600 per eye and approximately 90 native pairs/submissions per second on a headset set to 90 Hz. Gameplay and HUD appearance were confirmed in-headset. This is one tested configuration, not a guarantee of that performance at higher resolutions.

## Controls

| Key | Action |
|---|---|
| F6 | Toggle tracked VR and capture a neutral head pose |
| F7 | Toggle the shared lighting-depth correction (on by default) |
| F9 | Recenter |
| F8 | Save both-eye images, camera trace, and any armed rolling recording locally |
| F10 | Toggle the rolling stereo-frame recorder (off by default) |

The game starts on a stereo screen in VR; F6 enables the tracked view. Gameplay input remains the game's keyboard/mouse or gamepad input.

For an intermittent visual problem, press F10 before waiting for it, then F8 immediately after it appears. The recorder retains 360 reduced-size stereo pairs (about four seconds at 90 Hz), together with their tracking and submission data. It uses about 106 MB while armed; saving with F8 can briefly pause playback. Images and camera-history CSV files stay in the local `DeusExHRVR-captures` folder. F10 turns recording off again.

The HUD uses a shared plane projected through the recorded eye poses. Alignment of the health bar, minimap, and item bar has been confirmed in-headset.

Shared lighting-depth reconstruction uses the same eye frusta as world geometry. This fixes the tested hanging yellow lights; other light and shadow effects still have known stereo issues. F7 provides a live comparison while those remaining effects are investigated.

World scale is provisional. Copy `DeusExHRVR.ini.example` to `DeusExHRVR.ini` beside `DXHRDC.exe` to adjust it. Keep the `[VR]` section header; a setting outside that section is ignored. Restart the game after changing it and enable tracked VR with F6:

```ini
[VR]
WorldUnitsPerMetre=100
```

The accepted range is 10–1000. Higher values make the world appear smaller and increase close-range stereo depth; lower values make it appear larger and reduce depth. Physical head translation uses the same scale. To confirm that your edit was read, check `unitsPerMetre=` in the latest `Camera hooks` line in `DeusExHRVR-camera.log`.

The game's stereo separation/convergence sliders do not calibrate tracked VR: that path uses the headset eye poses and `WorldUnitsPerMetre`. The original stereo settings remain relevant to the untracked screen mode.

## Restore the original game

Close the game and run `uninstall.ps1` with the same game path:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 -GameDirectory "E:\SteamLibrary\steamapps\common\Deus Ex Human Revolution Director's Cut"
```

The backup restores original files and graphics settings. Logs, captures, and the backup remain local.

## How it works

The 32-bit game renders a double-height native texture: two complete headset-sized eyes stacked vertically. A D3D11 keyed mutex transfers the entire GPU pair to a 64-bit OpenXR companion on the same graphics adapter, which copies it into a two-slice OpenXR swapchain.

Tracking is attached to the engine scene and carried with the completed native pair. Projection submission requires both eyes to use the same recorded tracking sample. The companion signals the game after `xrWaitFrame`; bounded waits pace the producer and drop a whole pair on timeout. Live transport uses GPU copies; CPU readback is limited to F8 diagnostics. The original render pose remains attached to the image; further prediction and latency optimization remains future work.

## Build from source

Requires Visual Studio 2022 C++ tools, a Windows SDK, CMake 3.24+, and Git. CMake fetches pinned OpenXR and MinHook sources. Use separate developer shells and build directories.

In an **x86** Visual Studio developer shell:

```powershell
cmake -S . -B build-x86 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86 --target atidxx32 d3d11_proxy atiadlxy DeusExHRVRCameraMathProbe
```

In an **x64** Visual Studio developer shell:

```powershell
cmake -S . -B build-x64 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 --target DeusExHRVRHost DeusExHRVRProbe
```

Stage `d3d11.dll`, `atidxx32.dll`, and `atiadlxy.dll` from `build-x86/bin` into `dist`. Stage the x64 host from `build-x64/bin` into `dist/DeusExHRVR`. The installer requires this layout.

Offline builds can set `FETCHCONTENT_SOURCE_DIR_OPENXR` and `FETCHCONTENT_SOURCE_DIR_MINHOOK` to matching dependency checkouts; exact revisions are in CMakeLists.txt.

Validation tools:

- `DeusExHRVRCameraMathProbe`: camera-cache lifetime, tracking mailbox contention, pose conversion, inverse matrices, asymmetric eye frusta, and binocular HUD alignment. Pass a camera-history CSV path to replay scene creation/drawing through the cache.
- `DeusExHRVRProbe`: D3D11 pair bounds and GPU eye-array copies. Optional `--xr` renders red/green diagnostics; use x64 with the tested runtime.
- `DeusExHRVRTransportProbe`: optional x86 target exercising the complete GPU transport. Place the x64 companion in its `DeusExHRVR` subfolder.

Component probes do not replace headset gameplay testing. Logs and F8 images are written to the game folder. Review them before sharing; they are excluded from this repository and release packages.

## Credits and license

The stereo and adapter proxies derive from [effcol/wiz3D](https://github.com/effcol/wiz3D). Thanks also to the [cdcEngineDXHR](https://github.com/rrika/cdcEngineDXHR) reverse-engineering reference, Khronos OpenXR, and MinHook.

Distributed under LGPL 2.1. See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and `licenses/`. No game executable or game content is included.
