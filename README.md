# SpaceDuck — spectral sidechain ducker AUv3

A TrackSpacer-style spectral sidechain ducker for iPad, built for one
person's iPad Pro M2. Personal use only — no App Store upload, no paid
Apple Developer Program. (Working name "SpaceDuck" is deliberate: the
"TrackSpacer" name is a Wavesfactory trademark and is not used anywhere.)

**What it does:** put it on a beat track and feed it a vocal on its
sidechain input. It ducks the beat only in the frequencies the vocal
occupies, only while the vocal is there — so the vocal sits on top without
the whole beat pumping. A normal sidechain compressor turns the whole beat
down; this one carves out just the clashing frequencies.

**Status:** the DSP core is written and unit-tested on Linux (see below).
The full iOS AUv3 has not been compiled yet — there is no Mac or Xcode in
this environment. The first GitHub Actions build is the real test; see
"Most likely CI failure points" below.

## How to build / install

See **STEP-BY-STEP.md** — the plain-language guide (GitHub repo → Actions
build → SideStore install → Cubasis setup → weekly refresh). No Xcode, no
paid account, no newer Mac needed.

## Design decisions (and the evidence behind them)

### 1. Primary input: the standard AU sidechain bus

The plugin declares exactly **two stereo input buses** (bus 0 = main, bus 1
= sidechain) and one stereo output. There is no "sidechain flag" in JUCE
or AU — input element 1 *is* the sidechain by position. The declaration
follows JUCE's own `NoiseGatePluginDemo`: `BusesProperties` with the
sidechain input declared last and enabled by default, and
`isBusesLayoutSupported` constraining only the main bus (in == out, not
disabled) while accepting any sidechain layout — including disabled — so
the plugin still loads when the host connects nothing to bus 1. An
unconnected sidechain reads as silence, which the DSP treats as "no
trigger" (clean passthrough, never runaway ducking).

**Evidence — Cubasis 3 supports this since 3.3 (4 May 2021):** Steinberg's
version history lists "Sidechain support" for 3.3, and the Cubasis 3
Operation Manual (v3.6.5, p. 52) says the side-chaining button "appears
in the effect rack's tool bar for effects that have a side-chaining audio
input, such as the Channel Strip and certain Audio Unit effect plug-ins,"
and that tapping it shows a list of tracks to pick the trigger from. The
slot then expands to show the source track, a gain slider, and a pre/post
toggle. The old 2017 JUCE-forum claim that "sidechain AUv3 plug-ins do
not work in Cubasis" predates 3.3 and is superseded. AUM also supports
multi-bus AUv3 natively ("Multi-bus Audio Unit instances" nodes).
GarageBand iOS has no sidechain routing at all — the bus simply never
gets fed there.

### 2. LINK mode (MASTER/SLAVE) is the fallback, not the primary

For hosts with no sidechain routing, two instances cooperate through an
in-process shared store (`Source/Plugin/LinkHub.h`): a **Link Master** on
the trigger track analyses its own main input and publishes per-band
levels; a **Link Slave** on the ducked track reads them and ducks with
them. The DSP core supports both roles natively (`setMasterMode`,
`setLinkedBandLevels`). If no MASTER is publishing, the SLAVE falls back
to its own sidechain bus if one is connected, else clean passthrough.

**Caveat (unverified):** LINK relies on all instances of the AU in one
host sharing one extension process. A Loopy Pro forum developer reports
"each AU only gets one process per host", and 4Pockets' Sidechain
Compressor ships exactly this MASTER/SLAVE design — but Apple does not
guarantee it. Treat LINK as a fallback to prove on-device, not as the
primary mechanism. The SLAVE also expires stale levels after 500 ms (dead
MASTER → no ducking, never frozen gains).

### 3. JUCE version: 9.0.5 (pinned via FetchContent)

JUCE 9.0.5 (2 Nov 2026) is the current release, with the best iOS 26 /
Xcode 27 support; 8.0.15 is the conservative fallback. Do not start on
JUCE 7 — it is security-patches-only now, with no iOS 26 support work.

**Licensing (plain answer):** free personal use is fine with no
distribution. JUCE 7's free "Personal" tier (splash screen) is gone;
JUCE 8/9 replaced it with the free **Starter** tier (up to $20k revenue,
no splash screen). The open-source route is **AGPLv3** (not GPLv3 — that
changed at 8.0.0). A build installed only on one person's own iPad,
never shared or sold, is not distributed, so no AGPL source-offer
obligation is triggered; the Starter tier is the simpler story anyway.
*Not legal advice; the EULA text governs.*

**JUCE gotchas baked into the code:**
- Buses are declared **statically**; `canAddBus()`/`canRemoveBus()` are
  left at their defaults (`false`). This sidesteps the JUCE 9.0.1 AU
  `SetBusCount` bug (Logic adds a sidechain bus to writable effects and
  initialisation fails) and matches how the AUv3 wrapper works (buses are
  fixed at init — there is no `SetBusCount` in the AUv3 wrapper at all).
