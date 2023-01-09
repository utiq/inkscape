#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2022 René de Hesselle <dehesselle@web.de>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#
# This script is CI-only, you will encounter errors if you run it on your
# local machine. If you want to build Inkscape locally, see
# https://gitlab.com/inkscape/devel/mibap
#

# toolset release to build Inkscape
VERSION=v0.70

# directory convenience handles
SELF_DIR=$(dirname "${BASH_SOURCE[0]}")
MIBAP_DIR=$SELF_DIR/mibap

# clone macOS build repository
git clone \
  --recurse-submodules \
  --depth 1 \
  --branch $VERSION \
  --single-branch \
  https://gitlab.com/inkscape/devel/mibap \
  "$MIBAP_DIR"

# make sure the runner is clean (this doesn't hurt if there's nothing to do)
"$MIBAP_DIR"/uninstall_toolset.sh

if [ "$(basename -s .sh "${BASH_SOURCE[0]}")" = "test" ]; then
  # install build dependencies and Inkscape
  "$MIBAP_DIR"/install_toolset.sh restore_overlay
  # run the test suite
  "$MIBAP_DIR"/310-inkscape_test.sh
else
  # install build dependencies
  "$MIBAP_DIR"/install_toolset.sh
  # build Inkscape
  "$MIBAP_DIR"/build_inkscape.sh
  # uninstall build dependencies and archive build files
  "$MIBAP_DIR"/uninstall_toolset.sh save_overlay
fi
