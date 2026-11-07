# Phone-only build — Codespaces + Termux

This route is for a maintainer who has **only an Android phone**. A computer is not required: GitHub Codespaces supplies the Linux build host in the cloud, the phone browser controls the build, and Termux provides the local ADB bridge back to the same Android device.

The only game input selected manually is the user's original God of War NTSC-U ISO. The ISO, the extracted ELF/IRX, captured VU1/IOP data and generated C++ stay outside the public Git checkout.

> **Validation status:** the Codespaces environment, authenticated builder endpoint and Termux bridge are now in the repository and covered by host-side tests. The complete workflow still needs its first end-to-end run on the user's phone. In particular, ADB self-connection over Wireless debugging depends on the Android/ColorOS build and is not guaranteed on every device.

## 1. Open the cloud builder from the phone

1. Open the public repository in the phone browser: [Gow_android](https://github.com/CarlosCosta07-s/Gow_android).
2. In **Code → Codespaces**, create a Codespace from the `main` branch. Use the configured dev container when GitHub asks.
3. Wait for the terminal/editor to finish creating the environment. The dev-container setup installs JDK 17, Gradle 9.6.0, Android SDK platform 37, build-tools 37.0.0, NDK 30.0.16248370, CMake 3.22.1, Ninja and the Vulkan shader compiler.
4. In the Codespaces terminal, run:

```bash
cd /workspaces/Gow_android
python tools/phone_only_builder.py
```

The terminal prints a private bridge token and the forwarded HTTPS URL for port **8765**. Keep the Codespace; its home directory contains the private ISO workspace and the Debug signing key used for subsequent builds.

The forwarded port is public so Termux can send capture data to it. **Every data and build endpoint requires the random Bearer token.** Do not share the token or paste it into a public issue, commit, chat or screenshot. The landing page itself contains no game data.

## 2. Upload the original ISO and build the first APK

1. Open the printed forwarded URL in a browser tab on the same phone.
2. Paste the bridge token shown in the Codespaces terminal.
3. Select the original NTSC-U God of War ISO from the phone's file picker and tap **Enviar ISO original**.
4. Tap **Iniciar / repetir compilação**. Keep the Codespace open while the build runs.
5. When the page reports success, tap **Baixar APK ARM64**.

The first APK is an **EE AOT capture-bootstrap build**, not a completed gameplay build. It contains the statically recompilable EE code generated from the ISO, but may still be missing VU1/IOP native blocks. Its purpose is to run the first guarded attempt and collect those private captures.

## 3. Set up the phone-side ADB bridge once

Install the current Termux build from its official [F-Droid](https://f-droid.org/packages/com.termux/) or [GitHub installation instructions](https://github.com/termux/termux-app#installation). The deprecated Google Play build is not supported by the Termux project.

In Termux, install only the bridge utilities:

```bash
pkg update
pkg upgrade
pkg install python git android-tools
termux-setup-storage
git clone https://github.com/CarlosCosta07-s/Gow_android.git
cd Gow_android
```

On Android, enable **Developer options → Wireless debugging**. Open its **Pair device with pairing code** screen and keep the displayed pairing port, one-time code and the separate connection port available.

In Termux, pair ADB with the same phone using the values shown by Android:

```bash
python tools/termux_phone_bridge.py connect \
  --host-ip IP_MOSTRADO_NO_ANDROID \
  --pairing-port PORTA_DE_PAREAMENTO \
  --code CODIGO \
  --connection-port PORTA_DE_CONEXAO
```

Use the IPv4 address displayed on Android's Wireless debugging page as `--host-ip`; the pairing dialog shows its pairing port/code, while the main Wireless debugging page shows the separate connection port. Termux then tries to reach the phone's own Wi-Fi interface address from the same device. This self-connection is OEM-dependent and has not yet been confirmed on the user's ColorOS build. The script verifies both the ADB device and `run-as org.gowandroid.app.debug` before continuing. If it reports `unauthorized` or connection refused, do not uninstall the game; stop there and retain the verified private installation.

## 4. Install the APK without losing the installed ISO

After the browser downloads `gow-android-arm64.apk` to Downloads, use Termux:

```bash
python tools/termux_phone_bridge.py install-apk \
  --apk "$HOME/storage/downloads/gow-android-arm64.apk"
```

This calls the repository's existing Debug-signature migration tool. Before replacing the app, it backs up the private NTSC-U installation, validates the BOOT2 path and every SHA-256 entry, and keeps a verified backup in Termux's private workspace. It then installs the new APK and restores the private disc. Do not replace this with **uninstall app** or **clear storage** from Android Settings.

The Codespace's default Android Debug keystore lives in its persistent home directory. Keep using the same Codespace for later APK iterations so that the signing key remains stable. Deleting the Codespace deletes that key and the private cloud workspace.

## 5. Synchronize VU1/IOP captures without manual export

1. Open the updated FORGE app and verify the installed disc.
2. Tap **JOGAR** to start the guarded AOT attempt. The bootstrap runtime may stop at the first missing native VU1/IOP block; that is the expected capture stage, not proof of gameplay.
3. Return to Termux and send the private captures directly to the Codespace:

```bash
python tools/termux_phone_bridge.py upload-captures \
  --url "https://NOME-DO-CODESPACE-8765.app.github.dev" \
  --token "TOKEN_DO_CODESPACE"
```

Termux reads `files/gow-aot-captures` through the debug app's private `run-as` path, packages it temporarily and streams the ZIP over HTTPS to the authenticated Codespaces endpoint. The temporary ZIP is deleted from Termux after a successful upload. The user does not browse for or export ELF, IRX, microcode or capture files.

4. Return to the builder page and tap **Iniciar / repetir compilação**. When a capture ZIP is present, the server runs the existing strict VU1/IOP native generator and links the available AOT output into the next APK.
5. Download the new APK and repeat the safe Termux installation step. Repeat the capture/build cycle until the runtime reports complete native coverage and the game reaches a verified boot/menu.

The builder limits ISO uploads to 8 GiB and capture ZIP uploads to 512 MiB. The existing native-AOT generator performs the detailed path, file-count, module-hash and VU1 validation before compiling the captures.

## 6. Data, privacy and Codespaces usage

- ISO, ELF, IRX, capture ZIP, generated C++ and the cloud bridge token are stored outside the public repository. They are not committed and are not uploaded to GitHub Actions artifacts.
- The Codespaces bridge accepts only fixed ISO/capture upload endpoints and a fixed build action. It does not accept arbitrary shell commands or arbitrary filesystem paths.
- The public forwarded port requires the private Bearer token for all data, status, build and APK endpoints. Treat that token like a password.
- Keep the Codespace while iterating. Stop it when not building; do not delete it unless the verified private workspace and Debug key are no longer needed.
- GitHub documents a monthly personal Free quota of 120 core-hours (60 hours on a 2-core Codespace) and 15 GB-month storage. Usage beyond the included quota can be billed when account billing permits it; check the account's Codespaces budget before leaving a machine running: [Codespaces pricing and quotas](https://github.com/features/codespaces).

## Current acceptance criteria

- [x] Cloud setup installs the pinned Android/Gradle toolchain.
- [x] Browser builder authenticates every data/build endpoint with a random private token.
- [x] ISO and capture uploads are bounded and stored outside the repository.
- [x] The builder invokes only the existing ISO→AOT→APK pipeline.
- [x] Termux ADB bridge reads captures through the debug app's private path and streams them to Codespaces.
- [x] APK migration reuses the existing SHA-256-verified installation backup/restore process.
- [ ] First end-to-end Codespaces build from the user's ISO.
- [ ] Wireless ADB self-connection confirmed on the user's Android/ColorOS build.
- [ ] First capture upload and native VU1/IOP iteration confirmed on the user's phone.
- [ ] Boot/menu/gameplay confirmed on the device.
