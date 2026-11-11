# QPhoneManager

QPhoneManager manages an Android phone from the Linux desktop. It reads and
edits contacts, the call log, messages, calendar, tasks and notes, and it
handles files, apps, the phone screen and notifications.

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

## Run

```bash
build/debug-6.9/src/qphonemanager
```

## Test

```bash
ctest --preset debug-6.9
cmake --build build/debug-6.9 --target all_qmllint
cmake --build build/debug-6.9 --target clazy
```

The test presets run without a display (`QT_QPA_PLATFORM=offscreen`). The
`clazy` target needs `clazy-standalone`; the checks it runs are listed in
`.clazy`. The test of the PIN storage starts its own `dbus-daemon` and is
skipped when that program is missing.

Tests that need a phone carry the CTest label `device`. Without a phone they
report themselves as skipped. They read these environment variables:

| Variable | Meaning |
|---|---|
| `QPM_TEST_ADB` | Absolute path to `adb`. Required. |
| `QPM_TEST_SERIAL` | Serial number of the phone as `adb devices` shows it. Required. |
| `QPM_TEST_SMS_TO` | The only number a test may send a text message to. Optional. |
| `QPM_TEST_CALL_TO` | The only number a test may call. Optional. |

The contacts test writes freely only to an emulator, that is a serial that
starts with `emulator-`. On any other phone it only reads the contacts and
checks their form, and it writes nothing but one contact named
`QPM Testkontakt` with six digits, which it deletes again. It never prints or
keeps the contents of a phone's contacts.

All traffic of these tests goes through a guard. It lets a text message
through only to the number in `QPM_TEST_SMS_TO` and a call only to the number
in `QPM_TEST_CALL_TO`. Without these variables, every message and every call
is refused. Answering a call is always refused; hanging up is allowed, so a
call test can end its own call. Keep the numbers out of the repository, for
example in a file outside it that you load before the run.

```bash
QPM_TEST_ADB=$HOME/Android/Sdk/platform-tools/adb QPM_TEST_SERIAL=emulator-5554 \
    ctest --preset debug-6.9 -L device
```

## License

QPhoneManager is released under the BSD 3-Clause License, see `LICENSE`.
Third-party components and their licenses are listed in `THIRD_PARTY.md`.
