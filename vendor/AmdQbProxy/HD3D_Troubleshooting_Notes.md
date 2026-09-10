# HD3D Proxy Chain — Troubleshooting Notes & Observations

## Architecture Overview

The wiz3D HD3D proxy chain fakes an AMD GPU environment on NVIDIA hardware so that
games with native AMD HD3D stereo 3D support activate their stereo rendering paths.

### Proxy Chain

```
Game loads d3d11.dll (D3d11VendorProxy)
  → Hooks DXGI factory: CreateDXGIFactory / Factory1 / Factory2
  → Hooks IDXGIAdapter::GetDesc / GetDesc1: spoofs VendorId 0x10DE → 0x1002 (AMD)
  → Hooks LoadLibrary*: redirects atidxx32.dll → AmdQbProxy, atiadlxy.dll → AmdAdlProxy
  → Hooks GetProcAddress: intercepts AmdDxExtCreate11 → AmdQbProxy
  → Hooks GetModuleHandleA: triggers AmdQbProxy load on NULL query
  → Hooks Registry: blocks NvAPI Stereo3D / nvapi.dll (HD3D builds only)

Game sees AMD VendorId → enters AMD HD3D code path
  → Loads atiadlxy.dll (AmdAdlProxy) for display config queries
  → Calls AmdDxExtCreate11 → AmdQbProxy returns fake AMD extension interfaces
  → Game calls EnableQuadBufferStereo(TRUE) → Present hook installed
  → Game renders over-under into doubled swap chain
  → HookedPresent composites OvUn → SBS for display
```

### Key DLLs

| DLL             | Proxy         | Role |
|-----------------|---------------|------|
| d3d11.dll       | D3d11VendorProxy | Main entry; VendorId spoof, hook installation, NvAPI blocking |
| dxgi.dll        | DXGI proxy    | Forwards to system dxgi.dll (used by some games for initial factory creation) |
| atiadlxy.dll    | AmdAdlProxy   | AMD Display Library stubs (adapter count, display map config, versions) |
| atiadlxx.dll    | AmdAdlProxy   | x64 variant of above |
| atidxx32.dll    | AmdQbProxy    | AMD Quad-Buffer Stereo COM interface; Present hook for TAB→SBS compositor |
| atidxx64.dll    | AmdQbProxy    | x64 variant of above |

---

## Critical Discovery: NvAPI Must Be Fully Blocked

### The Problem

Many games with AMD HD3D support also have NVIDIA 3D Vision support. These "dual-3D"
games typically check for NvAPI **first**:

1. Game calls `LoadLibrary("nvapi.dll")` → NvAPI loads
2. NvAPI reads `SOFTWARE\NVIDIA Corporation\Global\Stereo3D` registry
3. If NvAPI initializes successfully → game uses 3D Vision path
4. Game **never falls through** to AMD HD3D, even if VendorId says AMD

### Evolution of the Fix

**Phase 1 — Stereo3D registry blocking only (`#ifdef BLOCK_NVAPI_STEREO`)**:
- Blocked `RegOpenKeyEx` for paths containing "Stereo3D" or "Stereo3DPersistent"
- Result: NvAPI **still loaded** through `nvapi.dll`, initialized through other
  registry paths (`Software\NVIDIA Corporation\Global\NvAPI`, `nvtweak`)
- Sleeping Dogs: Stayed mono — NvAPI partially initialized, game saw "NVIDIA present"
  but stereo failed, never fell through to HD3D
- Tomb Raider: Crashed — NvAPI initialized, saw AMD VendorId but real NVIDIA driver
  underneath, fatal mismatch

**Phase 2 — Full nvapi.dll/nvapi64.dll load blocking**:
- Added `IsNvapiDllA`/`IsNvapiDllW` helpers under `#ifdef BLOCK_NVAPI_STEREO`
- All four `LoadLibrary` hooks (`A`/`W`/`ExA`/`ExW`) return a **stub handle** (`g_hSelf`)
  for nvapi DLLs instead of NULL — games that don't NULL-check crash on NULL (Tomb Raider)
