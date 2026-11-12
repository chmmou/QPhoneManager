# Third-party components

QPhoneManager itself is released under the BSD 3-Clause License
(SPDX: `BSD-3-Clause`). It uses the components below. The program icon in
`res/icons/app/qphonemanager.svg` is part of QPhoneManager and carries the
same license.

| Component | Version | License (SPDX) | Location in this repository | Source |
|---|---|---|---|---|
| Qt | 6.9 or newer | `LGPL-3.0-only` | not included, linked dynamically | https://download.qt.io |
| Tabler Icons | 3.51.0 | `MIT` | `res/icons/tabler/` | https://github.com/tabler/tabler-icons |
| QR Code generator library (Project Nayuki) | 1.8.0 | `MIT` | `third_party/qrcodegen/` | https://github.com/nayuki/QR-Code-generator |

## Qt

- Used under the GNU Lesser General Public License version 3.
- QPhoneManager links to the Qt libraries dynamically. You can replace them
  with your own build of the same Qt version.
- The source code of Qt is available at https://download.qt.io.
- Modules in use: Core, Network, Concurrent, Sql, Bluetooth, Gui, Svg,
  Widgets, DBus, Qml, Quick, Quick Controls 2, Quick Dialogs 2,
  Quick Layouts, Help, Test, Quick Test and Linguist Tools. None of them is
  offered under the GPL only.
- The executable embeds the Qt translation catalogues `qtbase` and
  `qtdeclarative` for German and English, unchanged, from the Qt installation
  it is built against.
- QPhoneManager does not change the source code of Qt.

## Tabler Icons

- Version 3.51.0, release tag `v3.51.0`, outline set.
- License text: `res/icons/tabler/LICENSE`.
- The files are copied unchanged from `icons/outline/` of the tag archive
  https://github.com/tabler/tabler-icons/archive/refs/tags/v3.51.0.tar.gz.
- The release publishes no checksum. SHA-256 of the tag archive, computed on
  download: `9a8b3c0f9bac58c1fb3ccc7dd69f75772b897227a75d93f2dcfa55392a051594`.
- Cross-check: the drawing elements of every copied file match the npm package
  `@tabler/icons` 3.51.0, whose archive matched the integrity value published
  by the npm registry
  (`sha512-0pinvPZz7YeX+OSfxuEAbnEPWbocVXW5iU8h5lh41pYG8sZuQfpWECC980lxOT0clGIcNGn8xsBr+zKaRGdA/g==`).
- Files (63): address-book, alert-triangle, antenna-bars-5, apps, archive,
  arrow-back-up, arrows-sort, battery, bell, bluetooth, calendar, check,
  checklist, chevron-left, chevron-right, clipboard, clock, copy, cpu,
  database, device-desktop, device-mobile, device-sd-card, download, edit,
  external-link, file, filter, folder, folder-plus, help, home, info-circle,
  keyboard, layout-grid, list, lock, message, messages, notes, phone,
  phone-incoming, phone-outgoing, phone-x, photo, player-play, plug-connected,
  plug-connected-x, plus, qrcode, refresh, screen-share, search, send,
  settings, star, trash, upload, usb, user, users, wifi, x.

## QR Code generator library

- Version 1.8.0 by Project Nayuki, release tag `v1.8.0`
  (commit `720f62bddb7226106071d4728c292cb1df519ceb`).
- `third_party/qrcodegen/qrcodegen.hpp` and `qrcodegen.cpp` are copied
  unchanged from `cpp/` of the tag archive
  https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz.
- The release publishes no checksum. SHA-256 computed on download:

  | File | SHA-256 |
  |---|---|
  | tag archive `QR-Code-generator-v1.8.0.tar.gz` | `2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60` |
  | `qrcodegen.hpp` | `b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793` |
  | `qrcodegen.cpp` | `1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757` |

- License text: `third_party/qrcodegen/LICENSE`. The project has no separate
  license file; the text is the "License" section of its `Readme.markdown`,
  without the Markdown markup.
- Built as the static library target `qrcodegen` without the warning options
  of this project.
