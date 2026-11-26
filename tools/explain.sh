#!/usr/bin/env bash
# Build (if needed) and run the terminal BFS explainer.
#
#   bash tools/explain.sh samples/city.txt Jayanagar Whitefield
#   bash tools/explain.sh samples/bmtc.txt \
#        4th_T_Block_Jayanagara Canara_Bank_Hebbala
#
# It links the real src/graph.cpp; it does not reimplement the search.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p build
if [[ ! -x build/explain || tools/explain.cpp -nt build/explain ||
      src/graph.cpp -nt build/explain || include/stats.hpp -nt build/explain ]]; then
  g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -Iinclude tools/explain.cpp src/graph.cpp -o build/explain
fi

exec ./build/explain "$@"