- `JucePlugin_PreferredChannelConfigurations` is not defined (CMake
  `juce_add_plugin` doesn't define it by default).
- Never hand-index the `processBlock` buffer — use `getBusBuffer()`. The
  buffer bundles all buses, and the sidechain channels may map to memory
  the host assumes is read-only.

### 4. DSP core (plain C++, JUCE-independent)

`Source/DSP/SpectralDucker.{h,cpp}` — written fresh, no JUCE dependency,
so it compiles and tests on Linux.

- **STFT:** FFT 2048, Hann window, hop 512. Overlap-add with the
  synthesis window; the Hann + hop-N/4 overlap-add of window² is the
  constant 1.5, which the code divides out for unity gain.
- **Bands:** 32 log-spaced bands, 40 Hz–16 kHz. Per-band sidechain level
  (dBFS, normalised so a full-scale sine reads ~0 dB). Reduction per band
  = `Amount × (level − Threshold)`, capped at `maxReductionDb` (24 dB).
- **Smoothing:** per-band attack/release envelopes (attack 1–100 ms,
  release 50–500 ms), applied per hop.
- **Latency:** 1536 samples = FFT size − hop, ~32 ms at 48 kHz. The
  wrapper reports it via `setLatencySamples()` so hosts compensate.
- **Parameters:** Amount (0–100%, default 70), Attack (10 ms), Release
  (150 ms), Threshold (−40 dB), Listen (monitor the sidechain), Input
  mode (Sidechain / Link Master / Link Slave), Link channel (1–4).
- **Listen mode** outputs the sidechain signal so the trigger can be
  checked by ear. In Link Master mode the output already *is* the
  trigger, so Listen is a no-op there.

### 5. Bundle IDs and codes

- Containing app: `com.dudelampoon.spaceduck`
- AUv3 extension: JUCE's default derivation,
  `com.dudelampoon.spaceduck.spaceduckAUv3` (app ID + one suffix
  component), which satisfies Apple's prefix rule. The workflow prints
  the built `.appex` Info.plist bundle ID so this can be checked on the
  first CI run — do not override it with
  `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER` (JUCEAide generates the
  extension plist from `BUNDLE_ID` and ignores the override).
- `PLUGIN_MANUFACTURER_CODE DudL`, `PLUGIN_CODE Spdu` (exactly one
  upper-case letter, as AU requires).

### 6. CI build (GitHub Actions, macos-26)

- Runner `macos-26` ships **Xcode 26.6** by default; the workflow pins
  `DEVELOPER_DIR` to it and asserts `xcodebuild -version` first.
- **Unsigned on purpose:** `CODE_SIGN_IDENTITY=""`,
  `CODE_SIGNING_REQUIRED=NO`, `CODE_SIGNING_ALLOWED=NO` — no team, no
  certificates on the runner. SideStore signs the `.ipa` on the iPad with
  the free Apple ID instead. Do not set `DEVELOPMENT_TEAM`.
- **Plain `cmake --build`** (not `xcodebuild archive`): the archive route
  is unverified for JUCE AUv3 targets. The `.app` is taken from
  `build-ios/Build/Products/Release-iphoneos/`, zipped as
  `Payload/*.app` into an unsigned `.ipa`, uploaded as an artifact
  (30-day retention — download it promptly; artifacts are not a
  distribution channel).
- CMake: Xcode generator (required for the AUv3 target — Ninja/Make
  silently drop it), `CMAKE_SYSTEM_NAME=iOS`, `iphoneos` sysroot,
  `arm64` only, deployment target 15.0.
- Free-plan note: GitHub Free includes 2,000 Actions minutes/month, but
  macOS runners are billed at 10× the Linux rate — one ~10-minute build
  costs ~100 billed minutes. Still plenty for personal use.

### 7. SideStore install (iPadOS 27 reality, Nov 2026)

- **Latest iPadOS is 27.2.1** (9 Nov 2026); the iPad Pro M2 is on the 27
  list. SideStore has **no confirmed stable support for iPadOS 27** —
  the workable path is the **Nightly build (27 Oct 2026, SDK 27.2 bump)
  via iloader 2.4.1**, expecting possible anisette sign-in fiddling.
  Sideloadly 0.71.0 is the only tool found explicitly claiming iPadOS 27
  support, and the strongest fallback.
- SideStore **0.7.0-alpha** (15 Sep 2026) is the latest marked release;
  **do not use 0.6.4** (sign-in broken by an Apple server-side change).
- **iloader 2.4.1** (28 Oct 2026) is the official SideStore installer.
  Stated macOS floors conflict (10.13 vs 10.15) but are all below macOS
  12 — so it *should* run on the 2015 MacBook Pro (Monterey), but that
  is **unverified on this exact Mac**; treat the first iloader run as the
  test. Fallbacks, ranked: Sideloadly 0.71.0 (explicit Monterey +
  iPadOS 27 support) → AltStore Classic + AltServer (macOS 11+) →
  Windows PC + iloader.
- **LocalDevVPN** (free, App Store) is required — it supplies the
  on-device loopback VPN SideStore needs to sign/refresh (a free-signed
  app can't hold the VPN entitlement itself).
- **Free Apple ID limits:** 7-day provisioning expiry, **3 apps per
  device including SideStore itself** (so SideStore + SpaceDuck = 2 of 3,
  one spare), 10 App IDs per 7 days. The AUv3 extension normally costs
  its own App ID (2 total); SideStore offers a "Use Main Profile" choice
  for extensions — exact label/location unverified in current builds.
  Refresh manually every 4–5 days; don't rely on background refresh
  alone. If SideStore itself expires, it can't refresh anything —
  reinstall it via iloader.

## DSP test results (Linux, g++, 48 kHz)

Run: `cmake -S Tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests`

```
[PASS] unity passthrough (silent sidechain) (max abs err 0.00001, latency 1536 samples)
[PASS] band-selective ducking (1 kHz trigger) (1k band 17: 21.2 dB, 100Hz band 4: 0.0 dB, 8k band 28: 0.0 dB)
[PASS] output level drops during trigger (RMS before 0.332, during 0.256)
[PASS] release recovery (gains return to ~0 dB) (1k band reduction at t=1.5s: 0.02 dB)
[PASS] Amount=0 means no ducking (max abs err 0.00000)
[PASS] listen mode outputs the sidechain (max abs err 0.00000)
[PASS] LINK mode (MASTER levels drive SLAVE ducking) (1k band 17: 21.2 dB, 100Hz band 4: 0.0 dB, 8k band 28: 0.0 dB)
[PASS] latency matches reported value (measured 1536 samples, reported 1536)
ALL PASS: 0 failure(s)
```

## Most likely CI failure points (check the Action log first)

1. **JUCE FetchContent fails / wrong tag** — the `9.0.5` tag must exist
   on `juce-framework/JUCE`. If it 404s, check the tag name in the repo.
2. **CMake version** — the runner ships CMake 4.4.5; JUCE 9.0.5
   requires ≥ 3.22. If configure fails on CMake policy errors, pin an
   older CMake in the workflow.
3. **AUv3 target missing** — happens if the generator isn't Xcode.
   The workflow uses `-G Xcode`; if the `.appex` isn't in the built
   `.app`, that's the first thing to check (the workflow fails the run
   if no `.appex` is found).
4. **"Signing requires a development team"** — means one of the three
   signing flags didn't reach xcodebuild. The workflow passes them at
   both configure time (`CMAKE_XCODE_ATTRIBUTE_*`) and build time.
5. **Wrong bundle ID on the `.appex`** — the workflow prints both
   bundle IDs; the extension's must start with
   `com.dudelampoon.spaceduck`.
6. **AUv3 doesn't appear in Cubasis after install** — open the
   SpaceDuck container app first (extension registration), then check
   Cubasis's AU list. If still missing, the `.appex` entitlements are
   the suspect (JUCE forum reports AUv3 targets need their generated
   entitlements passed to codesign) — but with SideStore signing this is
   SideStore's side to get right; try reinstalling, then the Sideloadly
   fallback.

## Known limitations / open items

- The full iOS AUv3 has **not** been compiled anywhere yet — first CI
  build is untested. The JUCE wrapper code follows JUCE's own
  NoiseGate demo pattern and the AUv3 wrapper source as read in Nov 2026.
- **AUv3 discovery by Cubasis after SideStore signing is unverified** —
  no source confirms a SideStore-signed AUv3 extension is picked up by
  hosts, or that registration survives a SideStore refresh. Test
  on-device after install (open container app → check Cubasis AU list →
  re-check after a manual Refresh All).
- **LINK process-sharing is unproven on-device** (see §2).
- **iloader 2.4.1 on macOS 12 Monterey is unverified** on this exact Mac.
- **SideStore on iPadOS 27 is experimental** (Nightly only, possible
  anisette sign-in issues — see STEP-BY-STEP troubleshooting).
- The "Use Main Profile" toggle for extensions: exact label/location
  unverified in current SideStore builds.
- Cubasis LE tier: AUv3 sidechain routing in the free LE tier is
  unverified (the 3.3 feature list doesn't carve it out, but there's no
  explicit confirmation).

## Project layout

```
SpaceDuck/
├── CMakeLists.txt                  # JUCE project (AUv3 + Standalone)
├── .github/workflows/build.yml    # macos-26 unsigned .ipa build
├── README.md                       # this file
├── STEP-BY-STEP.md                 # plain-language build/install guide
├── Source/
│   ├── DSP/
│   │   ├── SpectralDucker.h/.cpp  # plain-C++ STFT ducking core (tested)
│   ├── Plugin/
│   │   ├── SpaceDuckAudioProcessor.h/.cpp  # JUCE wrapper, buses, LINK wiring
│   │   ├── SpaceDuckEditor.h/.cpp         # UI (sliders, mode, status)
│   │   ├── PluginParameters.h             # parameter IDs/defaults
│   │   └── LinkHub.h                      # in-process MASTER/SLAVE store
└── Tests/
    ├── test_ducker.cpp             # DSP test harness (7 tests)
    └── CMakeLists.txt              # standalone test build
```
