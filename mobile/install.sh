#!/data/data/com.termux/files/usr/bin/bash
# A7R3 mobile injector installer (Termux, Android, no root).
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"

echo "[*] installing Termux packages..."
pkg install -y python termux-api libusb lld llvm

echo "[*] installing python-libusb1..."
pip install --upgrade libusb1

echo "[*] running LLVM assembler self-test..."
python3 "$HERE/termux/selftest.py"

echo "[*] installing the \"inject\" command..."
mkdir -p "$PREFIX/bin"
cat > "$PREFIX/bin/inject" <<EOF
#!/data/data/com.termux/files/usr/bin/bash
exec python3 "$HERE/termux/inject.py" "\$@"
EOF
chmod +x "$PREFIX/bin/inject"

echo
echo "[OK] done. Plug the camera in (USB connection = Mass Storage) and run:"
echo "     inject"
