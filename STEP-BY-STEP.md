# SpaceDuck — step by step

This is the full walkthrough for getting SpaceDuck (your TrackSpacer-style
spectral sidechain ducker) built and running on your iPad Pro M2. You do
**not** need Xcode, a paid Apple Developer account ($99), or a newer Mac.
The build happens on GitHub's computers; the install happens with
SideStore; your 2015 MacBook Pro is only needed once, to set SideStore up.

**Time:** roughly 1–2 hours the first time, most of it waiting on the
build and downloads. After that, installing updates is a few minutes.

---

## Before you start — what you need

- Your **iPad Pro M2** (charged, on Wi-Fi).
- Your **2015 MacBook Pro** (for the one-time SideStore setup) and a
  **USB cable** to connect the iPad to it.
- A **free Apple ID** — the one you already use on the iPad is fine.
- A **free GitHub account** — make one in Step 2 if you don't have one.
- The project files — they're in a folder called `spaceduck-auv3` that
  Meta made for you. You'll upload them to GitHub in Step 3.

**One important check first:** this guide was written in November 2026,
when the latest iPadOS is **27.2.1**. SideStore's stable version doesn't
officially support iPadOS 27 yet, so we use the **Nightly** build — it
works, but it's the experimental channel. If anything in Steps 6–8
behaves oddly, the troubleshooting section at the end covers the known
fixes, and Sideloadly is the fallback.

---

## Step 1 — Check your iPadOS version

1. On the iPad, open **Settings → General → About**.
2. Look at **iPadOS Version**. Write it down.
   - **27.x** (most likely, e.g. 27.2.1) → use the **Nightly** build of
     SideStore in Step 6. This guide assumes this.
   - **26.4 or higher** → also use the **Nightly** build.
   - **Below 26.4** → use the **Stable** build instead (the steps are the
     same, just pick the other button in iloader).
3. **Do not update to a beta version of iPadOS** while doing this — betas
   break sideloading tools.

## Step 2 — Make a free GitHub account (skip if you have one)

1. On any device, go to **github.com** and tap **Sign up**.
2. Follow the prompts (email, password, username). It's free — pick the
   free plan when it asks.
3. Verify your email when GitHub sends the confirmation.

## Step 3 — Create a PUBLIC repo and upload the project files

The build only works on a **public** repo on the free plan — GitHub
charges for private-repo macOS builds, and public repos build for free.

**Web upload (easiest — no git needed):**

1. On github.com, tap the **+** in the top-right corner → **New
   repository**.
2. Name it `spaceduck-auv3`. Leave the description blank.
3. Set it to **Public**. (This is required.)
4. Tick **Add a README file** — it doesn't matter, it just gets the repo
   started. Then tap **Create repository**.
