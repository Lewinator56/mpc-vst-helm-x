#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== 1. Generating parameters, skin, and XML descriptor ==="
python3 tools/gen_vst.py vst.json
python3 tools/helm_paint.py

echo "=== 2. Cross-compiling ARM32 binary (helm.so) ==="
mkdir -p build

eval "$(python3 tools/gen_vst.py vst.json --shell)"

OBJS=""
for f in $SOURCES; do
  o="build/${f//\//_}.o"
  case "$f" in
    *.cpp|*.cc|*.cxx)
      arm-linux-gnueabihf-g++ -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu++11 $CFLAGS -Ibuild -c "$f" -o "$o"
      ;;
    *)
      arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu11 $CFLAGS -Ibuild -c "$f" -o "$o"
      ;;
  esac
  OBJS="$OBJS $o"
done

arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -fvisibility=hidden -std=gnu11 -Ibuild -c wrapper/vst2_wrap.c -o build/vst2_wrap.o
arm-linux-gnueabihf-g++ -O2 -shared -fPIC -fvisibility=hidden $OBJS build/vst2_wrap.o $LIBS -Wl,--no-undefined -o build/helm.so
arm-linux-gnueabihf-strip build/helm.so

echo "=== 3. Binary Verification ==="
file build/helm.so
echo "Exported symbols: $(readelf --dyn-syms -W build/helm.so | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{print $8}' | tr '\n' ' ')"
echo "Highest glibc symbol: $(readelf -V build/helm.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
ls -lh build/helm.so

echo "=== 4. Packaging Release Bundle ==="
PACKAGE_DIR="build/package/Matt Tytel - VST - Helm"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"
cp -r "build/skin/Matt Tytel - VST - Helm/"* "$PACKAGE_DIR/"
cp build/helm.so "$PACKAGE_DIR/"
cp -r patches "$PACKAGE_DIR/"
if [ -f build/pluginlist-entry.xml ]; then
  sed 's|file="[^"]*helm.so"|file="%payload-path%/Matt Tytel - VST - Helm/helm.so"|' build/pluginlist-entry.xml > "$PACKAGE_DIR/plugin-meta.xml"
fi

python3 ../../mpc-vst-plugins/tools/release.py \
  --so build/helm.so \
  --skin "build/skin/Matt Tytel - VST - Helm" \
  --entry build/pluginlist-entry.xml \
  --version "1.0.0" \
  --extra "patches:patches" \
  --about "Helm polyphonic synthesizer for Akai MPC" \
  --id "helm" \
  --repo "Lewinator56/mpc-vst-helm" \
  --license "GPL-3.0-only" \
  -o dist

echo "=== BUILD COMPLETE ==="
echo "Plugin packaged at: $PACKAGE_DIR and dist/Helm-1.0.0-mpc-armv7.zip"
