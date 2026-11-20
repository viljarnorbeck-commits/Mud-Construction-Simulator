#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p Saved/LinuxSoilTests
for name in environment-physics soil-transport earthwork terrain-surface earthworks-route dozer-mechanics loader-rig machine-motion running-gear worker-gait; do
    g++ -std=c++17 -O2 -Wall -Wextra -ISource/MudConstructionSimulator/Public "tests/$name.cpp" -o "Saved/LinuxSoilTests/$name"
    "Saved/LinuxSoilTests/$name" > "Saved/LinuxSoilTests/$name.txt"
    echo "$name passed"
    cat "Saved/LinuxSoilTests/$name.txt"
done
