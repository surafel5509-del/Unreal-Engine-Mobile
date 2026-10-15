#!/usr/bin/env bash
set -euo pipefail
# Always retain diagnostics, including when install/instrumentation fails.
trap 'adb logcat -d > device-logcat.log; adb exec-out screencap -p > editor-screenshot.png' EXIT
adb install -r apks/nova-engine-debug.apk
adb install -r apks/app-debug-androidTest.apk
adb logcat -c
adb shell am instrument -w -r com.nova.engine.test/androidx.test.runner.AndroidJUnitRunner | tee device-test.log
grep -q 'OK (1 test)' device-test.log
adb logcat -d > device-logcat.log
# System-image services can crash independently of the app. Match native
# renderer failures and this package's crash records, not every system fatal.
if grep -E 'gles: (render error|shader compilation failed|program link failed)|Process: com\.nova\.engine[, ]|Fatal signal.*com\.nova\.engine' device-logcat.log > device-errors.log; then
  while IFS= read -r line; do echo "::error title=Nova runtime::${line}"; done < device-errors.log
  exit 1
fi
# Leave the editor visible for an actual rendered screenshot (the test closes it).
adb shell am start -W -n com.nova.engine/.MainActivity
sleep 3
