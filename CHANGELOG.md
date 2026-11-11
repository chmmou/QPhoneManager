# Changelog

All notable changes to QPhoneManager are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version
numbers follow [Semantic Versioning](https://semver.org/).

## [1.0.0] - Unreleased

### Added

- Build with CMake presets for Qt 6.9 and Qt 6.12, release hardening and
  sanitizer builds.
- Main window with the application name.
- Static analysis with clazy and QML checks with qmllint as build targets.
- German and English translation catalogues.
- Connecting a phone over Wi-Fi (network search, fixed address, QR code), USB
  and Bluetooth, with the PIN of the phone app and an option to remember it in
  the keyring.
- Overview of the connected phone: model, Android version, battery, signal,
  SIM cards, storage, display and processor load.
- A profile per phone, with renaming, removing and switching without a
  connection.
- Settings for connecting, appearance, language and the storage folder.
