#!/bin/bash

set -e

name="game_test"
base_dir="$(git rev-parse --show-toplevel)"
src_dir="$base_dir/src"
build_dir="$base_dir/build"

if [ -d "$build_dir" ]; then
	rm -rf "$build_dir"
else
	echo "Build dir didn't exist!"
fi

mkdir "$build_dir"

wno="-Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers"
fno="-fno-rtti -fno-exceptions -fno-unwind-tables"
cpp_flags="-Werror -Wall -Wextra -Wdouble-promotion $wno -Og -ggdb -std=c++20 $fno -DINTERNAL=1 -DSLOW=1 -nodefaultlibs"
echo $cpp_flags > "$base_dir/$name.cxxflags"

pushd "$build_dir" > /dev/null
gcc "$src_dir/x11_platform.cpp" -lm -lc -lX11 -lXext -ldl -lasound $cpp_flags
popd > /dev/null
