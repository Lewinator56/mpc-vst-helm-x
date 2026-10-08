#!/usr/bin/env bash
set -e
eval "$(python3 tools/gen_vst.py vst.json --shell)"
OBJS=""
for f in $SOURCES; do
  o="build/${f//\//_}.o"
  OBJS="$OBJS $o"
done
arm-linux-gnueabihf-g++ -O2 -shared -fPIC -fvisibility=hidden $OBJS build/vst2_wrap.o $LIBS -Wl,--no-undefined -o build/helm.so
arm-linux-gnueabihf-strip build/helm.so
echo "SUCCESS!"
ls -lh build/helm.so
cp build/helm.so "build/package/Lewinator56 - VST - HelmX/" 2>/dev/null || true
cp fb_timing.txt "build/package/Lewinator56 - VST - HelmX/" 2>/dev/null || true
