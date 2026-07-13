#!/bin/sh
set -e

REPO="https://github.com/phyothant-dev/PT-Programming-Language.git"
INSTALL_DIR="/usr/local/bin"
TMPDIR=""

cleanup() {
  [ -n "$TMPDIR" ] && rm -rf "$TMPDIR"
}
trap cleanup EXIT

# Check for git
if ! command -v git >/dev/null 2>&1; then
  echo "Error: git is required. Please install git first."
  exit 1
fi

# Check for a C++17 compiler
if command -v g++ >/dev/null 2>&1; then
  CXX=g++
elif command -v clang++ >/dev/null 2>&1; then
  CXX=clang++
else
  echo "Error: No C++17 compiler found. Please install g++ or clang++."
  exit 1
fi

echo "Cloning PT repository..."
TMPDIR=$(mktemp -d)
git clone --depth 1 "$REPO" "$TMPDIR/pt"

echo "Building PT..."
cd "$TMPDIR/pt"
make CXX="$CXX"

echo "Installing to $INSTALL_DIR..."
if [ -w "$INSTALL_DIR" ] 2>/dev/null; then
  install -m 755 pt "$INSTALL_DIR/pt"
else
  echo "Need sudo to install to $INSTALL_DIR"
  sudo install -d "$INSTALL_DIR"
  sudo install -m 755 pt "$INSTALL_DIR/pt"
fi

echo ""
echo "PT installed successfully! Run 'pt' to start the REPL."
echo "  pt script.pt     — run a file"
echo "  pt               — interactive REPL"