5. On the new repo page, tap **Add file → Upload files**.
6. Drag the **contents** of the `spaceduck-auv3` folder into the upload
   area — the `CMakeLists.txt`, `README.md`, `STEP-BY-STEP.md`, the
   `Source` folder, the `Tests` folder, and the `.github` folder. (If
   your browser doesn't show the `.github` folder, upload it separately
   the same way — it has to be there, it's what triggers the build.)
7. Tap **Commit changes**.

**Git path (only if you're comfortable with the terminal):**

```bash
git init
git add .
git commit -m "SpaceDuck 1.0.0"
git branch -M main
git remote add origin https://github.com/YOUR-USERNAME/spaceduck-auv3.git
git push -u origin main
```

## Step 4 — Let GitHub build it, then download the .ipa

1. After you push/upload, go to your repo on github.com and tap the
   **Actions** tab at the top.
2. You'll see a run called **"Build SpaceDuck (iOS, unsigned)"** — it
   starts automatically. Wait for it to finish (roughly 10–15 minutes).
   A green checkmark means it worked; a red X means it failed.
3. If it failed: tap the failed run, scroll through the log, and send
   the error text to Meta — the most likely causes are listed in the
   README ("Most likely CI failure points"). Don't delete the repo; the
   fix is usually a one-line change.
4. When it's green, tap the run, scroll to the bottom to **Artifacts**,
   and download **SpaceDuck-ipa**. It's a zip file — unzip it and you'll
   have **`SpaceDuck.ipa`**.
5. **Download it within 30 days** — GitHub deletes build artifacts
   after that. The file itself is small (a few MB).

## Step 5 — Get the .ipa onto the iPad

Pick whichever is easiest:

- **AirDrop (recommended):** on the MacBook, right-click the
  `SpaceDuck.ipa` → Share → AirDrop → your iPad. Accept it on the iPad
  and save it to **Files**.
- **Safari on the iPad:** open github.com in Safari, go to your repo →
  Actions → the green run → download the artifact. It lands in Files →
  Downloads.
- **iCloud Drive / email it to yourself:** works too, as long as it ends
  up in the Files app on the iPad.

## Step 6 — Install LocalDevVPN on the iPad

SideStore needs this helper app to install and refresh apps — it's a
loopback VPN that stays entirely on your iPad (no traffic leaves the
device). A free-signed app can't hold the VPN permission itself, so
SideStore borrows it from this App Store app.

1. Open the **App Store** on the iPad and search for **LocalDevVPN**.
2. Install it (it's free).
3. Open it, tap **Connect**, tap **Allow** when it asks to add VPN
   configurations, and enter your passcode.
4. Leave it **Connected** whenever you're installing or refreshing apps
   in SideStore.

## Step 7 — Install SideStore with iloader (MacBook, one time)

iloader is the official SideStore installer. It signs SideStore with your
free Apple ID, installs it on the iPad, and places a pairing file the
iPad uses later for on-device signing. After this step, the MacBook is
only needed again if SideStore itself expires or the pairing breaks.

1. On the MacBook, go to
   **github.com/nab138/iloader/releases/tag/v2.4.1** and download the
   **.dmg** file (it works on Intel and Apple Silicon Macs).
2. Open the .dmg and drag iloader into Applications, then open it.
3. Plug the iPad into the MacBook with the USB cable. Unlock the iPad
   and tap **Trust** on "Trust This Computer?" if it appears, then enter
   your passcode.
4. In iloader, **sign in with your Apple ID** (the same one you use on
   the iPad — the email is case-sensitive, type it exactly).
5. Select your iPad in iloader.
6. Choose **Install SideStore (Nightly)** — pick Nightly because you're
   on iPadOS 27 (or 26.4+). If you're somehow still below 26.4, pick
   **Install SideStore (Stable)**.
7. Wait for it to finish. SideStore should now be on your iPad's home
   screen.

**If iloader won't run or won't see the iPad:** this is the one step
that hasn't been tested on a 2015 MacBook Pro running macOS 12
Monterey — it *should* work (every stated requirement is below macOS
12), but if it doesn't, don't fight it: use the **Sideloadly fallback**
in the troubleshooting section instead. Same Apple ID, same 7-day
limits, just no on-device refresh — you'd re-install from the Mac each
week.

## Step 8 — Trust the developer certificate

1. On the iPad, go to **Settings → General → VPN & Device Management**.
2. Under **Developer App**, tap your Apple ID.
3. Tap **Trust "[your email]"**, then confirm **Trust**.

## Step 9 — Turn on Developer Mode

Apple requires this for any app signed with a developer certificate
(free or paid) on iOS/iPadOS 16 and later. It does not jailbreak the
device.

1. Go to **Settings → Privacy & Security**.
2. Scroll all the way to the bottom → **Developer Mode**.
3. Toggle it **ON** → tap **Restart**.
4. After the iPad reboots, unlock it and tap **Turn On** on the
   confirmation alert, then enter your passcode.

**If you don't see the Developer Mode toggle:** it only appears after a
developer-signed app has been installed — finish Step 7 first, then
look again.

## Step 10 — Set up SideStore itself

1. Make sure **LocalDevVPN is Connected** and the iPad is on Wi-Fi.
2. Open **SideStore** and sign in with the **same Apple ID** you used in
   iloader.
3. If SideStore asks for a pairing file on first open: tap through to
   **Files → On My iPad → SideStore** and pick
   **`ALTPairingFile.mobiledevicepairing`** — iloader created that file
   when it installed SideStore. If it's not there, plug the iPad back
   into the MacBook and re-run the iloader install, or in iloader use
   **Manage Pairing File → Place** next to SideStore.
3. Go to **My Apps** and tap the **7 DAYS** counter next to SideStore —
   this does the first refresh. Accept if it asks to revoke or create a
   signing certificate.
4. You should now see SideStore listed with a 7-day countdown.

## Step 11 — Install SpaceDuck

1. In SideStore, go to **My Apps** → tap **+**.
2. Pick **`SpaceDuck.ipa`** from Files.
3. If the picker opens empty: first check the file is actually an
   **.ipa** — the GitHub artifact downloads as a **.zip**, and the
   picker only shows .ipa files. In the Files app, long-press the zip
   → **Uncompress**. Then move the `SpaceDuck.ipa` into **On My iPad →
   SideStore** in Files and pick it from there — the picker opens in
   SideStore's own folder, so that's the surest place for it to see
   the file.
4. **Skip the picker entirely (easiest):** in the Files app,
   long-press `SpaceDuck.ipa` → **Share** → **SideStore**. That sends
   the file straight to SideStore for install — no picker involved. If
   SideStore isn't in the share list, tap **More** in the share sheet
   and turn it on.
3. Wait while SideStore signs it with your Apple ID and installs it.
4. If it asks about **app extensions** (the AUv3 part), choose **Use
   Main Profile** — this keeps the extension on the same signing profile
   as the app instead of using up an extra App ID.
5. Open the **SpaceDuck** app once. It just shows a short explanation
   screen — that's normal, it's the container app. Opening it once also
   registers the AUv3 with iPadOS so hosts can find it.

## Step 12 — Load it in Cubasis (main setup)

This is the standard sidechain path — one instance, no linking needed.

1. Open **Cubasis 3**. Make sure the vocal is on its own track and the
   beat/music is on another track.
2. Select the **beat track** (the one you want ducked).
3. Open the **Inspector → Insert Effects → Audio Units** and load
   **SpaceDuck** as an insert.
4. Open the **Effect Rack** for that track. You should see a
   **side-chaining button** in the rack's toolbar — it only appears
   because SpaceDuck declares a sidechain input. (If it doesn't appear,
   the bus declaration is wrong — tell Meta, don't keep tapping.)
5. Tap the side-chaining button and **pick the vocal track** from the
   list. Tap the button again to disable; tap the small arrow to change
   to a different source track.
6. In the Inspector, the effect's slot now shows the source track, a
   **gain slider** for the sidechain level, and a **pre/post** icon
   (pre-fader takes the vocal before the track volume; post-fader after
   it — try pre first).
7. Open the SpaceDuck UI inside Cubasis:
   - **Mode:** Sidechain.
   - **Amount:** start around 70%.
   - **Attack:** 10 ms. **Release:** 150 ms. **Threshold:** −40 dB.
   - Turn **Listen** on briefly to check you're hearing the vocal (the
     trigger) — then turn it off.
8. Play the track. The beat should dip in the vocal's frequencies while
   the vocal is there and come back when it stops. Adjust Amount up for
   more carving, Threshold down if it's ducking on quiet parts.

**AUM (fallback host):** load SpaceDuck as an effect on the ducked
channel. On the trigger channel, add a **"Multi-bus Audio Unit
instances"** node for SpaceDuck, set its **"Input to Plugin Bus"** to
the sidechain bus, and set its output to None so the trigger audio
isn't duplicated.

## Step 13 — LINK mode (only if a host can't route the sidechain)

GarageBand, for example, has no sidechain routing. For hosts like that,
use two instances:

1. On the **vocal track**, load SpaceDuck and set **Mode → Link Master**,
   **Link Ch → 1**. (It analyses the vocal and passes it through
   unducked.)
2. On the **beat track**, load SpaceDuck and set **Mode → Link Slave**,
   **Link Ch → 1** (same channel number).
3. The status line in the plugin UI tells you the state: "linked to
   Master" means it's working; "no Master found" means the channel
   numbers don't match or the Master instance isn't running.
4. If no MASTER is publishing, the SLAVE falls back to its own sidechain
   bus if one is connected — otherwise it passes the beat through clean.

## Step 14 — The weekly refresh (don't skip this)

Your free Apple ID signs apps for **7 days at a time**. When the time
runs out, the app won't open until it's re-signed. SideStore tries to do
this in the background, but background refresh is best-effort — **do it
yourself every 4–5 days**:

1. Open **SideStore** (make sure LocalDevVPN is Connected and you're on
   Wi-Fi).
2. Tap **Refresh All**. Done — the 7-day clock restarts.

**Rules to remember:**
- **3 apps max**, and SideStore itself takes one slot. So: SideStore +
  SpaceDuck = 2 of 3, with one spare slot.
- If an app already expired, **don't delete it** — just tap Refresh All.
  Reinstalling over it keeps its data; deleting loses it.
- If **SideStore itself** has expired, it can't refresh anything. Go
  back to Step 7 and reinstall it from the MacBook with iloader, then
  refresh your other apps.
- Every distinct app you install uses one of **10 App IDs per 7 days**.
  Reinstalling the same app repeatedly eats them — refresh instead of
  reinstalling whenever you can.

**What the $99/year would change (optional, not needed):** a paid Apple
Developer Program membership signs apps for about a year instead of 7
days and lifts the 3-app cap. Nothing in this guide requires it.

---

## Troubleshooting

**"Sign-in failed" in SideStore (errors like −80045 / −80046).**
Apple changed sign-in on their side in October 2026 and it broke every
free-signing tool at once; the 0.7.0-alpha/Nightly builds are the fix.
If it still fails, it's usually the anisette server (the helper that
pretends to be an Apple device during sign-in): in SideStore's settings,
try switching the anisette server (one known-working option is labelled
"SideStore (New)" / new.ani.sidestore.io), turn auto-rotation off, and
reset the adi.pb file if the setting offers it. Then try signing in
again. If that doesn't work, check the SideStore GitHub issue tracker —
new Apple-side breakages show up there first.

**SideStore 0.6.4 won't sign in at all.** That's expected — 0.6.4 is
broken and can't be fixed. Use the **0.7.0-alpha** or **Nightly** build
from the releases page instead.

**"VPN error" when installing or refreshing (iPadOS 26.4+).** Reinstall
SideStore with iloader — the VPN validation changed in 26.4 and a fresh
iloader install fixes it. Make sure LocalDevVPN is Connected first.

**iloader doesn't see the iPad.** Try a different USB cable (some cables
are charge-only), unlock the iPad, tap Trust again, and restart both the
MacBook and the iPad. If it still doesn't appear, use the Sideloadly
fallback below.

**SpaceDuck doesn't show up in Cubasis's AU list.** First, open the
SpaceDuck container app once (Step 11) — the AUv3 only registers after
the container runs. Then force-quit Cubasis and reopen it. If it's still
missing, reinstall the .ipa in SideStore (choose "Use Main Profile" for
extensions if asked) and check again after a manual Refresh All. This
specific case — a SideStore-signed AUv3 being picked up by a host — is
the one thing nobody has confirmed in writing yet, so if it doesn't work,
tell Meta exactly what you see and we'll work through it (Sideloadly is
the next thing to try).

