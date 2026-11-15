# Third-party components

QPhoneManager itself is released under the BSD 3-Clause License
(SPDX: `BSD-3-Clause`). It uses the components below. The program icon in
`res/icons/app/qphonemanager.svg` is part of QPhoneManager and carries the
same license.

| Component | Version | License (SPDX) | Location | Source |
|---|---|---|---|---|
| Qt | 6.9 or newer; 6.9.3 in the AppImage | `LGPL-3.0-only` | not in this repository; the AppImage carries the Qt libraries and plugins, a source build links them dynamically | https://download.qt.io |
| ICU | 73.2 | see [ICU](#icu) | AppImage only | https://github.com/unicode-org/icu/releases/tag/release-73-2 |
| AppImage runtime (type2-runtime) | 20251108 | `MIT`, with statically linked parts, see [AppImage runtime](#appimage-runtime) | AppImage only | https://github.com/AppImage/type2-runtime/releases/tag/20251108 |
| System libraries of Ubuntu 22.04 | see [System libraries](#system-libraries) | see there | AppImage only | https://launchpad.net/ubuntu/jammy |
| Tabler Icons | 3.51.0 | `MIT` | `res/icons/tabler/` | https://github.com/tabler/tabler-icons |
| QR Code generator library (Project Nayuki) | 1.8.0 | `MIT` | `third_party/qrcodegen/` | https://github.com/nayuki/QR-Code-generator |

## License texts in the AppImage

The AppImage carries this file, `LICENSE` and the directory `LICENSES/` in
`usr/share/doc/qphonemanager/`:

| File | Text |
|---|---|
| `LICENSES/LGPL-3.0.txt` | GNU Lesser General Public License version 3, for Qt |
| `LICENSES/GPL-3.0.txt` | GNU General Public License version 3, which the LGPL version 3 builds on |
| `LICENSES/LGPL-2.1.txt` | GNU Lesser General Public License version 2.1, for libfuse in the AppImage runtime and for the system libraries under that license |
| `LICENSES/ICU-73.2.txt` | the license file of ICU 73.2 |
| `LICENSES/AppImage-type2-runtime.txt` | the license file of the AppImage runtime 20251108 |

The GNU texts are copied unchanged from https://www.gnu.org/licenses/, the
other two from the release tags named above. Each system library in the
AppImage carries the Debian copyright file of its package in
`usr/share/doc/<package>/copyright`.

The About dialog of the program shows the license texts of QPhoneManager,
the Tabler Icons and the QR Code generator library.

## Qt

- Used under the GNU Lesser General Public License version 3.
- QPhoneManager links to the Qt libraries dynamically, in the AppImage as in a
  source build. In the AppImage they lie in `usr/lib`, the plugins in
  `usr/plugins` and the QML modules in `usr/qml`. You can replace them with
  your own build of the same Qt version.
- The source code of Qt is available at https://download.qt.io, for the
  AppImage at https://download.qt.io/official_releases/qt/6.9/6.9.3/.
- Modules in use: Core, Network, Concurrent, Sql, Bluetooth, Gui, Svg,
  Widgets, DBus, Qml, Quick, Quick Controls 2, Quick Dialogs 2,
  Quick Layouts, Help, Test, Quick Test and Linguist Tools. None of them is
  offered under the GPL only.
- The AppImage carries further Qt libraries that these modules need at run
  time, among them Qt OpenGL, Qt Qml Models, Qt Qml Meta, Qt Qml Worker
  Script, Qt Quick Templates 2, Qt Quick Shapes, Qt Quick Effects, the Qt
  Quick Controls styles, Qt Labs Folder List Model, Qt Labs Qml Models and the
  XCB platform library, all under the same license.
- The AppImage carries the platform theme plugin
  `plugins/platformthemes/libqxdgdesktopportal.so` of Qt Base under the same
  license (`LGPL-3.0-only`). It shows the file dialogs of the desktop portal.
- The Qt libraries contain code of third parties, for example PCRE2 and
  HarfBuzz. Qt lists them with their licenses at
  https://doc.qt.io/qt-6/licenses-used-in-qt.html.
- The executable embeds the Qt translation catalogues `qtbase` and
  `qtdeclarative` for German and English, unchanged, from the Qt installation
  it is built against.
- QPhoneManager does not change the source code of Qt.

## ICU

- Version 73.2, the build that the Qt installer ships for Qt 6.9.3 on Linux.
  The AppImage carries `libicuuc`, `libicui18n` and `libicudata`, which Qt
  Core links.
- License: the Unicode license for data files and software, with further
  terms for data and code that ICU contains. The full text is
  `LICENSES/ICU-73.2.txt`, copied unchanged from `icu4c/LICENSE` of the tag
  `release-73-2`.
- Source: https://github.com/unicode-org/icu/releases/tag/release-73-2.

## AppImage runtime

- The runtime is the start program at the head of the AppImage file, release
  20251108 of https://github.com/AppImage/type2-runtime. It is under the MIT
  license (`MIT`).
- It is linked statically against these libraries:

  | Library | Version | License (SPDX) | Source |
  |---|---|---|---|
  | libfuse | 3.15.0, with the patch `patches/libfuse/mount.c.diff` of the runtime | `LGPL-2.1-only` | https://github.com/libfuse/libfuse/releases/tag/fuse-3.15.0 |
  | squashfuse | 0.5.2 | `BSD-2-Clause` | https://github.com/vasi/squashfuse/releases/tag/0.5.2 |
  | musl libc | package of Alpine Linux 3.21 | `MIT` | https://musl.libc.org |
  | zstd | package of Alpine Linux 3.21 | `BSD-3-Clause OR GPL-2.0-only` | https://github.com/facebook/zstd |
  | zlib | package of Alpine Linux 3.21 | `Zlib` | https://zlib.net |
  | mimalloc | package of Alpine Linux 3.21 | `MIT` | https://github.com/microsoft/mimalloc |

- The runtime is built from source by its own release workflow; the build
  scripts in `scripts/` of the release tag name the exact steps.
- The license file of the runtime is `LICENSES/AppImage-type2-runtime.txt`.
  It names all of these libraries except mimalloc, which the `Makefile` of the
  runtime links as well.

## System libraries

linuxdeploy copies the libraries below from the build system into `usr/lib`
of the AppImage, because Qt and its plugins need them and not every Linux
system has them. The release AppImage is built on Ubuntu 22.04. The versions
are those of the Ubuntu packages in the CI image, the source of each is the
Ubuntu source package of that name and version on
https://launchpad.net/ubuntu/jammy.

The license column names the license of the library itself. The Debian
copyright file of each package, which the AppImage carries, has the full text
and the terms of every file.

| Library | License (SPDX) | Ubuntu source package and version |
|---|---|---|
| `libbrotlicommon.so.1`, `libbrotlidec.so.1` | `MIT` | brotli 1.0.9-2ubuntu0.1 |
| `libbsd.so.0` | several BSD-style licenses and Beerware; no single SPDX expression determined, see the copyright file | libbsd 0.11.5-1 |
| `libcap.so.2` | `BSD-3-Clause OR GPL-2.0-only` | libcap2 1:2.44-1ubuntu0.22.04.3 |
| `libdbus-1.so.3` | `AFL-2.1 OR GPL-2.0-or-later` | dbus 1.12.20-2ubuntu4.1 |
| `libgcrypt.so.20` | `LGPL-2.1-or-later` | libgcrypt20 1.9.4-3ubuntu3.3 |
| `libglib-2.0.so.0`, `libgthread-2.0.so.0` | `LGPL-2.1-or-later` | glib2.0 2.72.4-0ubuntu2.11 |
| `libgssapi_krb5.so.2`, `libk5crypto.so.3`, `libkrb5.so.3`, `libkrb5support.so.0` | MIT Kerberos license with further BSD-style notices; no single SPDX expression determined, see the copyright file | krb5 1.19.2-2ubuntu0.10 |
| `libkeyutils.so.1` | `LGPL-2.0-or-later` | keyutils 1.6.1-2ubuntu3 |
| `liblz4.so.1` | `BSD-2-Clause` | lz4 1.9.3-2build2 |
| `liblzma.so.5` | public domain | xz-utils 5.2.5-2ubuntu1.2 |
| `libmd.so.0` | `BSD-3-Clause`, with parts under BSD-2-Clause, ISC, Beerware and in the public domain; see the copyright file | libmd 1.0.4-1build1 |
| `libpcre.so.3` | `BSD-3-Clause` | pcre3 2:8.39-13ubuntu0.22.04.1 |
| `libpng16.so.16` | the libpng license; no SPDX identifier determined, see the copyright file | libpng1.6 1.6.37-3ubuntu0.7 |
| `libsystemd.so.0` | `LGPL-2.1-or-later` | systemd 249.11-0ubuntu3.23 |
| `libXau.so.6` | X11-style license of The Open Group; see the copyright file | libxau 1:1.0.9-1build5 |
| `libXdmcp.so.6` | X11-style license; see the copyright file | libxdmcp 1:1.1.3-0ubuntu5 |
| `libxcb-randr.so.0`, `libxcb-render.so.0`, `libxcb-shape.so.0`, `libxcb-shm.so.0`, `libxcb-sync.so.1`, `libxcb-xfixes.so.0`, `libxcb-xkb.so.1` | X11-style license; see the copyright file | libxcb 1.14-3ubuntu3 |
| `libxcb-cursor.so.0` | X11-style license; see the copyright file | xcb-util-cursor 0.1.1-4ubuntu1 |
| `libxcb-icccm.so.4` | X11-style license; see the copyright file | xcb-util-wm 0.4.1-1.1build2 |
| `libxcb-image.so.0` | X11-style license; see the copyright file | xcb-util-image 0.4.0-2 |
| `libxcb-keysyms.so.1` | X11-style license; see the copyright file | xcb-util-keysyms 0.4.0-1build3 |
| `libxcb-render-util.so.0` | X11-style license; see the copyright file | xcb-util-renderutil 0.3.9-1build3 |
| `libxcb-util.so.1` | X11-style license; see the copyright file | xcb-util 0.4.0-1build2 |
| `libxkbcommon.so.0`, `libxkbcommon-x11.so.0` | X11-style licenses; see the copyright file | libxkbcommon 1.4.0-1 |
| `libzstd.so.1` | `BSD-3-Clause OR GPL-2.0-only` | libzstd 1.4.8+dfsg-3build1 |

The AppImage takes the C and C++ runtime, zlib, OpenGL, fontconfig, FreeType
and the X11 client library from the system it runs on; it carries none of
them.

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
- Files (74): address-book, alert-triangle, antenna-bars-5, apps, archive,
  arrow-back-up, arrows-maximize, arrows-sort, battery, bell, bell-off,
  bluetooth, calendar, camera, check, checklist, chevron-left, chevron-right,
  clipboard, clock, copy, cpu, database, device-desktop, device-floppy,
  device-mobile, device-sd-card, download, edit, external-link, file, filter,
  folder, folder-plus, help, home, info-circle, keyboard, layout-grid, list,
  lock, message, messages, microphone-off, notes, phone, phone-call,
  phone-incoming, phone-off, phone-outgoing, phone-x, photo, player-play,
  player-stop, plug-connected, plug-connected-x, plus, qrcode, refresh,
  rotate-clockwise, screen-share, search, send, settings, square, star, trash,
  upload, usb, user, users, volume, wifi, x.

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
