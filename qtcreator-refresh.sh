#!/bin/bash

set -e

cd "$(git rev-parse --show-toplevel)"

find * -type f ! -name "game_test.*" -print > game_test.files
