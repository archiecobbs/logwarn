#
# Logwarn - Utility for finding interesting messages in log files
#
# Copyright (C) 2010 Archie L. Cobbs. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# $Id$
#

AC_INIT([logwarn Utility for finding interesting messages in log files], [1.0.1], [http://code.google.com/p/logwarn/], [logwarn])
AC_CONFIG_AUX_DIR(scripts)
AM_INIT_AUTOMAKE
dnl AM_MAINTAINER_MODE
AC_PREREQ(2.59)
AC_REVISION($Id$)
AC_PREFIX_DEFAULT(/usr)
AC_PROG_MAKE_SET

[CFLAGS="-g -O3 -pipe -Wall -Waggregate-return -Wcast-align -Wchar-subscripts -Wcomment -Wformat -Wimplicit -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wno-long-long -Wparentheses -Wpointer-arith -Wredundant-decls -Wreturn-type -Wswitch -Wtrigraphs -Wuninitialized -Wunused -Wwrite-strings -Wshadow -Wstrict-prototypes -Wcast-qual $CFLAGS"]
AC_SUBST(CFLAGS)

# Compile flags for Linux
AC_DEFINE(_GNU_SOURCE, 1, GNU functions)
AC_DEFINE(_BSD_SOURCE, 1, BSD functions)
AC_DEFINE(_XOPEN_SOURCE, 500, XOpen functions)

# Compile flags for Mac OS
AC_DEFINE(_DARWIN_C_SOURCE, 1, MacOS functions)

# Check for required programs
AC_PROG_INSTALL
AC_PROG_CC

# Check for required header files
AC_HEADER_STDC
AC_CHECK_HEADERS(ctype.h dirent.h err.h errno.h libgen.h limits.h regex.h stdio.h stdlib.h string.h unistd.h sys/stat.h sys/types.h, [],
        [AC_MSG_ERROR([required header file '$ac_header' missing])])


# Cache directory
[DEFAULT_CACHE_DIR="/var/lib/logwarn"]
AC_ARG_WITH([cache-dir],
    [AS_HELP_STRING([--with-cache-dir=DIR],
       [default cache directory (default /var/lib/logwarn)])],
    [DEFAULT_CACHE_DIR=$withval],
    [])
AC_SUBST(DEFAULT_CACHE_DIR)

# Optional features
AC_ARG_ENABLE(Werror,
    AC_HELP_STRING([--enable-Werror],
        [enable compilation with -Werror flag (default NO)]),
    [test x"$enableval" = "xyes" && CFLAGS="${CFLAGS} -Werror"])

# Generated files
AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES(logwarn.1)
AM_CONFIG_HEADER(config.h)

# Go
AC_OUTPUT
