#!/usr/bin/env bash
# Minify and compress static assets in bt/ for serving with Content-Encoding
# negotiation. For each .html/.svg/.js/.css file this produces:
#   <file>.zst  -- zstd-compressed (served when client accepts zstd)
#   <file>.gz   -- gzip-compressed  (served when client accepts gzip)
# The plain file is left unchanged and served as identity fallback.
#
# Usage:
#   ./build-assets.sh          # minify + compress all assets
#   ./build-assets.sh clean    # remove all generated .zst and .gz files

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BT_DIR="$SCRIPT_DIR/bt"
BIN="$SCRIPT_DIR/node_modules/.bin"

EXTENSIONS=(html svg js css)

check_tools() {
    local missing=()
    for tool in esbuild svgo html-minifier-terser; do
        [[ -x "$BIN/$tool" ]] || missing+=("$tool")
    done
    for tool in gzip zstd; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "error: missing tools: ${missing[*]}" >&2
        [[ -x "$BIN/esbuild" ]] || echo "       run: npm ci  (from repo root)" >&2
        command -v zstd &>/dev/null || echo "       run: apt install zstd  (or brew install zstd)" >&2
        exit 1
    fi
}

minify_to() {
    local src="$1" dst="$2" ext="${1##*.}"
    case "$ext" in
        js|css)
            "$BIN/esbuild" --minify "$src" > "$dst"
            ;;
        html)
            "$BIN/html-minifier-terser" \
                --collapse-whitespace \
                --remove-comments \
                --minify-css true \
                --minify-js true \
                "$src" > "$dst"
            ;;
        svg)
            "$BIN/svgo" "$src" -o "$dst"
            ;;
    esac
}

build() {
    check_tools
    local tmp
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT

    for ext in "${EXTENSIONS[@]}"; do
        while IFS= read -r -d '' src; do
            echo "  ${src#"$SCRIPT_DIR"/}"
            minify_to "$src" "$tmp"
            zstd -q -f -19 "$tmp" -o "${src}.zst"
            gzip -n -9 -c "$tmp" > "${src}.gz"
        done < <(find "$BT_DIR" -maxdepth 1 -name "*.${ext}" -print0)
    done
}

clean() {
    for ext in "${EXTENSIONS[@]}"; do
        find "$BT_DIR" -maxdepth 1 \
            \( -name "*.${ext}.zst" -o -name "*.${ext}.gz" \) \
            -print -delete
    done
}

case "${1:-}" in
    clean)    clean ;;
    ""|build) build ;;
    *)
        printf 'Usage: %s [clean]\n' "$0" >&2
        exit 1
        ;;
esac
