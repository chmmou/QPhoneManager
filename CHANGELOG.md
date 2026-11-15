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
- Notifications of the phone: a list with app, title, text, time and picture,
  dismissing a notification and running its actions on the phone, and a
  desktop notification for each new one with the same buttons.
- The clipboard can be shared between the phone and this computer.
- Incoming calls show in a window of their own with the name of the contact
  and the number; the call can be answered, declined or ended, and the
  microphone and the speaker switched.
- An icon in the notification area shows the window, starts a new message,
  ends the connection and quits; the program can start with only that icon.
- Settings for desktop notifications, the shared clipboard and the start in
  the notification area.
- Backup: contacts, calendar, tasks, notes, the call log and messages go into
  one file with the extension `.qpmbackup`, each part chosen on its own. The
  file holds the standard formats and the form of the phone app, and it is not
  encrypted, which the program says before saving.
- Restore from a backup: a preview shows per part what is new, changed or
  unchanged. Contacts, calendar, tasks and notes go back to the phone; a backup
  of another phone only adds what this phone lacks. Messages go into the
  archive on this computer, because the app on the phone does not take
  messages. The call log stays in the backup, because the app on the phone
  may not change it.
- The sidebar opens backup and restore above the settings.
- Help in the program: F1 opens the chapter of the area on screen, in the
  language of the program, with English and then German where a chapter is
  missing. The help has contents, back and forward, and links that work by
  keyboard. Links to web pages open in the browser, and only over https.
- The handbook in German and English as Markdown, built into the help during
  the build.
- An About dialog with the version, the copyright, the license, the Qt version
  in use and the components with their licenses: the full license text of
  QPhoneManager, the Tabler Icons and the QR code library, and for Qt its
  copyright, its license and where to get its source code.
- An AppImage for x86_64, built by `packaging/appimage/build-appimage.sh`, and
  a desktop file, AppStream metadata and the icon in the install step.
- Releases on GitHub: a version tag builds and tests the AppImage in a
  container image, signs it with GPG, embedded and as a separate `.asc` file,
  and publishes it with SHA-256 checksums. A manual run makes an unsigned test
  build.
- The first sync of a phone shows a window with a progress bar and the area
  being synced. It closes when the sync ends, fails or the connection ends.

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
- The German texts are shorter and more direct, and the button that starts a
  new alarm names its action.
- The AppImage carries the license texts of Qt, ICU, the AppImage runtime
  and the system libraries it brings, and `THIRD_PARTY.md` lists them.
- The release workflows name each action by its commit and the build image by
  its digest, and the image installs aqtinstall with a hash for every package.
- Messages and the sync summary close by themselves after 15 seconds. They
  stay open while the mouse pointer rests on them or the keyboard focus is in
  them.
- When a Bluetooth connection fails because the phone offers no service, the
  message names the permission "Nearby devices" of the phone app and says to
  disconnect the phone once in the Bluetooth settings of the computer. The
  handbook has the same steps.
- The AppImage no longer carries the help plugin of the QML language server
  and Qt Qml Compiler, which the program does not use.

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
- A new search for phones no longer lists the phones an earlier search found
  over Wi-Fi.
- The backup dialog shows the status of a backup only on the tab Back up and
  the status of a restore only on the tab Restore.
