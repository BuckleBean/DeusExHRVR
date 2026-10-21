# DeusExHRVR

Experimental VR mod for **Deus Ex: Human Revolution — Director's Cut**, using the game's native AMD HD3D stereo renderer. **Both eyes are rendered in the same game frame. No AER.**

Gameplay stereo, headset tracking, HUD alignment, motion-controlled weapon aiming, controller buttons, and selectable interaction/walking directions have been tested in-headset. Automatic widescreen menus and several lighting, shadow and sky corrections are included. This remains an experimental prerelease; weapon visibility at extreme viewing angles and untested missions/effects still need broader testing.

Version 0.5.0 adds an **in-game VR settings panel**, opened from a VR Settings button beside Options on the title and pause screens — no controller chord to learn. From it you can set the turn style and snap angle, choose whether walking follows your head or either hand, and remap the controller buttons, all without editing a file or restarting. **`MovementDirection=LeftController` walks where your left hand points**, which leaves your weapon hand free to aim. The panel also brings mouse-style pointing to the game's own menus. Version 0.4.0 drew Jensen's hands at your controllers, with no calibration required, and added three options that take the VR view from your aiming input rather than the game's animated camera — the motion that made this game hard to play in VR — plus Carnuntum's fix for guns vanishing when equipped during a level load, the HRDCfix stutter fix, and an optional fixed conversation camera, shipped off. Version 0.3.0 added per-weapon grip calibration, the SteamVR OpenXR sRGB compatibility fix, controller menu chords, and the immersive sniper scope, now on by default. The imported Luma lighting/color work remains experimental and disabled by default; its compiled shaders are included. XeGTAO is disabled following headset flicker and performance regressions. Roomscale body movement is not implemented.

## Download and install

