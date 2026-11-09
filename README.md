# TFCC: Team Fortress Classic Competitive Edition

TFCC is a GoldSrc mod built on [tf15-client](https://github.com/FWGS/tf15-client)
that fixes TFC bugs which can't be fixed by server plugins alone, while staying
100% compatible with everything that works on stock TFC (maps, AMXX plugins,
models, sprites, sounds).

* [docs/FIXES.md](docs/FIXES.md): every known bug (officialtfc.win, tfc_fixes_mm 1.2.0), where it is fixed, and what changed in the client.
* [docs/TESTING.md](docs/TESTING.md): the regression pipeline and the custom files needed for it.
* `server/tfcc_mm`: the server companion metamod plugin that lets the server opt clients into client-side fixes.
* `tfcc_dist/`, `scripts/package_tfcc.sh`: the `tfcc` mod folder (`fallback_dir "tfc"`).

Build for GoldSrc (Linux):

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchains/i386-linux-gnu.cmake -DGOLDSOURCE_SUPPORT=ON -DGOLDSOURCE_DEFAULT_FLAGS=ON -DBUILD_MENU=OFF
cmake --build build
tests/pm/run.sh && tests/abi/check.sh build/cl_dll/client.so
```

Upstream tf15-client README follows.

---

# TF15-Client [![Build Status](https://github.com/Velaron/tf15-client/actions/workflows/build.yml/badge.svg)](https://github.com/Velaron/tf15-client/actions) <img align="right" width="128" height="128" src="https://github.com/Velaron/tf15-client/raw/master/android/app/src/main/ic_launcher-playstore.png" alt="TF15-Client" />
Team Fortress Classic reverse-engineered.

## Donate
[![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/velaron)

[Support](https://www.buymeacoffee.com/velaron) me on Buy Me A Coffee, if you like my work and would like to support further development goals, like a working server implementation and reverse-engineering other great mods.

## Download
You can download a build at the [Releases](https://github.com/Velaron/tf15-client/releases/tag/continuous) section, or use these links:
* [Android](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client.apk)
* [Linux](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client_linux.tar.gz) (not recommended, just use the Steam version)
* [Windows](https://github.com/Velaron/tf15-client/releases/download/continuous/tf15-client_win32.zip) (not recommended, same as above)

## Installation
To run TF15-Client you need the latest developer build of Xash3D FWGS, which you can get [here](https://github.com/FWGS/xash3d-fwgs/releases/tag/continuous).
You have to own the [game on Steam](https://store.steampowered.com/app/20/Team_Fortress_Classic/) and copy `valve` and `tfc` folders into your Xash3D FWGS directory.
After that, just install the APK and run.

## Building
Clone the source code:
```
git clone https://github.com/Velaron/tf15-client --recursive
```
### Windows
```
cmake -A Win32 -S . -B build
cmake --build build --config Release
```
### Linux
```
cmake -S . -B build
cmake --build build --config Release
```
### Android
```
cd android
./gradlew assembleRelease
```