- Refcount bumped via `LoadLibraryW(self)` so `FreeLibrary` won't unload our proxy
- Result: NvAPI **cannot initialize at all** → game falls through to AMD HD3D
- Sleeping Dogs: **WORKS!** SBS stereo 3D active
- Grid Autosport: **WORKS!** Stereo active (over-under, compositor investigation needed)
- ⚠️ Tomb Raider/Hitman still crashed — `GetProcAddress(stub, "nvapi_QueryInterface")`
  returned NULL, game called NULL pointer without checking → Phase 4 needed

**Phase 3 — Display adapter class registry spoofing (current)**:
- Games reading `SYSTEM\ControlSet001\Control\Class\{4d36e968-...}\0000` get real
  NVIDIA driver info that contradicts the AMD VendorId spoof
- Added spoofing in `HookedRegQueryValueExA`/`W` under `#ifdef BLOCK_NVAPI_STEREO`:
  - `ProviderName` containing "NVIDIA" → spoofed to "AMD"
  - `DriverDesc` containing "NVIDIA" → spoofed to "AMD Radeon"
  - Any other string value containing "NVIDIA" → replaced with "AMD   " (padded)
  - `MatchingDeviceId` → `ven_10de` replaced with `ven_1002`
- Only triggered when returned data actually contains "nvidia" (case-insensitive)
  so non-NVIDIA data passes through untouched

### Key Insight

**Blocking only the Stereo3D registry is insufficient.** NvAPI initializes through
multiple registry paths. The only reliable way to prevent NvAPI from interfering with
the AMD VendorId spoof is to block `nvapi.dll` / `nvapi64.dll` from loading entirely.

This is safe for HD3D games because they have a complete AMD code path — they don't
need NvAPI at all. The `#ifdef BLOCK_NVAPI_STEREO` ensures the 3DVision experimental
build still allows NvAPI loading for investigation.

---

## Per-Game Observations

### ✅ Sleeping Dogs (Square Enix, 2012)

- **3D Support**: Native AMD HD3D + NVIDIA 3D Vision (confirmed by MTBS3D review)
- **Status**: WORKING with nvapi.dll blocking
- **Architecture**: x86 (HKShip.exe)
- **Key log evidence**:
  - `LoadLibraryA(nvapi.dll) -> BLOCKED (nvapi)` — NvAPI blocked
  - `GetProcAddress(AmdDxExtCreate11) intercepted` — AMD HD3D path activated
  - `AmdQbProxy loaded OK` — quad-buffer proxy loaded
  - No `GetDesc1` calls logged — game doesn't query adapter descriptor directly,
    relies on AmdDxExtCreate11 succeeding to confirm AMD hardware
- **Confirmed AmdQbProxy pipeline** (from `wiz3D_atidxx.log`):
  ```
  AmdDxExtCreate11 called
  EnableQuadBufferStereo(TRUE) - installing Present hook
  IDXGISwapChain::Present hook installed
  IDXGIFactory::CreateSwapChain hook installed
  CreateSwapChain: 3840x2160 -> 3840x4320 (stereo)
  Present #1: BB=3840x4320 fmt=28 origH=2160 windowed=0
  Compositor OK: staging/srv/vs/ps/cb/rtv all created
  ResizeBuffers: 3840x2160 -> 3840x4320 (stereo)
  ```
  Full pipeline working: quad-buffer enabled → swap chain doubled → compositor active → SBS output
- **Notes from MTBS3D review** (Neil, Oct 2012):
  - Native HD3D scored 4.5/10 stereoscopic effectiveness
  - Camera separation inconsistency — some scenes have proper parallax, others
    have nearly identical left/right views (like images offset but not separated)
  - Crosshair rendered incorrectly — bullets don't go where aimed
  - Blur effects only rendered in one eye during races (turn off)
  - Navigation markers in mono (artistic choice per reviewer)
  - Separation/convergence settings "clumsy" — game developer may not have fully
    understood the parameters
  - Same issues present on both AMD HD3D and NVIDIA 3D Vision platforms
  - Square Enix published (same as Tomb Raider, Deus Ex HR, Hitman Absolution)

### ✅ Grid Autosport (Codemasters, EGO engine, 2014)

- **3D Support**: Native AMD HD3D
- **Status**: WORKING — stereo active, shows over-under (compositor not triggering)
- **Architecture**: x86 (GRIDAutosport_avx.exe)
- **Previous issue**: Black screen caused by global `origH` variable
  - Game creates TWO swap chains: 3840×2160 and 1920×1080
  - Global origH was overwritten by second SC (1080), then Present on first SC
    saw BB=4320 but expected 2×1080=2160 → skipped as non-stereo → black
  - **Fixed**: Per-swap-chain `std::unordered_map<IDXGISwapChain*, UINT>` tracking
