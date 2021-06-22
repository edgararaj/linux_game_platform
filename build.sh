#!/bin/sh

set -e

base_dir="$(git rev-parse --show-toplevel)"
src_dir="$(git rev-parse --show-toplevel)/src"
build_dir="$(git rev-parse --show-toplevel)/build"

if [ -d "$build_dir" ]; then
	rm -rf "$build_dir"
else
	echo "Build dir didn't exist!"
fi

mkdir "$build_dir"

pushd "$build_dir" > /dev/null
gcc -ggdb -std=c++20 "$src_dir/x11_platform.cpp" -lX11 -lXext -lm
popd > /dev/null
