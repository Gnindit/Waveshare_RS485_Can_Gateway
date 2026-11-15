#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
testDir=$(mktemp -d)
trap 'rm -rf "$testDir"' EXIT
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude test/ota_policy_test.cpp -o "$testDir/ota-policy"
"$testDir/ota-policy"
python3 -m unittest discover -s test -p 'test_*.py'
