#!/usr/bin/env bash
# Builds dist/QPhoneManager-<version>-x86_64.AppImage from a release build.
#
# Usage: packaging/appimage/build-appimage.sh [configure-preset]
#
# The preset defaults to release-6.9 and reads the Qt directory from the
# environment variable it names, for example QPM_QT_6_9. The Qt part of the
# AppImage comes from the install step of the build, which runs Qt's
# deployment script. linuxdeploy then adds the libraries of the system that
# Qt and its plugins need, and appimagetool packs the result.
#
# The tools are fixed releases. They are downloaded once into a cache outside
# the repository and checked against the SHA-256 sums below on every run.
set -euo pipefail

linuxdeploy_url="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
appimagetool_url="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
appimagetool_sha256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
# Passed to appimagetool explicitly; without it, appimagetool downloads the
# latest runtime on its own.
runtime_url="https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64"
runtime_sha256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

app_id="io.github.chmmou.QPhoneManager"
preset="${1:-release-6.9}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "$script_dir/../.." && pwd)"
build_dir="$source_dir/build/$preset"
appdir="$build_dir/AppDir"
dist_dir="$source_dir/dist"
cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/qphonemanager/appimage-tools"

fail() {
    echo "build-appimage.sh: $*" >&2
    exit 1
}

# fetch <url> <sha256> <file name>: prints the path of the verified file in
# the cache.
fetch() {
    local url="$1" sha256="$2" file="$cache_dir/$3"
    if [ ! -f "$file" ]; then
        mkdir -p "$cache_dir"
        curl --fail --location --silent --show-error --proto '=https' \
            --output "$file.part" "$url" || fail "cannot download $url"
        if ! echo "$sha256  $file.part" | sha256sum --check --status; then
            rm -f "$file.part"
            fail "checksum mismatch for $url"
        fi
        mv "$file.part" "$file"
    elif ! echo "$sha256  $file" | sha256sum --check --status; then
        fail "checksum mismatch for $file; remove the file to download it again"
    fi
    chmod +x "$file"
    echo "$file"
}

for tool in cmake curl sha256sum; do
    command -v "$tool" >/dev/null || fail "$tool not found"
done
[ "$(uname -m)" = "x86_64" ] || fail "only x86_64 is supported"

linuxdeploy="$(fetch "$linuxdeploy_url" "$linuxdeploy_sha256" linuxdeploy-1-alpha-20251107-1-x86_64.AppImage)"
appimagetool="$(fetch "$appimagetool_url" "$appimagetool_sha256" appimagetool-1.9.1-x86_64.AppImage)"
runtime="$(fetch "$runtime_url" "$runtime_sha256" type2-runtime-20251108-x86_64)"

cd "$source_dir"
cmake --preset "$preset"
# Qt's QML import scan runs again only when its own inputs change, and the
# QML sources of qpm_ui are not among them. Removing its result makes the
# build scan again, so a new import reaches the AppImage.
rm -f "$build_dir/src/.qt/qml_imports/qphonemanager_build.cmake"
cmake --build --preset "$preset"
[ -f "$build_dir/CMakeCache.txt" ] || fail "no build directory at $build_dir"
version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")"
[ -n "$version" ] || fail "no project version in $build_dir/CMakeCache.txt"

rm -rf "$appdir"
cmake --install "$build_dir" --prefix "$appdir/usr"
# The import scan of Qt's deployment script depends on a property Qt does not
# document; without the Qt Quick modules the program would not start.
[ -f "$appdir/usr/qml/QtQuick/Controls/qmldir" ] || fail "the install step deployed no Qt Quick Controls"

# The tools are AppImages themselves; extracting them avoids the need for
# FUSE on the build machine.
export APPIMAGE_EXTRACT_AND_RUN=1

# linuxdeploy follows the executable and usr/lib by itself. The Qt plugins
# and QML plugins are loaded at run time, so their dependencies are named
# here.
"$linuxdeploy" \
    --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/$app_id.svg" \
    --deploy-deps-only "$appdir/usr/plugins" \
    --deploy-deps-only "$appdir/usr/qml"

mkdir -p "$dist_dir"
output="$dist_dir/QPhoneManager-$version-x86_64.AppImage"
rm -f "$output"
ARCH=x86_64 "$appimagetool" --runtime-file "$runtime" "$appdir" "$output"
echo "Built $output"
