#!/bin/sh

set -e

cd "$(git rev-parse --show-toplevel)"

find * -type f -not \( -name "game_test.*" \) -print > game_test.files
