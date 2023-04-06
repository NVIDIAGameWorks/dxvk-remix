#############################################################################
# Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#############################################################################

#!/usr/bin/env bash

set -e

shopt -s extglob

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 version destdir [--no-package] [--dev-build]"
  exit 1
fi

DXVK_VERSION="$1"
DXVK_SRC_DIR=`dirname $(readlink -f $0)`
DXVK_BUILD_DIR=$(realpath "$2")"/dxvk-$DXVK_VERSION"
DXVK_ARCHIVE_PATH=$(realpath "$2")"/dxvk-$DXVK_VERSION.tar.gz"

if [ -e "$DXVK_BUILD_DIR" ]; then
  echo "Build directory $DXVK_BUILD_DIR already exists"
  exit 1
fi

shift 2

opt_nopackage=0
opt_devbuild=0
opt_buildid=false

crossfile="build-win"

while [ $# -gt 0 ]; do
  case "$1" in
  "--no-package")
    opt_nopackage=1
    ;;
  "--dev-build")
    opt_nopackage=1
    opt_devbuild=1
    ;;
  "--build-id")
    opt_buildid=true
    ;;
  *)
    echo "Unrecognized option: $1" >&2
    exit 1
  esac
  shift
done

function build_arch {
  export WINEARCH="win$1"
  export WINEPREFIX="$DXVK_BUILD_DIR/wine.$1"
  
  cd "$DXVK_SRC_DIR"

  opt_strip=
  if [ $opt_devbuild -eq 0 ]; then
    opt_strip=--strip
  fi

  meson --cross-file "$DXVK_SRC_DIR/$crossfile$1.txt" \
        --buildtype "release"                         \
        --prefix "$DXVK_BUILD_DIR"                    \
        $opt_strip                                    \
        --bindir "x$1"                                \
        --libdir "x$1"                                \
        -Denable_tests=false                          \
        -Dbuild_id=$opt_buildid                       \
        "$DXVK_BUILD_DIR/build.$1"

  cd "$DXVK_BUILD_DIR/build.$1"
  ninja install

  if [ $opt_devbuild -eq 0 ]; then
    # get rid of some useless .a files
    rm "$DXVK_BUILD_DIR/x$1/"*.!(dll)
    rm -R "$DXVK_BUILD_DIR/build.$1"
  fi
}

function build_script {
  cp "$DXVK_SRC_DIR/setup_dxvk.sh" "$DXVK_BUILD_DIR/setup_dxvk.sh"
  chmod +x "$DXVK_BUILD_DIR/setup_dxvk.sh"
}

function package {
  cd "$DXVK_BUILD_DIR/.."
  tar -czf "$DXVK_ARCHIVE_PATH" "dxvk-$DXVK_VERSION"
  rm -R "dxvk-$DXVK_VERSION"
}

build_arch 64
#build_arch 32
build_script

if [ $opt_nopackage -eq 0 ]; then
  package
fi
