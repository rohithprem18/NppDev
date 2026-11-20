#!/usr/bin/env bash
# NppDev installer for Ubuntu (and other Debian-based Linux). Paste into a terminal:
#
#     curl -fsSL https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.sh | bash
#
# It asks GitHub for the latest release, downloads its .deb, checks it against the SHA-256 that
# GitHub publishes for that file, and installs it with apt (which asks for your password and adds
# the system libraries NppDev needs). Running it again updates NppDev.
# Remove NppDev with:  sudo apt remove nppdev
#
# Optional:  NPPDEV_DEB=/path/to/NppDev_x.y.z_amd64.deb  installs that file instead of downloading.
set -euo pipefail

repo="rohithprem18/NppDev"
say() { printf '  %s\n' "$*"; }
fail() { printf '  \033[31m%s\033[0m\n' "$*" >&2; exit 1; }

echo
printf '  \033[1mNppDev\033[0m\n'
printf '  \033[2mhttps://github.com/%s\033[0m\n\n' "$repo"

command -v apt-get >/dev/null || fail "This installer is for Ubuntu and other Debian-based systems (apt). Download the AppImage from https://github.com/$repo/releases/latest instead."
[ "$(uname -m)" = "x86_64" ] || fail "NppDev is built for 64-bit Intel/AMD (x86_64) computers; this one is $(uname -m)."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ -n "${NPPDEV_DEB:-}" ]; then
  deb="$work/nppdev.deb"
  cp "$NPPDEV_DEB" "$deb"
  say "Using $NPPDEV_DEB"
else
  command -v curl >/dev/null || fail "curl is needed: sudo apt install curl"
  command -v python3 >/dev/null || fail "python3 is needed: sudo apt install python3"
  say "Finding the latest release..."
  json="$(curl -fsSL -H 'Accept: application/vnd.github+json' -A NppDev-installer "https://api.github.com/repos/$repo/releases/latest")" \
    || fail "Couldn't reach GitHub. Check your internet connection."
  # tag, download URL and SHA-256 of the .deb, one per line
  info="$(printf '%s' "$json" | python3 -c '
import json, sys
r = json.load(sys.stdin)
a = next((a for a in r.get("assets", []) if a["name"].endswith("_amd64.deb")), None)
if a:
    print(r["tag_name"]); print(a["browser_download_url"]); print((a.get("digest") or "").replace("sha256:", ""))
')"
  [ -n "$info" ] || fail "The latest release has no .deb file."
  tag="$(sed -n 1p <<<"$info")"; url="$(sed -n 2p <<<"$info")"; want="$(sed -n 3p <<<"$info")"
  [ -n "$want" ] || fail "GitHub gave no checksum for this file, so it can't be verified. Nothing was installed."
  deb="$work/$(basename "$url")"
  say "Downloading NppDev ${tag#v}..."
  curl -fsSL -A NppDev-installer -o "$deb" "$url" || fail "The download failed. Please try again."
  got="$(sha256sum "$deb" | cut -d' ' -f1)"
  [ "$got" = "$want" ] || fail "The download doesn't match GitHub's checksum (expected $want, got $got). Nothing was installed; please try again."
  printf '  \033[32mVerified: %s MB, SHA-256 %s...\033[0m\n' "$(( $(stat -c %s "$deb") / 1048576 ))" "${got:0:16}"
fi

say "Installing (apt may ask for your password)..."
sudo_cmd=""
[ "$(id -u)" -eq 0 ] || sudo_cmd="sudo"
# apt reads the file as its own user, so it must be readable by everyone
chmod 644 "$deb"; chmod 755 "$work"
$sudo_cmd apt-get install -y "$deb" >/dev/null || fail "apt couldn't install NppDev."

printf '  \033[32mInstalled NppDev.\033[0m\n'
say "Open it from your apps (search for NppDev), or run: nppdev"
echo