- **Current behavior**:
  - Stereo activates but output is over-under, not SBS
  - `wiz3D_atidxx.log` shows `AmdDxExtCreate11 called` (×2) and `GetDisplayModeList`
    but NO `EnableQuadBufferStereo` / `Present hook` lines
  - This means EGO engine called AmdDxExtCreate11 and got display modes, but the
    second AmdDxExtCreate11 call (different code path?) may not be requesting the
    quad-buffer stereo extension
  - **Backburner**: Investigate why EnableQuadBufferStereo isn't being called by the
    EGO engine in Grid Autosport (it may need ADL display mode injection or a
    different initialization sequence)
- **nvapi.dll blocked**: Yes — `LoadLibraryA(nvapi.dll) -> BLOCKED (nvapi)` in log

### ✅ Dirt Showdown (Codemasters, EGO engine, 2012)

- **3D Support**: Native AMD HD3D
- **Status**: Stereo 3D activates (display output issues deferred)
- **Architecture**: x86
- **Notes**: First game confirmed working. EGO engine game like Grid 2/Autosport.

### ✅ Grid 2 (Codemasters, EGO engine, 2013)

- **3D Support**: Native AMD HD3D
- **Status**: Stereo 3D activates (display output issues deferred)
- **Architecture**: x86
- **Notes**: EGO engine game, confirmed working early in development.

### 🔧 Tomb Raider (Square Enix / Crystal Dynamics, 2013)

