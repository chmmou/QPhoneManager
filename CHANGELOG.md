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
- Call log: outgoing, incoming and missed calls with the names of the
  contacts, a filter by kind, search, details, deleting single calls or all
  calls in the list, export as a table, calling back, writing a message and
  taking a number into the contacts.
- Messages: text and multimedia messages as conversations with the names of
  the contacts, a search over all messages, pictures in the conversation,
  saving attachments, an archive on this computer that keeps messages the
  phone deleted, export as a table, a web page or an archive file, and reading
  such an archive file back.
- Writing text messages to one or more recipients with a count of characters
  and messages and a choice of the SIM card, and scheduled messages the phone
  sends on its own.
- New messages are announced while the phone is connected.
- Deleting messages and changing their read state are offered; where the app
  on the phone refuses, the program says so and points to the archive.
- The sync of all areas includes the messages.
- Apps: the installed apps with icon, name, package, version, size and date,
  a search and a filter for your own apps, system apps or all. An app can be
  opened on the phone, its APK saved on this computer, an APK installed from
  this computer and an app removed; the phone asks before it installs or
  removes anything.

### Changed

- The header shows the icon of the open area next to its title.
- The sidebar is as wide as its longest label needs.
- The profile database moves to schema version 2 with tables for contacts,
  accounts and groups; the contacts are loaded in full once after the update.
- The profile database moves to schema version 3 with a table for the call
  log.
- The profile database moves to schema version 4 with tables and a search
  index for the messages; the messages are loaded in full once after the
  update.
- The question before deleting calls says that the app on the phone may
  refuse it and what then happens.

### Fixed

- A contact photo that was replaced or whose contact was deleted no longer
  stays behind as a file in the profile folder.
- In the dark scheme the window behind a dialog is darkened instead of washed
  out.
- Tables for spreadsheets keep a cell that starts with =, +, -, @, a tab or a
  carriage return from running as a formula; the program reads its own tables
  back unchanged.
- The dialogs of the contacts show where the keyboard focus is when they
  open.
