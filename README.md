# QPhoneManager

QPhoneManager manages an Android phone from the Linux desktop. It reads and
edits contacts, the call log, messages, calendar, tasks and notes, backs them
up into a file and restores them, and it handles files, apps, the phone screen
and notifications.

It talks to the Android app "MyPhoneExplorer Client" over Wi-Fi, USB or
Bluetooth. QPhoneManager is an independent project and is not affiliated with
the maker of MyPhoneExplorer.

## Requirements

- On the phone: "MyPhoneExplorer Client" 4.15 or newer.
- Linux on x86_64.
- Qt 6.9 or newer with these modules: Core, Network, Concurrent, Sql,
  Bluetooth, Gui, Svg, Widgets, DBus, Qml, Quick, Quick Controls 2,
  Quick Dialogs 2, Quick Layouts, Help, Test, Quick Test and Linguist Tools.
- CMake 3.28 or newer, Ninja and a C++20 compiler.
- `qhelpgenerator` from the Qt installation the build uses. It comes with
  the Qt Help module; the build does not look for it on `PATH`.
- For USB connections: `adb` from the Android SDK Platform Tools.

## Build

The build uses the presets in `CMakePresets.json`:

| Preset | Purpose |
|---|---|
| `debug-6.9`, `debug-6.12` | Debug build against Qt 6.9 or Qt 6.12 |
| `release-6.9`, `release-6.12` | Hardened release build |
| `asan-6.9` | Debug build with AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan-6.9` | Debug build with ThreadSanitizer |

All presets treat compiler warnings as errors.

```bash
cmake --preset debug-6.9
cmake --build --preset debug-6.9
```

The presets take the Qt directory from two environment variables. Set them
before you configure:

| Variable | Used by | Example |
|---|---|---|
| `QPM_QT_6_9` | all `*-6.9` presets | `/opt/qt/6.9.3/gcc_64` |
| `QPM_QT_6_12` | all `*-6.12` presets | `/opt/qt/6.12.0/gcc_64` |

```bash
export QPM_QT_6_9=/opt/qt/6.9.3/gcc_64
export QPM_QT_6_12=/opt/qt/6.12.0/gcc_64
```

If a variable is empty, CMake cannot find Qt and the configure step stops.

Instead of the variables, you can add your own presets in
`CMakeUserPresets.json`. Git ignores this file. Example:

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "debug-local",
            "inherits": "base-debug",
            "cacheVariables": { "CMAKE_PREFIX_PATH": "/path/to/Qt/6.9.3/gcc_64" }
        }
    ],
    "buildPresets": [
        { "name": "debug-local", "configurePreset": "debug-local", "jobs": 4 }
    ],
    "testPresets": [
        { "name": "debug-local", "inherits": "base", "configurePreset": "debug-local" }
    ]
}
```

### Handbook as PDF

The target `handbook_pdf` sets the handbook as two PDF files:
`doc/handbook/QPhoneManager-Handbuch.pdf` in German and
`doc/handbook/QPhoneManager-Manual.pdf` in English. The normal build does not
run it. Run it after a change to the handbook and commit both files.

```bash
cmake --build build/debug-6.9 --target handbook_pdf
```

The target needs `pandoc` and `weasyprint` on `PATH`. Without them the
configure step still works, and only this target fails with a message. The
PDFs in the repository were made with these versions:

| Tool | Version |
|---|---|
| pandoc | 3.7.0.2 |
| WeasyPrint | 67.0 |

The template, the stylesheet and the pandoc filter lie in `doc/handbook/pdf/`.
The text font is Noto Sans and code uses Noto Sans Mono. WeasyPrint hyphenates
German text only when the German hyphenation patterns are installed. On
Ubuntu 26.04 this installs everything the target needs:

```bash
sudo apt install pandoc weasyprint hyphen-de fonts-noto-core fonts-noto-mono
```

## Help and handbook

The handbook lies in `doc/handbook/`, in German and English, one Markdown
chapter per area of the program. The build turns it into one help file per
language with the tool in `tools/helpgen` and embeds them in the program.
F1 opens the chapter of the area on screen. `doc/handbook/README.md` lists the
chapters and the rules the build checks.

## Run

```bash
build/debug-6.9/src/qphonemanager
```

## AppImage

`packaging/appimage/build-appimage.sh` builds
`dist/QPhoneManager-<version>-x86_64.AppImage` from the preset `release-6.9`.
To use another release preset, give its name as the first argument.

```bash
export QPM_QT_6_9=/opt/qt/6.9.3/gcc_64
packaging/appimage/build-appimage.sh
```

The install step of the build puts the program into `build/<preset>/AppDir`,
together with the desktop file and AppStream metadata from
`packaging/linux/` and the icon. Qt's deployment script adds the Qt
libraries, plugins and QML modules the program uses. linuxdeploy then adds
the system libraries that Qt needs and that are not part of every Linux
system. appimagetool packs the AppImage.

