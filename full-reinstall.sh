#!/bin/bash
rm -rf install/*
rm -rf build/*
meson build --prefix=`pwd`/install
ninja -C build
meson test -C build -v
ninja -C build install