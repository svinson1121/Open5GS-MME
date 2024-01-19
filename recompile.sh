#!/bin/bash
meson build --prefix=`pwd`/install
ninja -C build install
systemctl restart 'open5gs*'
