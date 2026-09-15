#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ASSET_DIR=${1:-"$SCRIPT_DIR/assets"}
OUTPUT_DIR=${2:-"$SCRIPT_DIR/dist-release"}
VENV="$SCRIPT_DIR/.venv-macos"
PYTHON=${REKORDPOD_PYTHON:-}
export PIP_CACHE_DIR="$SCRIPT_DIR/.pip-cache"
export PYINSTALLER_CONFIG_DIR="$SCRIPT_DIR/.pyinstaller"

if [ -z "$PYTHON" ]; then
    for candidate in python3.13 python3.12 python3.11 python3.10
    do
        if command -v "$candidate" >/dev/null 2>&1; then
            PYTHON=$(command -v "$candidate")
            break
        fi
    done
fi

if [ -z "$PYTHON" ]; then
    echo "Python 3.10 or newer is required." >&2
    exit 1
fi

for archive in \
    rekordpod-public-beta-2-ipod6g.zip
do
    if [ ! -f "$ASSET_DIR/$archive" ]; then
        echo "Missing $ASSET_DIR/$archive" >&2
        exit 1
    fi
done

if [ ! -x "$VENV/bin/python" ]; then
    "$PYTHON" -m venv "$VENV"
elif ! "$VENV/bin/python" -c \
    'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)'
then
    echo "Remove $VENV and rerun; it was created by Python older than 3.10." >&2
    exit 1
fi

"$VENV/bin/python" -m pip install --disable-pip-version-check \
    -r "$SCRIPT_DIR/requirements-installer.txt"

rm -rf "$SCRIPT_DIR/build" "$SCRIPT_DIR/dist"
(
    cd "$SCRIPT_DIR"
    REKORDPOD_ASSET_DIR="$ASSET_DIR" \
        "$VENV/bin/python" -m PyInstaller \
        --noconfirm --clean "$SCRIPT_DIR/rekordpod-installer.spec"
)

mkdir -p "$OUTPUT_DIR"
rm -rf "$OUTPUT_DIR/Rekordpod Installer.app" \
       "$OUTPUT_DIR/Rekordpod-Installer-macOS.zip"
cp -R "$SCRIPT_DIR/dist/Rekordpod Installer.app" "$OUTPUT_DIR/"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent \
    "$OUTPUT_DIR/Rekordpod Installer.app" \
    "$OUTPUT_DIR/Rekordpod-Installer-macOS.zip"

echo "Built $OUTPUT_DIR/Rekordpod Installer.app"
echo "Built $OUTPUT_DIR/Rekordpod-Installer-macOS.zip"
