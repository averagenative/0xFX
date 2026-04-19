# macOS Code Signing & Notarization

This is the one-time setup + per-release workflow for producing a macOS build of 0xFX that Gatekeeper will accept without the `xattr -cr` workaround.

Once everything here is in place, the per-release process is two commands:

```bash
./scripts/packaging/package_macos.sh 1.3.0
CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE="0xFX-notary" \
./scripts/packaging/sign_macos.sh 1.3.0
./scripts/packaging/upload_release.sh 1.3.0
```

> **Two different certificates are needed**, both issued by the same team:
> - **Developer ID Application** — signs the `.app` and the plugin bundles.
> - **Developer ID Installer** — signs the `.pkg` installer.

---

## One-time setup

### 1. Enroll in the Apple Developer Program

- $99/year at <https://developer.apple.com/programs/>.
- Individual enrollment uses your legal name; Organization enrollment requires a D-U-N-S number and shows the company name as the developer.

### 2. Create both "Developer ID" certificates

You need two separate certs, both under the same team:

- **Developer ID Application** — signs `.app` and plugin bundles.
- **Developer ID Installer** — signs `.pkg` installers.

Generate each one the same way; the only difference is which type you pick.

**Via Xcode (easiest):**
1. Open Xcode → *Settings → Accounts*.
2. Sign in with the Apple ID on your developer account.
3. Click *Manage Certificates... → +* → **Developer ID Application**, then repeat for **Developer ID Installer**.
4. Xcode installs both in your login keychain.

**Via the developer portal:**
1. Generate a CSR from *Keychain Access → Certificate Assistant → Request a Certificate From a Certificate Authority* ("Saved to disk").
2. Upload it at <https://developer.apple.com/account/resources/certificates> → **+ → Developer ID Application**. Repeat for **Developer ID Installer** (can reuse one CSR or create a new one).
3. Download both `.cer` files and double-click each to install in Keychain.

### 3. Verify both identities are usable

```bash
security find-identity -v -p codesigning | grep "Developer ID"
```

You should see two entries:

```
1) XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX "Developer ID Application: Your Name (TEAMID)"
2) YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY "Developer ID Installer: Your Name (TEAMID)"
```

The Application string becomes `CODESIGN_IDENTITY`; the Installer string becomes `INSTALLER_IDENTITY`. The 10-character `TEAMID` inside the parens is your Apple Team ID.

### 4. Create an app-specific password for notarization

Notarization uploads your build to Apple's service, which scans it and returns a ticket. The upload is authenticated with an **app-specific password**, not your Apple ID password.

1. Go to <https://account.apple.com/> → *Sign-In and Security → App-Specific Passwords*.
2. Generate one labeled `0xFX notarytool` (or similar).
3. Copy the `xxxx-xxxx-xxxx-xxxx` string.

### 5. Store notarization credentials in the keychain

This lets `notarytool` authenticate without prompting on every release:

```bash
xcrun notarytool store-credentials "0xFX-notary" \
    --apple-id "you@example.com" \
    --team-id "YOURTEAMID" \
    --password "xxxx-xxxx-xxxx-xxxx"
```

`0xFX-notary` is the keychain profile name — pass it later as `NOTARY_PROFILE`.

---

## Per-release workflow

### 1. Build

```bash
./scripts/packaging/package_macos.sh 1.3.0
```

This produces an **unsigned** `.app`, plugin bundles, a `.pkg` installer, and a `.zip` under `release/`. Without the signing step, end users see Gatekeeper warnings and need the `xattr -cr` workaround.

### 2. Sign + notarize + staple

```bash
CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE="0xFX-notary" \
./scripts/packaging/sign_macos.sh 1.3.0
```

The script:
- Signs the three plugin bundles (`.clap`, `.vst3`, `.component`) with hardened runtime and a secure timestamp.
- Signs `0xFX.app` with hardened runtime + entitlements from `resources/macos/0xfx.entitlements` (microphone access, library-validation off for SDL2, JIT for ImGui/DSP).
- Rebuilds `release/0xFX-1.3.0-macos-universal.pkg` via `build_pkg_macos.sh` so its payload contains the signed bundles, and signs the `.pkg` itself with the Installer identity.
- Submits the `.pkg` to Apple's notary service (~1–5 min) and waits.
- Staples the notarization ticket onto the `.pkg`, the `.app`, and each plugin — so Gatekeeper can verify them offline.
- Re-zips `release/0xFX-1.3.0-macos-universal.zip` with the stapled contents for the portable-install path.

Leave `NOTARY_PROFILE` unset to sign locally without notarizing (useful for testing the signing step before you have credentials stored). Leave `INSTALLER_IDENTITY` unset to produce an unsigned `.pkg` — Gatekeeper will still warn on first open, but it's fine for internal testing.

### 3. Upload

```bash
./scripts/packaging/upload_release.sh 1.3.0
```

Once a signed + notarized build is uploaded, you can remove the Gatekeeper workaround section from the release notes.

---

## Entitlements rationale

Stored in `resources/macos/0xfx.entitlements`:

| Entitlement | Why |
| --- | --- |
| `com.apple.security.device.audio-input` | Live guitar input in the standalone app. Without it, notarized builds silently get no audio input. |
| `com.apple.security.cs.disable-library-validation` | Hardened runtime otherwise rejects loading SDL2 (not signed by your team ID). |
| `com.apple.security.cs.allow-jit` | ImGui shader cache and some DSP paths allocate writable+executable memory. |
| `com.apple.security.cs.allow-unsigned-executable-memory` | Same reason as JIT — harmless to include, avoids edge-case crashes on older SDKs. |

Tighten these later if profiling shows the JIT entitlements aren't exercised.

---

## Troubleshooting

**`codesign: errSecInternalComponent`** — the signing identity isn't unlocked. Run `security unlock-keychain login.keychain` or open Keychain Access and unlock the login keychain.

**Notarization succeeds but the app still shows the warning** — stapling didn't complete. Re-run `xcrun stapler staple release/0xFX.app` and re-zip. You can verify a stapled ticket with `stapler validate release/0xFX.app`.

**`spctl --assess` prints "source=Unnotarized Developer ID"** — the app is signed but not notarized. Either notarization hasn't completed yet, or `NOTARY_PROFILE` wasn't set on the signing run.

**`notarytool submit` returns `Invalid`** — fetch the detailed log:

```bash
xcrun notarytool log <submission-id> --keychain-profile 0xFX-notary
```

Most common causes are missing hardened runtime (fixed by `--options runtime`, which `sign_macos.sh` passes) or un-timestamped signatures (fixed by `--timestamp`).

**Plugins show up in the DAW but fail to load after notarization** — plugin bundles need to be signed *and* stapled. The script does both; if you signed manually, make sure each bundle was stapled individually.