**"Untrusted Developer" when opening SpaceDuck.** Go back to Step 8 and
trust the certificate again.

**"Developer Mode Required" when opening SpaceDuck.** Go back to Step 9
and make sure Developer Mode is on.

**Sideloadly fallback (if iloader or SideStore won't cooperate).**
Sideloadly 0.71.0 explicitly supports macOS 10.12+ (tested on Monterey)
and iOS/iPadOS 7–27+. Download it from sideloadly.io on the MacBook,
plug the iPad in, sign in with your Apple ID, drag the `SpaceDuck.ipa`
into Sideloadly, and install. The trade-off: there's no on-device
refresh — you plug the iPad into the MacBook once a week to re-sign.
Same 7-day / 3-app limits.

**AltStore (second fallback).** AltStore Classic + AltServer needs a Mac
running macOS 11 or later (Monterey qualifies). AltServer has to be
running on the same Wi-Fi to refresh. It was not verified on iPadOS 27,
so try Sideloadly first.

---

## What Meta still needs from you

- **Your exact iPadOS version** (Step 1) — this decides Stable vs
  Nightly.
- **Your Cubasis version** — sidechain routing needs Cubasis 3.3 or
  later (any version from the last few years is fine; if you're on an
  older one, update it first).
- **A GitHub account** if you don't have one (Step 2).
- After the first install: **does SpaceDuck show up in Cubasis, and
  does the sidechain button appear in the Effect Rack?** That's the
  one-device smoke test that confirms the whole design.
