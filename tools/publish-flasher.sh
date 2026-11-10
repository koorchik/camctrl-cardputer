#!/usr/bin/env bash
# Publishes the browser installer (flasher/index.html + ESP Web Tools) to the
# gh-pages branch: builds the release images (tools/release.sh), writes one
# ESP Web Tools manifest per device environment, copies the screenshots and
# pushes. GitHub Pages serves it at https://<owner>.github.io/<repo>/.
# Usage: tools/publish-flasher.sh [--no-push]
set -euo pipefail
cd "$(dirname "$0")/.."

NAME="$(sed -n 's/^project(\(.*\))/\1/p' CMakeLists.txt)"
VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' CMakeLists.txt)"
TITLE="$(sed -n 's:.*<title>\(.*\)</title>.*:\1:p' flasher/index.html)"
tools/release.sh

SITE="$(mktemp -d)"
trap 'git worktree remove --force "$SITE" 2>/dev/null || rm -rf "$SITE"' EXIT
rm -rf "$SITE"
if git show-ref --quiet refs/heads/gh-pages || git fetch -q origin gh-pages:gh-pages 2>/dev/null; then
    git worktree add -q "$SITE" gh-pages
else
    git worktree add -q --orphan -b gh-pages "$SITE"
fi
# The site is rebuilt from scratch every time.
git -C "$SITE" rm -rqf --ignore-unmatch . >/dev/null
find "$SITE" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

cp flasher/index.html "$SITE/index.html"
# ESP Web Tools served from the site itself (no CDN at install time), from
# the npm tarball checked against its published sha512.
EWT_VER="10.4.0"
EWT_SHA512="3pwkeFFm5Fj7UQo8SJNYK5RXrtNCpq6X9QoI6bMT4GBZWgrJqjn0YvM9ihG74BtMoSFYXfmDtkehuxe50PTMPQ=="
EWT_TGZ="$(mktemp)"
curl -sfL -o "$EWT_TGZ" "https://registry.npmjs.org/esp-web-tools/-/esp-web-tools-$EWT_VER.tgz"
[[ "$(openssl dgst -sha512 -binary "$EWT_TGZ" | base64 -w0)" == "$EWT_SHA512" ]] || { echo "esp-web-tools checksum mismatch" >&2; exit 1; }
mkdir -p "$SITE/esp-web-tools"
tar -xzf "$EWT_TGZ" -C "$SITE/esp-web-tools" --strip-components=3 package/dist/web
rm -f "$EWT_TGZ"
mkdir -p "$SITE/images" "$SITE/firmware"
cp docs/images/*.png docs/images/*.gif "$SITE/images/" 2>/dev/null || true
for full in dist/*-full.bin; do
    env="$(basename "$full" | sed "s/^$NAME-\(.*\)-v$VER-full.bin$/\1/")"
    cp "$full" "$SITE/firmware/"
    cat > "$SITE/manifest-$env.json" <<JSON
{
  "name": "$TITLE ($env)",
  "version": "$VER",
  "new_install_prompt_erase": true,
  "builds": [
    {"chipFamily": "ESP32-S3", "parts": [{"path": "firmware/$(basename "$full")", "offset": 0}]}
  ]
}
JSON
done
touch "$SITE/.nojekyll"
git -C "$SITE" add -A
git -C "$SITE" commit -qm "Installer for $NAME $VER" || echo "installer unchanged"
if [[ "${1:-}" != "--no-push" ]]; then
    git -C "$SITE" push -q origin gh-pages
    echo "pushed gh-pages; enable Pages (branch gh-pages, root) once in the repository settings"
fi
