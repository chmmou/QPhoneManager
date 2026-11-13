#!/usr/bin/env bash
# Checks both signatures of a signed AppImage against the keyring of gpg: the
# detached one in <AppImage>.asc and the one appimagetool embeds.
#
# Usage: packaging/appimage/verify-signature.sh <AppImage> <fingerprint>
#
# The fingerprint is the one of the primary key that has to have made both
# signatures. A valid signature by any other key fails the check.
#
# appimagetool signs the SHA-256 of the AppImage as a hex string, taken while
# the sections .sha256_sig and .sig_key were still zero, and then writes the
# armored signature into .sha256_sig and the public key into .sig_key. The
# check zeroes both sections in a copy and verifies against that digest.
set -euo pipefail

fail() {
    echo "verify-signature.sh: $*" >&2
    exit 1
}

[ $# -eq 2 ] || fail "usage: verify-signature.sh <AppImage> <fingerprint>"
appimage="$1"
fingerprint="$(echo "$2" | tr -d ' ' | tr '[:lower:]' '[:upper:]')"
[ -f "$appimage" ] || fail "$appimage not found"
[ -f "$appimage.asc" ] || fail "$appimage.asc not found"
for tool in dd gpg readelf sha256sum; do
    command -v "$tool" >/dev/null || fail "$tool not found"
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# check <signature> <signed file> <what>
check() {
    local status
    status="$(gpg --batch --status-fd 1 --verify "$1" "$2" 2>/dev/null)" ||
        fail "$3: the signature does not verify"
    grep -Eq "^\[GNUPG:\] VALIDSIG( [^ ]+){9} $fingerprint\$" <<<"$status" ||
        fail "$3: not signed by $fingerprint"
    echo "$3: good signature by $fingerprint"
}

# section <name>: prints the file offset and size of an ELF section in bytes.
section() {
    local fields offset size
    # Columns after the bracketed index: name, type, address, offset, size.
    fields="$(readelf --section-headers --wide "$appimage" |
        awk -v name="$1" '{ sub(/^ *\[ *[0-9]+\]/, "") } $1 == name && !found { print $4, $5; found = 1 }')"
    [ -n "$fields" ] || fail "$appimage has no section $1"
    read -r offset size <<<"$fields"
    echo "$((16#$offset)) $((16#$size))"
}

check "$appimage.asc" "$appimage" "detached signature"

sig_section="$(section .sha256_sig)"
key_section="$(section .sig_key)"
read -r sig_offset sig_size <<<"$sig_section"
read -r key_offset key_size <<<"$key_section"

dd if="$appimage" of="$work/section" iflag=skip_bytes,count_bytes \
    skip="$sig_offset" count="$sig_size" status=none
tr -d '\0' <"$work/section" >"$work/embedded.asc"
[ -s "$work/embedded.asc" ] || fail "$appimage carries no embedded signature"

cp "$appimage" "$work/unsigned"
dd if=/dev/zero of="$work/unsigned" oflag=seek_bytes conv=notrunc \
    bs="$sig_size" count=1 seek="$sig_offset" status=none
dd if=/dev/zero of="$work/unsigned" oflag=seek_bytes conv=notrunc \
    bs="$key_size" count=1 seek="$key_offset" status=none
printf '%s' "$(sha256sum "$work/unsigned" | cut -d ' ' -f 1)" >"$work/digest"

check "$work/embedded.asc" "$work/digest" "embedded signature"
