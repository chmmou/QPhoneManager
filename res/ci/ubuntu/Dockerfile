# The image the release run builds, tests and packs the AppImage in. It
# carries the compiler, CMake, Qt with the modules the project links, the
# system libraries linuxdeploy copies into the AppDir, gpg, and the three
# AppImage tools.
#
# The base is the oldest Ubuntu that still meets the build. linuxdeploy takes
# the system libraries from the build machine and the program links against
# its C library, so this base sets the oldest Linux the AppImage starts on.
#
# Build it with the AppImage script as a second context:
#   docker buildx build --build-context appimage=packaging/appimage res/ci/ubuntu
#
# The values below stand before the first FROM so that a stage which needs one
# can re-declare the name without a default.

# The minimum the project declares.
ARG QT_VERSION=6.9.3

# aqtinstall checks every Qt archive against the checksum the Qt server lists.
ARG AQTINSTALL_VERSION=3.3.0

# The CMake of Ubuntu 22.04 is older than the 3.28 the presets require. The
# checksum is written here rather than read from the file beside the archive,
# which travels the same way as the archive and would confirm whatever arrives.
ARG CMAKE_VERSION=3.31.12
ARG CMAKE_SHA256=0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c

FROM ubuntu:22.04@sha256:74b12b11ffa550fba1929ad4d8d16533c34f6612728469671afba1b03b66881c

ARG QT_VERSION
ARG AQTINSTALL_VERSION
ARG CMAKE_VERSION
ARG CMAKE_SHA256

ENV DEBIAN_FRONTEND=noninteractive

# The build needs the compiler, Ninja and Python for aqtinstall. The Qt
# libraries need the X protocol, OpenGL and font libraries even under the
# offscreen platform, and linuxdeploy copies the ones the xcb platform plugin
# needs into the AppImage. appimagetool calls file, and gpg signs. dbus,
# unzip and python3 are there for tests that skip without them. QTimeZone
# reads the zone database from tzdata; without it every zone but UTC is
# unknown and the calendar tests fail.
#
# The project does not use xkbcommon. Qt Quick Layouts asks Qt for its private
# Gui component, whose configuration looks for the xkbcommon headers and
# reports the component as absent without them.
RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    build-essential \
    ca-certificates \
    curl \
    dbus \
    file \
    fontconfig \
    fonts-dejavu-core \
    git \
    gnupg \
    gpg-agent \
    libdbus-1-3 \
    libegl1 \
    libfontconfig1 \
    libfreetype6 \
    libgl1-mesa-dev \
    libglib2.0-0 \
    libx11-xcb1 \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xinerama0 \
    libxcb-xkb1 \
    libxcb1 \
    libxkbcommon-dev \
    libxkbcommon-x11-0 \
    libxkbcommon0 \
    ninja-build \
    python3 \
    python3-venv \
    tzdata \
    unzip \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN curl --fail --location --silent --show-error --proto '=https' \
        --output /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && echo "${CMAKE_SHA256}  /tmp/cmake.tar.gz" | sha256sum --check - \
    && tar -xzf /tmp/cmake.tar.gz -C /usr/local --strip-components=1 \
    && rm /tmp/cmake.tar.gz

# The base archives without qtwayland, which the AppImage does not ship, and
# Qt Connectivity for Bluetooth. Qt Help and qhelpgenerator are part of
# qttools, Qt SVG of qtsvg.
RUN cd /tmp \
    && python3 -m venv /tmp/aqt \
    && /tmp/aqt/bin/pip install --no-cache-dir "aqtinstall==${AQTINSTALL_VERSION}" \
    && /tmp/aqt/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 \
        --outputdir /opt/qt \
        --archives icu qtbase qtdeclarative qtsvg qttools qttranslations \
        --modules qtconnectivity \
    && rm -rf /tmp/aqt /tmp/aqtinstall.log

# The versions and checksums of linuxdeploy, appimagetool and the AppImage
# runtime live in the script alone. It fetches them here, and on every later
# run it checks the files against the same checksums.
ENV QPM_APPIMAGE_TOOLS_DIR=/opt/appimage-tools
COPY --from=appimage build-appimage.sh /tmp/build-appimage.sh
RUN /tmp/build-appimage.sh --fetch-tools && rm /tmp/build-appimage.sh

# Qt expects a UTF-8 locale and warns on every start without one. C.UTF-8 is
# part of the base image.
ENV LANG=C.UTF-8
# The offscreen plugin draws, the minimal one does not, and the tests of the
# ui layer need the drawing operations.
ENV QT_QPA_PLATFORM=offscreen
ENV QT_VERSION=${QT_VERSION}
# The release-6.9 preset reads the Qt directory from this variable.
ENV QPM_QT_6_9=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH="/opt/qt/${QT_VERSION}/gcc_64/bin:${PATH}"

CMD ["/bin/bash"]
