#!/bin/bash
# Runs the testbed headless for a fixed number of simulation steps and checks that
# the physics state hash matches the expected value.
#
# Usage: .github/scripts/headless-smoke.sh <expected-hash>   (from the repository root, after a build)
#
# The hash changes whenever the testbed scene or the physics simulation changes on purpose.
# When that happens, update EXPECTED_PHYSICS_HASH in .github/workflows/ci.yml in the same PR.
set -euo pipefail

expected_hash="${1:?usage: headless-smoke.sh <expected-hash>}"
expected_steps=600
log="$(pwd)/bin/headless-smoke.log"

cd bin
status=0
# No audio device in CI: OpenAL Soft's null backend keeps the audio plugin happy.
ALSOFT_DRIVERS=null timeout 600 ./testbed.kapp --config ../testbed.kapp/app_config_headless.kson >"$log" 2>&1 || status=$?

# Strip ANSI colors so the log greps cleanly.
clean_log="$(sed 's/\x1b\[[0-9;]*m//g' "$log")"

if [ "$status" -ne 0 ]; then
	echo "$clean_log" | tail -n 50
	echo "::error::Headless run exited with status $status."
	exit 1
fi

steps="$(echo "$clean_log" | sed -n 's/.*Testbed ran \([0-9]*\) fixed simulation steps.*/\1/p' | tail -n 1)"
hash="$(echo "$clean_log" | sed -n 's/.*Physics state hash: \([0-9a-f]*\).*/\1/p' | tail -n 1)"

echo "Simulation steps: ${steps:-missing} (expected $expected_steps)"
echo "Physics state hash: ${hash:-missing} (expected $expected_hash)"

if [ "$steps" != "$expected_steps" ] || [ "$hash" != "$expected_hash" ]; then
	echo "$clean_log" | tail -n 50
	echo "::error::Headless smoke run did not match the expected steps and physics hash."
	exit 1
fi
