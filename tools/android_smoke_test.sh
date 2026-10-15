#!/usr/bin/env bash
set -euo pipefail
# Always retain diagnostics, including when install/instrumentation fails.
trap 'adb logcat -d > device-logcat.log; adb exec-out screencap -p > editor-screenshot.png' EXIT
adb install -r apks/nova-engine-debug.apk
adb install -r apks/app-debug-androidTest.apk
adb logcat -c
adb shell am instrument -w -r com.nova.engine.test/androidx.test.runner.AndroidJUnitRunner | tee device-test.log
grep -q 'OK (1 test)' device-test.log
if adb logcat -d | grep -E 'gles: (render error|shader compilation failed|program link failed)|FATAL EXCEPTION|Fatal signal'; then
  echo '::error::Runtime graphics or crash error detected'
  exit 1
fi
# Leave the editor visible for an actual rendered screenshot (the test closes it).
adb shell am start -W -n com.nova.engine/.MainActivity
sleep 3
