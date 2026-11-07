#!/usr/bin/env bash
set -euo pipefail

# Cloud-side build environment for users who only have an Android phone.
# All game data and signing material stay in the Codespace home directory,
# never in the public Git checkout.

sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential clang cmake ninja-build git curl unzip zip python3 \
  openjdk-17-jdk glslang-tools pkg-config

export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
export ANDROID_HOME="$HOME/android-sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
mkdir -p "$ANDROID_HOME/cmdline-tools"

SDK_TOOLS_REVISION="15859902"
SDK_TOOLS_SHA256="4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583"
SDK_TOOLS_ZIP="$HOME/.cache/commandlinetools-linux-${SDK_TOOLS_REVISION}_latest.zip"
mkdir -p "$(dirname "$SDK_TOOLS_ZIP")"

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
  curl --fail --location --retry 3 \
    "https://dl.google.com/android/repository/commandlinetools-linux-${SDK_TOOLS_REVISION}_latest.zip" \
    --output "$SDK_TOOLS_ZIP"
  printf '%s  %s\n' "$SDK_TOOLS_SHA256" "$SDK_TOOLS_ZIP" | sha256sum --check --status
  SDK_STAGE="$(mktemp -d)"
  unzip -q "$SDK_TOOLS_ZIP" -d "$SDK_STAGE"
  rm -rf "$ANDROID_HOME/cmdline-tools/latest"
  mkdir -p "$ANDROID_HOME/cmdline-tools/latest"
  cp -a "$SDK_STAGE/cmdline-tools/." "$ANDROID_HOME/cmdline-tools/latest/"
  rm -rf "$SDK_STAGE"
fi

SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
yes | "$SDKMANAGER" --sdk_root="$ANDROID_HOME" --licenses >/dev/null || true
"$SDKMANAGER" --sdk_root="$ANDROID_HOME" --install \
  "platforms;android-37.0" \
  "build-tools;37.0.0" \
  "ndk;30.0.16248370" \
  "cmake;3.22.1"

GRADLE_VERSION="9.6.0"
GRADLE_HOME="$HOME/.local/gradle/gradle-$GRADLE_VERSION"
if [ ! -x "$GRADLE_HOME/bin/gradle" ]; then
  mkdir -p "$HOME/.local/gradle" "$HOME/.cache"
  GRADLE_ZIP="$HOME/.cache/gradle-$GRADLE_VERSION-bin.zip"
  curl --fail --location --retry 3 \
    "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip" \
    --output "$GRADLE_ZIP"
  curl --fail --location --retry 3 \
    "https://services.gradle.org/distributions/gradle-$GRADLE_VERSION-bin.zip.sha256" \
    --output "$GRADLE_ZIP.sha256"
  EXPECTED="$(tr -d '[:space:]' < "$GRADLE_ZIP.sha256")"
  ACTUAL="$(sha256sum "$GRADLE_ZIP" | awk '{print $1}')"
  [ "$EXPECTED" = "$ACTUAL" ] || { echo "Gradle SHA-256 mismatch" >&2; exit 1; }
  unzip -q "$GRADLE_ZIP" -d "$HOME/.local/gradle"
fi

{
  echo 'export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"'
  echo 'export ANDROID_HOME="$HOME/android-sdk"'
  echo 'export ANDROID_SDK_ROOT="$ANDROID_HOME"'
  echo 'export PATH="$HOME/.local/gradle/gradle-9.6.0/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmake/3.22.1/bin:$PATH"'
  echo 'export GOW_BUILD_JOBS="${GOW_BUILD_JOBS:-2}"'
} >> "$HOME/.bashrc"

echo "Codespaces Android build environment is ready."
echo "SDK: $ANDROID_HOME"
echo "Gradle: $GRADLE_HOME"
echo "CMake: $ANDROID_HOME/cmake/3.22.1"
echo "Build parallelism defaults to 2 jobs. The ISO and generated AOT must remain outside the repository."