The script downloads these tools once into
`~/.cache/qphonemanager/appimage-tools` and checks each one against a fixed
SHA-256 sum on every run. It stops when a sum does not match. Set
`QPM_APPIMAGE_TOOLS_DIR` to keep them in another directory.
`build-appimage.sh --fetch-tools` only downloads and checks them.

To sign the AppImage, set `QPM_APPIMAGE_SIGN_KEY` to the fingerprint of a
secret key in your gpg keyring. appimagetool then embeds the signature and the
public key in the AppImage. `packaging/appimage/verify-signature.sh` checks
that signature and the detached one in `<AppImage>.asc`.

| Tool | Version |
|---|---|
| linuxdeploy | 1-alpha-20251107-1 |
| appimagetool | 1.9.1 |
| AppImage runtime (type2-runtime) | 20251108 |

Building needs `curl` and `sha256sum`, but no FUSE. linuxdeploy takes the
libraries for Qt's X11 platform plugin from the build machine. On Ubuntu
26.04 this installs them:

```bash
sudo apt install libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0
```

Running the AppImage needs FUSE, that is `fusermount3` or `fusermount` on
`PATH`. Without it, start the AppImage with `--appimage-extract-and-run`.
The AppImage takes glibc and `libstdc++` from the system, so it needs at
least the versions of the machine it was built on. The release AppImage is
built on Ubuntu 22.04 and needs glibc 2.34 and the `libstdc++` of GCC 11
(`GLIBCXX_3.4.29`) or newer.

## Releases

The owner makes a release by pushing a tag `vX.Y.Z` that names the version
in `CMakeLists.txt`. A suffix such as `v1.1.0-rc.1` makes a pre-release.
The workflow `.github/workflows/release.yml` then builds and tests the
release preset, builds the AppImage, signs it and creates the GitHub release
with three files: the AppImage, its signature `<AppImage>.asc` and
`SHA256SUMS`. Started by hand, the workflow makes an unsigned test build and
keeps the AppImage as an artifact of the run.

The workflow builds in the image `ghcr.io/chmmou/qphonemanager/ci-ubuntu`
from `res/ci/ubuntu/Dockerfile`. The workflow `deps-image.yml` builds it.

The workflow needs one secret, `GPG_PRIVATE_KEY`: the owner's secret key,
ASCII-armored and without a passphrase. A tag run without it stops with an
error.

To check a downloaded AppImage, import the owner's public key and verify the
signature:

```bash
curl -fsSL https://github.com/chmmou.gpg | gpg --import
gpg --verify QPhoneManager-1.0.0-x86_64.AppImage.asc QPhoneManager-1.0.0-x86_64.AppImage
sha256sum --check --ignore-missing SHA256SUMS
```

gpg prints "Good signature" and the fingerprint of the key. Check that it is
the fingerprint of the key you imported. The AppImage also carries the
signature inside. `packaging/appimage/verify-signature.sh` checks both.

## Test

```bash
ctest --preset debug-6.9
cmake --build build/debug-6.9 --target all_qmllint
cmake --build build/debug-6.9 --target clazy
```

The test presets run without a display (`QT_QPA_PLATFORM=offscreen`). The
`clazy` target needs `clazy-standalone`; the checks it runs are listed in
`.clazy`. The test of the PIN storage starts its own `dbus-daemon` and is
skipped when that program is missing. The test of the backup archive also
checks its archives with `unzip` and with the `zipfile` module of `python3`;
each of these two checks is skipped when its program is missing.

Tests that need a phone carry the CTest label `device`. Without a phone they
report themselves as skipped. They read these environment variables:

| Variable | Meaning |
|---|---|
| `QPM_TEST_ADB` | Absolute path to `adb`. Required. |
| `QPM_TEST_SERIAL` | Serial number of the phone as `adb devices` shows it. Required. |
| `QPM_TEST_SMS_TO` | The only number a test may send a text message to. Optional. |
| `QPM_TEST_CALL_TO` | The only number a test may call. Optional. |
| `QPM_TEST_READ_ONLY` | When set, the call log test writes nothing to a phone that is not an emulator. Optional. |
| `QPM_TEST_SEND_SMS` | When `1`, the messages test sends one real text message to `QPM_TEST_SMS_TO`. Optional. |
| `QPM_TEST_CALL` | When `1`, the desktop test places one real call to `QPM_TEST_CALL_TO`. Optional. |

The contacts test writes freely only to an emulator, that is a serial that
starts with `emulator-`. On any other phone it only reads the contacts and
checks their form, and it writes nothing but one contact named
`QPM Testkontakt` with six digits, which it deletes again. It never prints or
keeps the contents of a phone's contacts.

