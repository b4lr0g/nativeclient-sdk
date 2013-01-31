#!/bin/bash
# Copyright (c) 2012 The Native Client Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

SCRIPT_DIR="$(cd $(dirname $0) && pwd)"
NACLPORTS_ROOT=${SCRIPT_DIR}/../../third_party/naclports/src

source ${NACLPORTS_ROOT}/build_tools/bots/bot_common.sh

OUT_DIR=${SCRIPT_DIR}/../out/naclports
OUT_BUNDLE_DIR=${OUT_DIR}

cd ${NACLPORTS_ROOT}

CustomBuildPackage() {
  export NACLPORTS_PREFIX=${OUT_DIR}/_build_/$2_$3_$4
  export NACL_ARCH=$2
  export NACL_GLIBC=0

  if [ "$4" = "Debug" ]; then
    export NACL_DEBUG=1
  else
    export NACL_DEBUG=0
  fi

  BuildPackage $1
}

BuildPackageArchAll() {
  CustomBuildPackage $1 $2 newlib Release
  CustomBuildPackage $1 $2 newlib Debug
}

BuildPackageAll() {
  BuildPackageArchAll $1 i686
  BuildPackageArchAll $1 x86_64
  BuildPackageArchAll $1 arm
}

MoveLibs() {
  for ARCH in i686 x86_64 arm; do
    if [ "$ARCH" = "i686" ]; then
      ARCH_DIR=x86_32
    else
      ARCH_DIR=$ARCH
    fi

    for LIBC in newlib; do
      for CONFIG in Debug Release; do
        # Copy build results to destination directories.
        SRC_DIR=${OUT_DIR}/_build_/${ARCH}_${LIBC}_${CONFIG}
        ARCH_LIB_DIR=${OUT_BUNDLE_DIR}/lib/${LIBC}_${ARCH_DIR}/${CONFIG}

        mkdir -p ${ARCH_LIB_DIR}
        cp -f -d -r ${SRC_DIR}/* ${OUT_BUNDLE_DIR}

        for FILE in ${OUT_BUNDLE_DIR}/lib/* ; do
          if [ -f "$FILE" ]; then
            mv ${FILE} ${ARCH_LIB_DIR}
          fi
        done
      done
    done
  done
}

PACKAGES="fontconfig xml2 png jpeg tiff nacl-mounts"

echo "Building packages..."
for package in $PACKAGES; do
  BuildPackageAll $package
done

echo "Moving results..."
for package in $PACKAGES; do
  MoveLibs $package
done

echo "Done!"
echo -e "$MESSAGES"

exit $RESULT