Download the packaged build from [Releases](https://github.com/BuckleBean/DeusExHRVR/releases). GitHub's automatic source ZIP does not contain compiled DLLs.

This is a fork of [farmerarmor/DeusExHRVR](https://github.com/farmerarmor/DeusExHRVR), continuing from upstream v0.3.0.

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

4. Launch `DXHRDC.exe` and load a save. Gameplay enters full VR automatically. Press **F9** to recenter if needed.

On a first install the installer also writes `DeusExHRVR.ini` beside `DXHRDC.exe`, containing the configuration the mod is tested with: controller hands, the motion-sickness fixes, the comfort options, snap turn, and motion-controlled weapons. **If you already have a `DeusExHRVR.ini`, the installer leaves it alone** — upgrading never changes your settings, and never reads them.

**Snap turn is on, and it is a matter of taste.** It replaces the right stick's smooth, accelerating camera turn with an instant step, because smooth turning makes many people sick in VR — but plenty of people dislike snapping and are fine with smooth. To go back to the game's original turning, open `DeusExHRVR.ini`, set `SnapTurn=0`, and restart. Nothing else depends on it. To keep snapping but change the step, set `SnapTurnDegrees` (30 by default, 45 is a common alternative).

To change anything else, edit `DeusExHRVR.ini` and restart the game. `DeusExHRVR.ini.example` lists every option the mod has with its default and what it does; `DeusExHRVR.ini.comfort` is a copy of the configuration the installer writes. Deleting a line returns that option to its default, which for most of them means off. `InteractionAim` accepts `Mouse`, `Headset` or `Controller`, and `MovementDirection` accepts those plus `LeftController`, which walks where your left hand points and is the one most VR players expect — `Controller` uses the right hand, which is also your weapon hand, so aiming steers you. Most of these can now be set from the in-game VR settings panel instead; `WorldUnitsPerMetre` is world scale and the tested value is 300. Upgrading replaces both the game DLL and its companion together.

Run the installer as the Windows user who plays the game. It backs up replaced files and original graphics settings to `DeusExHRVR-backup` in the game folder. It enables DX11/native stereo and disables VSync and antialiasing for the tested configuration.

Keep the companion in the game's `DeusExHRVR/DeusExHRVRHost.exe` subfolder so it cannot load the game's 32-bit proxy DLLs.

## Headset resolution and refresh rate

Connect the headset before launching. The mod queries the active OpenXR runtime at startup and renders each eye at its recommended resolution, including the runtime's current resolution setting. It does not upscale the old desktop-sized image. Restart the game after changing the headset's resolution setting. If the startup query fails, the mod logs the failure and retains the game's original resolution for that run.

Complete native stereo pairs are paced by OpenXR frame requests. The desktop mirror uses a windowed, nonblocking presentation path so desktop VSync and a 60 Hz monitor do not throttle VR. The headset's selected refresh rate is retained; the mod does not switch it to the highest available rate. Runtime reprojection and GPU/CPU limits can still lower the rate of newly rendered frames.

The initial live check confirmed 1344 × 1600 per eye and approximately 90 native pairs/submissions per second on a headset set to 90 Hz. Gameplay and HUD appearance were confirmed in-headset. This is one tested configuration, not a guarantee of that performance at higher resolutions.

## Controls

### Motion controllers

These are the buttons you will use to play. They emulate Xbox controller 1 when `MotionControls=1`, with no virtual-controller driver required; the layout is the game's own default Xbox mapping with Y and B exchanged. Keyboard, mouse and a physical gamepad all keep working alongside them.

| Motion controller | Xbox input / default game action |
| --- | --- |
| Left stick / click | Move / crouch |
| Right stick / quick click | Camera turn / iron sight or scope |
| Left trigger | LT / take cover |
| Right trigger | RT / fire |
| Left grip | LB / sprint |
| Right grip | RB / throw grenade |
| Left X | X / interact or reload |
| Left Y | B / non-lethal takedown; hold for lethal takedown |
| Right A | A / jump |
| Right B | Y / holster or draw; hold for quick inventory |
| Hold right-stick click + left stick | D-pad: up cloaking, down smart vision, left move silently, right Typhoon |
| Left menu: release before 1.5 seconds | Back / in-game menu |
| Hold right stick click + left X | Back / in-game menu |
| Hold right stick click + left Y | Start / pause menu |
| Left menu: hold at least 1.5 seconds | Start / pause menu, once per hold |

Any of these can be remapped — see [Remapping buttons](#remapping-buttons).

### Keyboard

| Key | Action |
|---|---|
| F3 | Toggle per-eye projected light/shadow transforms (on by default) |
| F4 | Toggle per-eye shader camera inputs (on by default; requires F7) |
| F6 | Toggle tracked VR and capture a neutral head pose |
| F7 | Toggle the shared lighting-depth correction (on by default) |
| F9 | Recenter |
| F8 | Save both-eye images, camera trace, and any armed rolling recording locally |
| F10 | Toggle the rolling stereo-frame recorder (off by default) |

Full VR is enabled by default and starts automatically when gameplay loads. F6 manually toggles between full VR and the virtual screen. Gameplay accepts keyboard/mouse, physical gamepad, or the motion controllers through the game's Xbox input path.

Two independent `[VR]` settings choose where interaction targeting and walking point:

```ini
InteractionAim=Headset
MovementDirection=Headset
```

Each accepts `Mouse`, `Headset`, or `Controller` (the right controller's aim). Omitted settings default to `Mouse`, retaining native mouse/gamepad look direction. Interaction aim selects doors and items independently of the gun. Walking uses horizontal heading only; looking up/down does not tilt movement. Controller direction works with the gun holstered and does not require the weapon experiment. Menus, virtual-screen modes and unavailable tracking retain native direction. Restart after changing settings. These direction options are experimental and need in-game validation.

Terminal interaction, hacking, the main/pause/game-over menus, sniper scope aiming, and prerecorded video playback automatically use the 16:9 virtual screen. Full VR resumes afterward unless you disabled it with F6. The screen appears in front of your current head position; VR rendering retains the headset resolution and refresh rate.

Set `LockVerticalCamera=1` under `[VR]` in `DeusExHRVR.ini` to keep mouse/gamepad aiming pitch out of the full-VR camera. The gun still aims vertically, while the VR camera uses a level base plus your headset pitch. Horizontal look stays available. Virtual-screen modes, including scoped aiming, retain the native camera. The option defaults to `0` (off); restart the game after changing it.

Set `MotionControls=0` under `[VR]` to disable all motion-controller features, including controller tracking, the weapon experiment, and controller-based interaction/walking. Headset VR and `Headset` direction settings stay available; `Controller` directions fall back to native mouse/gamepad direction. `MotionControls=1` enables controller features selected by the other settings and is the default for compatibility. Restart after changing it.

`LeftStickClickDelayMs` under `[VR]` sets how long the left stick click must be held before it registers, so that a stray tap does nothing. The installed configuration sets it to `0`, which sends the click immediately; `150` is the original behaviour and is what you want if the left click is mapped to crouch, since it lets a two-click chord arrive together.

Install the matching game DLL and host together: this build uses **transport version 12**, which is the version that carries the left controller's aim pose and the settings-menu channel. A mismatched pair refuses to connect rather than misreading each other, so you get no VR at all instead of a corrupted one — the installer always replaces both files together, so a normal install or upgrade cannot produce a mismatch.

### Adjusting a weapon's grip by hand

**Only relevant with `ControllerHands=0`.** The installed configuration enables the hands, which take the gun's position from the game's own hand-to-weapon relationship and need no measured offset; `WeaponCalibration` is set to `0` to match. This section is here for setups not using the hands.

**With the hands enabled, `WeaponCalibration=1` has no effect.** The weapon's own grip, read from the game's animation, replaces a stored offset rather than adding to it, so the gesture below will appear to do nothing and a saved offset will not move the gun. If you want to position weapons by hand, set `ControllerHands=0` as well.

Use full VR with motion-controlled weapons enabled and keep the weapon out of scope mode. Press both thumbsticks and hold for one second, until the gun stops following your hand (a system tone also signals this). Keep holding and move/rotate your right controller into the grip position you want. Release either thumbstick to save, then release the other. Each weapon type keeps its own position and rotation offset in `DeusExHRVR-weapons.ini` beside the game executable, including across restarts. The gun, firing line, muzzle effects and immersive scope share that offset. Repeat to readjust; delete that file while the game is closed to reset all weapons.

The gesture consumes controller movement, turning, firing and button actions until both sticks are released. Tracking loss, menus, recentering or weapon changes cancel an unfinished calibration.

`ImmersiveScope=1` optionally keeps native scoped weapons in full VR. With motion-controlled weapons enabled, raise the right controller near your eye and point forward to enter the scope; lower it to exit. The magnified view follows controller aim, using the rifle's firing line as a shared optical viewpoint for both eyes. `ScopeMagnification=4` controls zoom (1-12). This alignment has been tested in-headset with the sniper rifle. The default remains the automatic virtual screen (`ImmersiveScope=0`). Restart after changing these settings.

`ExperimentalMotionControls=1` enables a right-controller weapon experiment when `MotionControls=1`. It moves the equipped gun's render pose and supplies the controller muzzle to the player's firing-direction calculation. Motion-controller buttons use the Xbox bindings below; keyboard/mouse and physical gamepad input also remain available. `ControllerHideArms=1` (default) hides the player's actor/arms mesh during controller aiming. It falls back to normal weapon handling in virtual-screen modes or when controller tracking is unavailable. `ControllerMuzzleForwardMetres` sets the muzzle distance ahead of the controller (default `0.25`). Restart after changing these options. This remains experimental; alignment, shot impacts and different weapon models need in-game verification.

The motion-controller button layout is in [Controls](#motion-controllers) above. `MotionControls` provides the buttons; `ExperimentalMotionControls` controls the weapon pose separately.

While right-stick click is held, the left stick sends only D-pad input. Diagonals choose the dominant direction. A quick right-stick click under 350ms still toggles scope on release if no D-pad direction was used; a longer hold sends no scope click. Menu short presses are delayed until release so a long press opens only pause. Tracking/focus loss releases the emulated inputs. `MotionControls=0` disables button emulation together with all other controller features; restart to apply. Native rumble is not yet mapped to VR haptics. F8 includes a private input diagnostic log.

For an intermittent visual problem, press F10 before waiting for it, then F8 immediately after it appears. The recorder retains 360 reduced-size stereo pairs (about four seconds at 90 Hz), together with their tracking and submission data. It uses about 106 MB while armed; saving with F8 can briefly pause playback. Images and camera-history CSV files stay in the local `DeusExHRVR-captures` folder. F10 turns recording off again.

The HUD uses a shared plane projected through the recorded eye poses. Alignment of the health bar, minimap, and item bar has been confirmed in-headset.

Shared lighting-depth reconstruction uses the same eye frusta as world geometry. This fixes the tested hanging yellow lights; other light and shadow effects still have known stereo issues. F7 provides a live comparison while those remaining effects are investigated.

World scale is provisional. Copy `DeusExHRVR.ini.example` to `DeusExHRVR.ini` beside `DXHRDC.exe` to adjust it. Keep the `[VR]` section header; a setting outside that section is ignored. Restart the game after changing it:

```ini
[VR]
WorldUnitsPerMetre=100
```

The accepted range is 10–1000. Higher values make the world appear smaller and increase close-range stereo depth; lower values make it appear larger and reduce depth. Physical head translation uses the same scale. To confirm that your edit was read, check `unitsPerMetre=` in the latest `Camera hooks` line in `DeusExHRVR-camera.log`.

The game's stereo separation/convergence sliders do not calibrate tracked VR: that path uses the headset eye poses and `WorldUnitsPerMetre`. The original stereo settings remain relevant to the untracked screen mode.

`LevelRecenter=1` under `[VR]` (default) keeps only the heading of the head pose captured when tracking starts and on F9. A head tilted slightly up or down at that moment no longer tilts the world for the rest of the session; position, including height, is still taken from the captured pose. Set it to `0` for the previous behavior. The effect is most visible with `LockVerticalCamera=1`, where the native look pitch no longer masks the tilt.

`LevelMenu=1` under `[VR]` (default) levels the in-game menu (map, objectives, inventory) and loading screens. The game stops updating its camera while these are up, so the view freezes at the head pose of that moment, and the screen, drawn in front of that pose, stayed tilted by however far the head was tilted. The frozen pose now keeps only its heading, the same as `LevelRecenter`: the screen comes out level, and the 3D view behind the in-game menu is kept. The in-game menu is leveled as it opens; any other frozen view is leveled once the camera has been stopped for 10 frames, so a brief stall in gameplay is left alone (in testing, every freeze outside the in-game menu lasted over 100 frames; loading screens show their first 10 frames tilted). The headset's recenter now also brings a frozen view in front of you, as it already did for the virtual screen. Set it to `0` for the previous leveling behavior.

`LevelScreen=1` under `[VR]` (default) places the virtual screen used by the title and pause menus, terminals and videos upright, 2 m straight ahead at head height, keeping only the heading of the head pose when it is placed. Set it to `0` to place it tilted with the head, as before.

`YawOnlyCamera=1` under `[VR]` takes the VR rendering base from the game camera's heading and position only, so the headset supplies all pitch and roll. It goes further than `LockVerticalCamera`, which removes look pitch but keeps the camera's own pitch and roll, including the walk animation's tilt. Aiming, the gun and virtual-screen modes are unaffected. Defaults to `0`; restart after changing it.

`NativeLookPitchLock=1`, `NativeLookHeading=1` and `NativeCameraPosition=1` under `[VR]` keep the game's own walk, sprint and equip animation out of the VR view. The game builds its camera from an animated skeleton, so that animation reaches the view as motion you did not make; when a save's own look angle is steep, the heading extracted from that camera becomes several times more sensitive to small rotations and the result is the whole scene swaying as you move. Rather than filtering the animated camera, these take the view from the aiming state upstream of it: the first zeroes the native aiming pitch through the game's own setter so the heading stays well conditioned, the second takes the VR heading from the aiming yaw, and the third takes the camera's horizontal position from the player's own position. Each defaults to `0`; restart after changing them. All three require `MotionControls` and `YawOnlyCamera`, apply only during normal gameplay with the player's own camera — menus, conversations, cover, ladders and scoped aiming keep the existing behavior — and disable themselves on a game build whose code they do not recognize. `NativeCameraPosition` additionally requires `NativeLookHeading`. `DeusExHRVR-camera.log` reports how many frames each supplied and how much motion was removed. Reported to remove the symptom completely on the tested setup; if you get motion sick in VR, `DeusExHRVR.ini.comfort` turns them on along with the rest of the comfort options.

`FixedDialogueCamera=1` under `[VR]` keeps the view at Jensen's eyes during conversations instead of following the game's shot cuts between the NPC and Jensen; turn your head to look around. The view is held from the conversation's first camera shot until it ends and the game camera is back at the held pose (at most 2 s later), and Jensen's own model is hidden meanwhile. Cover, takedowns and other scripted camera shots are unaffected. F9 also releases the hold should one ever stay stuck.

**It defaults to `0` because of a known bug, and is not recommended yet.** In some conversations the held view ends up at floor height, leaving you looking up from the ground for the whole scene. This has been reproduced and captured — the held pose was taken at the body's origin height rather than at eye height — but the reason the pose was wrong has not been found, and no fix has been attempted. Leaving it off means conversations use the game's own camera cuts, which cannot do this. Restart after changing it.

Beyond that bug, it has been tested in-headset only with ordinary NPC conversations; social boss conversations and briefings use the same camera path and have not been tested at all.

`StanceHold=1` under `[VR]` takes the VR camera's height from the player's own origin plus a held eye height, rather than following the game camera's vertical motion. It removes the walk animation's bounce and the stance-height steps described below.

```ini
StanceHold=1
StanceHoldTrigger=60    ; game units; gap that counts as a real stance change
```

Measured against the player entity over 9000 frames on the supported build: the camera's X and Y track the entity's origin closely, to 0.2 units in that sample, so the camera's vertical behaviour is what needs handling here. (Horizontal offsets do appear during some motion - see `SideSwayHoldMs` below and `NativeCameraPosition` above - but they are a separate problem from the stance height.) That height above the origin is a *stance* height, not a smooth signal - the game raises it about 15 units while crouch-walking and lowers it about 25 while sprinting, then steps back when you stop. Those steps are 3-5 cm of vertical head movement and read as a bounce; averaging can smooth such a step but can never cancel a sustained offset.

Holding the height instead makes gait offsets and walking bob invisible, while a real stance change - crouched and standing differ by about 300 units - is followed at the speed of the game's own crouch/stand transition (about 1000 units/s, measured) until the camera settles. `StanceHoldTrigger` separates the two: gait offsets are 15-25 units and walking bob is about 2. Defaults to `0`. Ladders, cover, vaulting and elevators have not been tested with it yet.

`SideSwayHoldMs` under `[VR]` does the same for sideways motion. While running, the game sways the camera side to side relative to the player's own position, about 1 cm peak to peak, once per step and once per stride, while the body itself travels straight (measured over two 46-second runs; standing, the camera sits exactly on the body, and running leans it about 3 cm forward). With `SideSwayHoldMs=667` - one running stride - the VR camera stays on the player's path at the camera's average offset over the last 667 ms, which cancels the sway without lag because that offset barely changes while moving; in the measured runs it removed about 85-90% of the stride sway and nearly all of the step sway. It also removes the camera's step-by-step forward-and-back surge relative to the body, since the body's own path is the smoother of the two. Offsets larger than 15 cm (camera cuts, cover and similar) pass through untouched, and the VR camera is never held more than 4 cm from the game's camera, so deliberate camera moves can't lag behind. Defaults to `0` (off).

`SleepFix=1` under `[VR]` applies the stutter fix from [HRDCfix](https://github.com/imring/HRDCfix) by imring (MIT). One of the game's wait loops calls `Sleep(1)` while it waits; the game never raises the Windows timer resolution, so each call can last a whole timer tick (about 15.6 ms, nearly two frames at 120 Hz). The fix changes it to `Sleep(0)`, which only yields. Applying it from the VR mod avoids HRDCfix's DX11 ASI loader, which installs as `d3d11.dll` and would replace the mod's own proxy. The loop's bytes are checked before patching. Defaults to `0` (off).

Optional heading-swing filtering smooths the walk animation's left-right swing of the camera heading out of the VR view, without touching the game's own camera. It needs `YawOnlyCamera=1`. Each setting is a time window in milliseconds and defaults to `0` (off):

```ini
HeadSwayYawMs=667       ; heading swing, walking
HeadSwayYawMs2=526      ; heading swing, second rhythm (sprinting)
```

The filter reports the time-average of the heading over the last window, extrapolated by half a window so steady turning is not delayed; a periodic swing whose period divides the window averages out. The correction is limited to `HeadSwayYawLimit` (1.5 degrees); a larger difference means the average no longer describes the camera, so the correction is dropped and averaging restarts. Snap turns and other heading jumps over 3 degrees in one frame pass straight through. Loads, teleports and pauses reset the filter. Only gameplay is filtered; other camera modes pass through.

`BobTrace=1` writes one line per frame to `DeusExHRVR-bob.csv` (camera position and heading, stick input, head pose; capped at 36000 lines) for measuring the walk animation on other hardware. The values above came from such a trace of walking and sprinting in the first hub. Tracing costs frame time - leave it at `0` for play.

`SnapTurn=1` under `[VR]` replaces the right stick's smooth camera turn during gameplay with a snap turn. A flick within 60 degrees of horizontal turns the view instantly by `SnapTurnDegrees` (default 30); the stick must return near center before the next flick. Menus, terminals and scoped aiming keep the native right stick.

The turn is one injected relative mouse move, which avoids the stick's acceleration ramp. `SnapTurnMouseCounts` is the size of that move; the mod measures the resulting heading change after each snap and writes a corrected value back to the INI, so it self-calibrates within a few turns from any starting value. The game appears to ignore mouse look while the gamepad left stick is deflected, so walking is released for up to `SnapTurnPauseMs` (default 60) around the injected move and resumed as soon as the camera has visibly turned.

The injected mouse move also brings up the game's keyboard-and-mouse quickbar (the PC item bar). The game's own quickbar auto-hide, the setting the tilde key toggles, keeps it hidden, so with `SnapTurn=1` the mod turns it on whenever gameplay starts. The setting is checked against the supported build before it is changed. `SnapTurnHideQuickBar=0` leaves it to you.

### Remapping buttons

To change what a motion-controller input sends:

1. Open `DeusExHRVR.ini` beside `DXHRDC.exe` (copy `DeusExHRVR.ini.example` there first if you don't have one).
2. Add a `[Buttons]` section for gameplay, a `[ScreenButtons]` section for everything else, or both.
3. Under it, add one line per input you want to change, in the form `Input=Target`, for example `LeftGrip=Y`.
4. Save the file and restart the game.

Motion-controller buttons can be remapped in `DeusExHRVR.ini` without touching the game's own bindings. `[Buttons]` applies during gameplay and scoped aiming; `[ScreenButtons]` applies to everything else: the title and pause menus, the in-game menu (map, objectives, inventory), the e-reader and news reader, terminals, hacking, videos, game over and loading screens, where one-handed use and a different Select/Back pairing are often easier. Any input not listed keeps the stock layout above. The in-game menu and the readers stay in tracked VR (snap turn still works over a reader); only their buttons follow `[ScreenButtons]`.

```ini
[Buttons]
RightA=A
RightB=Y
LeftX=X
LeftY=B
LeftGrip=LB
RightGrip=RB
LeftStickClick=LS
RightStickClick=RS
LeftTrigger=LT
RightTrigger=RT
```

Inputs are `RightA`, `RightB`, `LeftX`, `LeftY`, `LeftGrip`, `RightGrip`, `LeftStickClick`, `RightStickClick`, `LeftTrigger`, `RightTrigger`, plus `RightStickUp` and `RightStickDown` in `[Buttons]` (below). Targets are `A`, `B`, `X`, `Y`, `LB`, `RB`, `LS`/`L3`, `RS`/`R3`, `LT`, `RT`, `Back`, `Start`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `QuickSave`, `HandCalibrate`, or `None`. `QuickSave` presses F5, the game's quicksave key, which has no gamepad binding (for example `LeftXHoldMs=500` with `LeftXTap=QuickSave`). Triggers may be mapped to buttons and buttons to triggers. `<Input>HoldMs=250` makes that input send its target only after it has been held that long, so accidental taps send nothing; with a hold delay set, `<Input>Tap=A` sends a different target as a 120 ms pulse when the input is released early. Unrecognized names are logged and ignored. Restart after editing the INI manually. The left menu button is not remappable: a tap sends Back (in-game menu) and a hold of 1.5 seconds sends Start (pause), as in the table above.

In the VR settings panel, **Button Remapping** opens a large action list with one assigned-control column. Scroll with the arrows or left stick. Select an assignment, release the pointer trigger, then press and release the desired controller button, trigger, or right-stick up/down. For an existing delayed button, tap or hold it to choose that gesture; the delay itself is retained. A confirmation shows the proposed control, its displaced action and the action's previous assignments. Confirming changes the draft and replaces that action's other custom assignments, leaving one chosen binding. Existing duplicates remain unchanged until you explicitly rebind their action. Save & Back applies and persists; Discard & Back abandons the draft. Settings returns to the main panel without losing the draft. Left menu or Cancel exits capture. Simultaneous button chords are not captured. Separate screen bindings, fixed menu/calibration chords and stick axes remain unchanged. Action names describe the default native game layout. The matched transport-v12 DLL and companion are required; saved assignments work without restarting.

`RightStickUp` and `RightStickDown` under `[Buttons]` can hold a button while the right stick is pushed within 30 degrees of vertical during gameplay (for example `RightStickUp=A` to jump and `RightStickDown=LS` to crouch). This gives up vertical stick aiming, which you don't need when aiming with the controller (`ExperimentalMotionControls=1`). It works with snap turn or smooth turn. With `SnapTurn=0`, setting either key takes the stick's vertical axis for these buttons and leaves the horizontal axis for smooth turning; a push inside the 30-degree cone also holds the turn, so jumping or crouching doesn't drift the view. Menus, terminals and the scope keep the native right stick.

## Controller hands

With motion controls enabled the mod draws Jensen's hands at your controllers, instead of showing the arms the game animates. An equipped weapon is carried with the right hand, using the game's own hand-to-weapon relationship, so the gun sits in the hand the way the game intends rather than at a learned offset. The installed configuration turns this on; the settings are:

```ini
ExperimentalMotionControls=1
ControllerHands=1
ControllerHandsMode=15
ControllerHandsModelAlignment=1
ControllerHandsWeapon=1
ControllerHandsLeft=1
WeaponCalibration=0
```

`ControllerHands` defaults to `0`. Restart the game after changing any of them.

**No calibration is required.** `ControllerHandsMode=15` with `ControllerHandsModelAlignment=1` takes the hand position from the player model's own skeleton, so there is no gesture to perform, no file to build, and nothing to redo if you change controllers. `ControllerHandsCalibrate=0` and `WeaponCalibration=0` keep the older measured paths out of the way; they are not needed here and are not recommended.

The left hand needs the 64-bit companion that carries the left controller's pose. The DLL and companion are installed together and must match — an upgrade replaces both. `ControllerHandsLeft=0` leaves the right hand alone.

`ControllerHandsNeutralWrist=1` (the default) holds the wrist at the model's own resting angle to the palm. Without it the wrist keeps the animation the game is playing underneath, which shows up as the wrist wiggling while you sprint and sitting at a different angle for each weapon. Set it to `0` to see the game's animated wrist instead.

### When the hands step aside

The game moves its camera out of Jensen's eyes for several things: taking cover, climbing ladders, lying down, and some scripted shots that ride an animation bone. Hands at your controllers only make sense while you are looking out of his eyes, so in those modes the mod steps aside and the game draws Jensen normally.

`ControllerHandsSuspendModes` is a bit mask over the game's camera modes and defaults to `342` — cover, ladders, the animation-bone camera, lying down and the orbit camera. First-person combat and iron sights are not included, and the conversation and hacking cameras have their own handling. There is rarely a reason to change it.

> **Known limitation — weapons in third-person views.** When the game takes the camera out of Jensen's eyes — cover, ladders and some scripted shots — the controller hands step aside and Jensen is drawn normally, but an equipped weapon still follows your controller rather than returning to his hand. You will see the gun beside you while his hands are empty, and its angle differs from the one you are used to. Aiming and firing still work. This is a known limitation, not a fault in your setup.

### If a player model is not recognized

The hands are built against the exact layout of the player model. The game does not use one model throughout: a second player model, with 162 bones instead of 223, appears from **M1 - Searching for Proof** (Chapter 9, Corporate Warfare) onward at Tai Yong Medical. Both are supported.

If a model turns up that the mod does not recognize, it does not guess at it. The hands are not drawn, Jensen's body is hidden, and the weapon is carried at your controller on its own. Aiming, firing and muzzle effects keep working, so the game stays playable — but this is a degraded state, not a second way to play, and you should not meet it in normal use. The mod keeps retrying about once a second, so a supported model is picked up again on the next load or chapter change without restarting the game.

**If you lose your hands and they do not come back,** the mod has met a model it cannot build against. `DeusExHRVR-hand-only.log` identifies it:

```
handOnly version=3 ready=0 rejected=12 rejectionStage=1 observedBones=...
observedLayout=... observedIndices=...
```

`observedBones`, `observedLayout` and `observedIndices` describe the model that was refused. Those three values are what a new model needs to be added, so include that line when reporting it.

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
