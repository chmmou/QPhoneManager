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
- Contacts: list with search, letters and filters by group and account,
  details, a form for every kind of entry with photo and crop, accounts and
  groups. Only changed entries go to the phone; a contact changed on the phone
  meanwhile shows both versions to choose from.
- Duplicate contacts by number, mail address or similar name, with merging.
- Import of vCard 2.1, 3.0 and 4.0 and of tables (CSV); export as vCard 3.0
  and as a table.
- Calling a number from a contact, and handing a number over for a message.
- The sync of all areas includes the contacts and reports their changes.

### Changed

- The header shows the icon of the open area next to its title.
- The sidebar is as wide as its longest label needs.
- The profile database moves to schema version 2 with tables for contacts,
  accounts and groups; the contacts are loaded in full once after the update.