The call log test works the same way. On an emulator it writes, reads, filters
and deletes entries of its own; where the app on the phone may not change the
call log, it writes and deletes them through adb instead. On any other phone it
reads the three call lists and checks their form and count. It writes nothing
there but one entry named `QPM Testanruf` with six digits and a made-up number,
which it deletes again, and only when the app on the phone may change the call
log. It never prints or keeps the contents of a phone's call log.

The messages test lets the emulator receive text messages of its own through
the emulator console (`adb emu sms send`), which sends nothing. It then reads,
searches, archives and exports them, asks the app on the phone to delete them
and to change their read state, and removes them again through adb. To do
that it allows the Android shell of the emulator to write messages. On any
other phone it only reads the messages and checks their count and form, in a
temporary store that is gone after the test. Only with `QPM_TEST_SEND_SMS=1`
and on a phone that is not an emulator does it send one text message,
`QPM Test` with six digits, to the number in `QPM_TEST_SMS_TO` and check that
it shows among the sent messages. The app on the phone cannot delete text
messages, so that message stays on the phone. Scheduled messages are written
only to the fake phone of the other tests.

The files test writes only into a folder `QPM-Test-` with eight hex digits
below `Download`, which it creates and deletes again; such folders left over
from an earlier run are removed first. There it copies a few small files and
a picture of its own in both directions, renames and deletes them and asks
for the thumbnail of the picture. On an emulator it also copies a file of
100 MB both ways and cancels a large upload. On any other phone it only counts
the storages and the top level of the internal storage. The app on the phone
needs access to all files; where the phone has not granted it, the test is
skipped rather than the permission asked for on the phone.

The apps test counts the apps on every phone, checks their form and reads the
icon of MyPhoneExplorer Client. On an emulator it also opens Settings, saves
the APK of the smallest preinstalled app and compares it with the file on the
phone, and installs and removes a test app `org.qphonemanager.devicetest`,
confirmed on the emulator's screen through adb. It builds that app with the
build tools and a platform of the Android SDK next to `adb`; without them it
skips installing and removing. On any other phone it opens, saves, installs
and removes nothing and prints only counts.

The screen test reads on every phone only whether the accessibility service
of MyPhoneExplorer Client is on. On an emulator it also shows the screen: it
confirms the question of the phone through adb, waits for a picture of the
size of the display, presses Back on the phone and checks through adb that
Settings, which it opened, is left, and saves a screenshot. Where the service
is off on the emulator, the test switches it on through adb and puts the
setting back afterwards. On any other phone it starts no capture and sends no
input: the capture asks the owner of the phone, and input acts on whatever it
shows.

The desktop test reads on every phone whether MyPhoneExplorer Client may read
the notifications, and with that access it counts them and checks their form.
It never prints their content. On an emulator it also writes the clipboard
from this computer and reads it back through the app on the phone, and puts
back what was there before. It posts a notification of its own with
`cmd notification post`, waits for it with its title and text, and dismisses
it. Where the app on the emulator may not read notifications, the test allows
it with `cmd notification allow_listener` and takes that back afterwards. It
lets the emulator ring through its console (`adb emu gsm call`), which calls
nobody, checks that the call shows with its number, and ends it. On any other
phone it reads only: no clipboard, no dismissing, no action of a notification
and no setting. Only with `QPM_TEST_CALL=1` and on a phone that is not an
emulator does it call the number in `QPM_TEST_CALL_TO`, check that the call
shows, and hang up after at most ten seconds. It never prints that number.

The backup test reads every area of the phone into a temporary store, backs
it up into a temporary folder, reads the file back and compares it with what
it read. It prints only the number of entries per area and deletes the file.
On an emulator it also writes a contact `QPM Testkontakt` with six digits and,
where a calendar takes new appointments, an appointment `QPM Testtermin` with
six digits. It backs them up, deletes them, checks that the backup offers them
as new, restores them, compares their fields and deletes them again. On any
other phone it restores and writes nothing.

All traffic of these tests goes through a guard. It lets a text message
through only to the number in `QPM_TEST_SMS_TO` and a call only to the number
in `QPM_TEST_CALL_TO`. Without these variables, every message and every call
is refused. Answering a call is always refused; hanging up is allowed, so a
call test can end its own call. The commands that capture or control the
screen, dismiss notifications or write the clipboard pass only to an emulator.
Running an action of a notification is always refused. Keep the numbers out
of the repository, for example in a file outside it that you load before the
run.

```bash
QPM_TEST_ADB=$HOME/Android/Sdk/platform-tools/adb QPM_TEST_SERIAL=emulator-5554 \
    ctest --preset debug-6.9 -L device
```

## License

QPhoneManager is released under the BSD 3-Clause License, see `LICENSE`.
Third-party components and their licenses are listed in `THIRD_PARTY.md`.
