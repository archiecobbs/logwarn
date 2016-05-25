#
# Logwarn - Utility for finding interesting messages in log files
#
# Copyright (C) 2010-2011 Archie L. Cobbs. All rights reserved.
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

bin_PROGRAMS=		logwarn

noinst_HEADERS=		logwarn.h

man_MANS=		logwarn.1

docdir=			$(datadir)/doc/packages/$(PACKAGE)

doc_DATA=		CHANGES COPYING README.md INSTALL AUTHORS

plugindir=		$(prefix)/lib/nagios/plugins

plugin_SCRIPTS=		check_logwarn

EXTRA_DIST=		CHANGES README.md

logwarn_SOURCES=	main.c \
			state.c \
			gitrev.c

gitrev.c:
			printf 'const char *const logwarn_version = "%s";\n' "`git describe`" > gitrev.c

.PHONY:			tests
tests:			logwarn
			cd tests && sh runtests

