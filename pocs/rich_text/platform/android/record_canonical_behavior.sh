#!/usr/bin/env bash
set -euo pipefail

readonly package_name="com.mostorm.canvas.poc04"
readonly activity_name="${package_name}/.RichTextActivity"
readonly artifact_name="android-behavior.json"
readonly failed_artifact_name="android-behavior.failed.json"
readonly output_dir="${GITHUB_WORKSPACE:-$(pwd)}/out/poc04-android"
readonly apk_path="pocs/rich_text/platform/android/app/build/outputs/apk/release/app-release.apk"
readonly device_output_dir="/sdcard/Android/data/${package_name}/files"

collect_diagnostics() {
  mkdir -p "$output_dir"
  if adb shell test -s "$device_output_dir/$failed_artifact_name"; then
    adb pull "$device_output_dir/$failed_artifact_name" \
      "$output_dir/$failed_artifact_name" || true
  fi
  adb logcat -d -t 1000 > "$output_dir/logcat.txt" || true
  adb shell dumpsys activity top > "$output_dir/activity-top.txt" || true
  adb shell dumpsys package "$package_name" > "$output_dir/package.txt" || true
  cat "$output_dir/logcat.txt" >&2 || true
  cat "$output_dir/activity-top.txt" >&2 || true
}

mkdir -p "$output_dir"
adb install -r "$apk_path"
adb logcat -c
adb shell am force-stop "$package_name"
adb shell rm -f "$device_output_dir/$artifact_name" \
  "$device_output_dir/$failed_artifact_name" || true
adb shell am start -S -W --activity-clear-task -n "$activity_name" \
  --ez recordCanonicalBehavior true

for attempt in $(seq 1 180); do
  if adb shell test -s "$device_output_dir/$artifact_name"; then
    adb pull "$device_output_dir/$artifact_name" "$output_dir/$artifact_name"
    adb logcat -d -t 300 > "$output_dir/logcat.txt" || true
    exit 0
  fi
  if (( attempt >= 5 )) && ! adb shell pidof "$package_name" >/dev/null; then
    echo "Android canonical behavior Activity exited before producing its artifact." >&2
    collect_diagnostics
    exit 1
  fi
  sleep 1
done

echo "Android canonical behavior artifact was not produced within 180 seconds." >&2
collect_diagnostics
exit 1
