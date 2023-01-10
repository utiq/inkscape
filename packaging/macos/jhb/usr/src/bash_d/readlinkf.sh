# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/dehesselle/bash_d

### description ################################################################

# This is a replacement for GNU's '-f' extension to 'readlink' which is not
# part of the BSD version.

### includes ###################################################################

# Nothing here.

### variables ##################################################################

# Nothing here.

### functions ##################################################################

# Nothing here.

### aliases ####################################################################

alias readlinkf='perl -e '"'"'use Cwd "abs_path"; print abs_path(@ARGV[0])'"'"' --'

### main #######################################################################

# Nothing here.