- **3D Support**: Native AMD HD3D + NVIDIA 3D Vision
- **Status**: TESTING — nvapi_QueryInterface export added (Phase 4)
- **Architecture**: x86 (TombRaider.exe)
- **Diagnosis**:
  - VendorId spoof works: `GetDesc1: VendorId 0x10DE->0x1002`
  - Stereo3D registry blocked: `RegOpenKeyExA(SOFTWARE\NVIDIA Corporation\Global\Stereo3D) -> BLOCKED`
  - nvapi.dll stubbed: `LoadLibraryA(nvapi.dll) -> STUBBED (nvapi)` — returns our own
    module as a valid HMODULE instead of NULL (game doesn't NULL-check LoadLibrary return)
  - Display adapter class keys opened 6× but **no RegQueryValueEx calls logged** for them
  - **Registry spoofing**: Game reads display adapter class registry directly:
    `SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000`
    This path contains real NVIDIA driver info (ProviderName, DriverDesc, MatchingDeviceId)
    that contradicts the AMD VendorId spoof → spoofed to AMD values
- **Fix history**:
  - Phase 1 (Stereo3D only): NvAPI still loaded, initialized, crashed on AMD VendorId mismatch
  - Phase 2 (nvapi NULL): Immediate crash — game doesn't check LoadLibrary return
  - Phase 3 (stub handle + registry spoof): Stub returned, but game called
    `GetProcAddress(stub, "nvapi_QueryInterface")` → NULL → game called NULL → crash
  - Phase 4 (nvapi_QueryInterface export): d3d11.dll now exports `nvapi_QueryInterface`
    returning NULL for all IDs → NvAPI unavailable → should fall through to AMD HD3D
- **Key observation**: Game never loads ADL or atidxx — it hasn't reached the AMD HD3D
  code path yet. The crash happens during NvAPI init before any AMD extension calls.
- **Note**: Crystal Dynamics engine may have deeper NVIDIA driver integration than other
  HD3D games. Same publisher (Square Enix) as Sleeping Dogs, Deus Ex HR, Hitman Absolution.

### 🔧 Hitman Absolution (Square Enix / IO Interactive, 2012)

- **3D Support**: Native AMD HD3D
- **Status**: NEEDS RETEST — exe name bug fixed, nvapi blocking complete
- **Architecture**: x86 (HMA.exe)
- **Known fixes applied**:
  - Phase 4: `nvapi_QueryInterface` export added to d3d11.dll (returns `NvApiStub` pointer,
    not NULL — avoids NULL-call crash), nvapi.dll LoadLibrary now returns NULL (mimics AMD)
  - **Exe name fix**: Detection in AmdQbProxy was checking `wcsstr(procPath, L"hitmanabsolution.exe")`
    but the actual exe is `HMA.exe`. Fixed to `L"hma.exe"` — `g_bHitmanAbsolution` flag
    was never being set before this fix, so the Hitman half-height compositor path was
    never activating; game fell through to generic compositor.
- **Hitman compositor path** (now correctly gated on `HMA.exe` detection):
  - Glacier engine renders each eye's 3D into the TOP HALF of its T&B shadow slot
  - uvScl=0.25, uvOff 0.0/0.5 to stretch half-height content to full SBS viewport
  - Present1 hook also installed (game uses Present for loading, Present1 for rendering)
- **Previous diagnosis**:
  - ADL loaded OK, AmdQbProxy loaded OK
  - AmdDxExtCreate11 called successfully
  - GetDisplayModeList called 4× (304 modes each)
  - NO EnableQuadBufferStereo called — game still initializing at time of crash
  - nvapi.dll loaded 4× as STUBBED after AMD ext init
  - Crash at `atidxx32.dll!54562abc()` (unreliable per debugger warning — crash was NULL fn pointer call)
- **Next**: Build and test with HMA.exe. Watch for `Hitman Absolution detected` in log.

### 🔧 Sniper Elite 3 (Rebellion, 2014)

- **3D Support**: Native AMD HD3D (in-game option)
- **Status**: HD3D greyed out in menu
- **Architecture**: x86 or x64 (SniperElite3.exe)
- **Diagnosis**:
  - VendorId spoof works: `GetDesc1: VendorId 0x10DE->0x1002` (multiple calls)
  - **NO ADL or atidxx loading logged** — game never attempts AMD extension init
  - Game has a pre-game launcher that runs before d3d11.dll loads
  - `wiz3D_dxgi.log` only appears after starting the actual game (past launcher)
  - Launcher may save config with HD3D=disabled before proxy loads
  - Game reads config at startup → HD3D already disabled → never checks for AMD
- **Likely fix**: User manually edits game config file to enable AMD HD3D option,
  bypassing the launcher's detection. The proxy would then handle the rest.

---

## NVIDIA 3D Vision Architecture (Observed from Logs)

### How Games Detect 3D Vision

From observing registry access patterns across multiple games:

1. **NvAPI Loading**: Game calls `LoadLibrary("nvapi.dll")` (x86) or `nvapi64.dll` (x64)
2. **NvAPI Initialization**: NvAPI reads from multiple registry paths:
   - `Software\NVIDIA Corporation\Global\NvAPI` — NvAPI core config
   - `Software\NVIDIA Corporation\Global\nvtweak` — NvAPI tweaks
     (`enableSRS1364`, `enable1364BP`, `enable1364RP`, `EnableIOCTLPathForNVAPI`)
   - Various `EnableGR*` and `EnableRID*` values
3. **Stereo3D Check**: NvAPI reads stereo-specific paths:
   - `SOFTWARE\NVIDIA Corporation\Global\Stereo3D` — Main stereo config
   - `SOFTWARE\NVIDIA Corporation\Global\Stereo3D\Old\37204` — Legacy stereo data
   - `SOFTWARE\NVIDIA Corporation\Global\Stereo3DPersistent` — Persistent stereo state
4. **3D Vision Install Location**: Games may query:
   - `InstallLocation` = `"C:\Program Files (x86)\NVIDIA Corporation\3D Vision"`
5. **Display Adapter Class**: Some games read raw GPU driver info:
   - `SYSTEM\ControlSet001\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000`
   - Contains DriverVersion, InfSection, ProviderName, adapter description, etc.

### NVIDIA Registry Path Hierarchy

```
SOFTWARE\NVIDIA Corporation\Global\
├── NvAPI                          — NvAPI core (always accessed)
├── nvtweak                        — Feature flags and tweaks
├── Stereo3D\                      — Main stereo 3D configuration
│   └── Old\37204                  — Legacy/backup stereo data
├── Stereo3DPersistent\            — Persistent stereo state across sessions
└── EHEX                           — Unknown purpose (always returns ERROR_FILE_NOT_FOUND)

SYSTEM\ControlSet001\Control\Class\
└── {4d36e968-e325-11ce-bfc1-08002be10318}\  — Display adapter class GUID
    └── 0000\                      — First GPU adapter (real NVIDIA driver info)
        ├── DriverVersion
        ├── InfSection
        ├── ProviderName
        └── DirectXUserGlobalSettings
```

### Blocking Strategy

For HD3D builds, three levels of defense are employed:

1. **nvapi.dll stub handle** (LoadLibrary hooks): Returns our own d3d11.dll as a stub
   HMODULE for nvapi.dll/nvapi64.dll. `GetProcAddress(stub, "nvapi_QueryInterface")`
   finds our exported stub → returns NULL for all IDs → NvAPI cannot initialize → game
   falls through to AMD HD3D. Using a stub handle instead of NULL prevents crashes in
   games that don't check LoadLibrary's return value (e.g. Tomb Raider).

2. **Stereo3D registry blocking** (RegOpenKeyEx hooks): Blocks `Stereo3D` and
   `Stereo3DPersistent` registry paths. Secondary defense that was the original approach
   but proved insufficient alone because NvAPI still initializes through `NvAPI` and
   `nvtweak` paths.

3. **Display adapter class registry spoofing** (RegQueryValueEx hooks): Spoofs NVIDIA
   driver info returned from `{4d36e968-e325-11ce-bfc1-08002be10318}\0000` display
   adapter class keys. Replaces NVIDIA ProviderName/DriverDesc with AMD equivalents,
   replaces `ven_10de` with `ven_1002` in MatchingDeviceId. Only triggered when returned
   data actually contains "nvidia" so non-NVIDIA data passes through untouched.

4. **nvapi_QueryInterface export** (d3d11.dll export, HD3D builds only): Games that
   load `nvapi.dll` (getting our stub handle) then call `GetProcAddress(stub,
   "nvapi_QueryInterface")`. Without a real export, GetProcAddress returns NULL and
   games that don't NULL-check it (Tomb Raider, likely Hitman Absolution) crash calling
   a NULL function pointer. Our d3d11.dll now exports `nvapi_QueryInterface` via
   `__declspec(dllexport)` (only in `BLOCK_NVAPI_STEREO` builds) — it returns nullptr
   for every interface ID, telling NvAPI "not available". This is ordinal 52 alongside
   the 51 d3d11.dll exports.

All are compiled under `#ifdef BLOCK_NVAPI_STEREO` so the 3DVision experimental build
can still allow NvAPI loading for hybrid investigations.

---

## AMD HD3D Architecture (Observed from Logs)

### How Games Detect AMD HD3D

1. **VendorId Check**: Game calls `IDXGIAdapter::GetDesc` or `IDXGIAdapter1::GetDesc1`
   - If VendorId == 0x1002 (AMD) → proceed to HD3D check
   - Our proxy spoofs 0x10DE (NVIDIA) → 0x1002 (AMD)

2. **ADL Loading**: Game loads `atiadlxy.dll` (x86) or `atiadlxx.dll` (x64)
   - Queries adapter count, adapter info, display map configuration
   - Display map config provides resolution info (e.g., 3840×2160, 1 target)
   - Some games also query `ADL_Graphics_Versions_Get`
   - Our AmdAdlProxy provides stub implementations

3. **Quad-Buffer Extension**: Game calls `AmdDxExtCreate11(pDevice, &pExt)`
   - Two ways this happens:
     a. Static import (game has AmdDxExtCreate11 in IAT) — PatchAmdIAT handles this
     b. Dynamic: `GetProcAddress(hModule, "AmdDxExtCreate11")` — GetProcAddress hook
        intercepts and returns our proxy function
   - Game gets `IAmdDxExt` interface, then calls `GetExtInterface(AmdDxExtQuadBufferStereoID)`
   - Gets `IAmdDxExtQuadBufferStereo` with `EnableQuadBufferStereo`, `GetLineOffset`,
     `GetDisplayModeList`

4. **Stereo Activation**: Game calls `EnableQuadBufferStereo(TRUE)`
   - Our proxy installs Present/ResizeBuffers/CreateSwapChain hooks
   - Future `CreateSwapChain` calls get height doubled (e.g., 1080 → 2160)
   - `GetLineOffset` returns the original (pre-doubled) height

5. **Rendering**: Game renders left eye to top half, right eye to bottom half of
   the doubled buffer. Our `HookedPresent` composites to SBS for display.

### ADL Functions Used by Games

From logs, the EGO engine (Codemasters) and Square Enix games use:
- `ADL_Main_Control_Create` / `Destroy`
- `ADL_Adapter_NumberOfAdapters_Get` → 1
- `ADL_Adapter_AdapterInfo_Get` → adapter info struct (1572 bytes)
- `ADL_Display_DisplayMapConfig_Get` → display resolution and target count
- `ADL_Graphics_Versions_Get`
- `ADL_Display_SLSMapIndex_Get` (queried but may not be critical)
- `ADL_Display_SLSMapConfig_Get` (queried but may not be critical)
- `ADL_Display_Modes_Get` (queried but may not be critical)
- `ADL_Graphics_Platform_Get` → returns NULL (not implemented, games handle gracefully)

---

## Multi-Swap-Chain Handling

### The Problem (Fixed)

Some games create multiple DXGI swap chains. The original AmdQbProxy used a single
global `g_nOrigHeight` to track the pre-doubled height. When multiple SCs were created,
the last one overwrote the global, causing Present to miscalculate for earlier SCs.

**Grid Autosport example**:
```
CreateSwapChain: 3840×2160 → 3840×4320 (origH stored as 2160)
CreateSwapChain: 1920×1080 → 1920×2160 (origH OVERWRITTEN to 1080)
Present on SC1: BB=3840×4320, expected H = 2×1080 = 2160, actual 4320 → SKIP
```

### The Fix

Replaced global `g_nOrigHeight` with `std::unordered_map<IDXGISwapChain*, UINT>`:
- `HookedCreateSwapChain`: Stores origH keyed by `*ppSC` after successful creation
- `HookedResizeBuffers`: Updates map entry for the specific SC
- `HookedPresent`: Looks up origH from map using `pSC`
- `GetLineOffset`: Looks up from map using `pSC`
- SCs not in the map (non-stereo, UI overlays) pass through silently

---

## Build Configuration

### HD3D Build (Release)
- Preprocessor: `BLOCK_NVAPI_STEREO` defined
- Blocks: nvapi.dll stub handle + Stereo3D/Stereo3DPersistent registry + display adapter class spoofing
- Output: `releases/wiz3D/hd3d/x86/` and `x64/`
- Function count: ~125-128 (x86/x64, varies with optimizer)

### 3DVision Build (3DVision Release)
- Preprocessor: `BLOCK_NVAPI_STEREO` NOT defined
- Blocks: Nothing — NvAPI loads freely, Stereo3D registry accessible
- Output: `releases/wiz3D/3dvision/x86/` and `x64/`
- Function count: 121 (x86), 123 (x64)
- Purpose: Experimental hybrid investigation

### Function Count Verification
The function difference between HD3D and 3DVision builds confirms `#ifdef` is working:
- `ContainsNvStereo3DA` / `ContainsNvStereo3DW` (registry blocking helpers)
- `IsNvapiDllA` / `IsNvapiDllW` (LoadLibrary blocking helpers)
- `GetNvapiStubHandle` (stub handle for nvapi blocking)
- Registry spoofing code in `HookedRegQueryValueExA` / `HookedRegQueryValueExW`

---

## Known Game-Specific Backup Folders

Per copilot-instructions.md, the following are known-good backup copies and must NOT
be updated with new builds:
- `releases/wiz3D/hd3d/Deus Ex Human Revolution/`
- `releases/wiz3D/hd3d/Deus Ex Human Revolution Director's Cut/`
- `releases/wiz3D/hd3d/Sleeping Dogs/` (confirmed working build)

Only update `x86/` and `x64/` folders with new builds.

---

## Open Issues & Next Steps

1. **Tomb Raider**: nvapi_QueryInterface export added — needs retest (Phase 4)
2. **Hitman Absolution**: nvapi_QueryInterface export likely fixes crash — retest
3. **Sniper Elite 3**: HD3D greyed out — try manually enabling in game config
4. **Grid Autosport over-under**: Compositor not triggering — investigate why
   `EnableQuadBufferStereo` isn't being called
5. **Sniper Elite Nazi Zombie Army** (all 3 games): Stereo activates, output issues
6. **Display output**: Dirt Showdown and Grid 2 have stereo active but display
   output issues (deferred until more games working)
7. **3DVision hybrid path**: Untested experimental concept
