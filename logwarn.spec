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
# $Id$
#

Name:           logwarn
Version:        1.0.4
Release:        1
License:        Apache License, Version 2.0
Summary:        Utility for finding interesting messages in log files
Group:          System/Utilities
Source:         %{name}-%{version}.tar.gz
URL:            http://%{name}.googlecode.com/
BuildRoot:      %{_tmppath}/%{name}-%{version}-root
BuildRequires:  make
BuildRequires:  gcc

%description
%{name} searches for interesting messages in log files, where ``interest-
ing'' is defined by an user-supplied list of positive and negative (pre-
ceeded with a ``!'') extended regular expressions provided on the command
line.

Each log message is compared against each pattern in the order given.  If
the log message matches a positive pattern before matching a negative
!pattern then it's printed to standard output.

%{name} keeps track of its position between invocations, so each matching
line is only ever output once.  It also finds messages in log files that
have been rotated (and possibly compressed) since the previous invoca-
tion.

%{name} also includes support for log messages that span multiple lines.

%prep
%setup -q

%build
%{configure}
make

%install
rm -rf ${RPM_BUILD_ROOT}
%{makeinstall}
install -d ${RPM_BUILD_ROOT}%{_var}/lib/%{name}

%files
%attr(0755,root,root) %{_bindir}/%{name}
%attr(0644,root,root) %{_mandir}/man1/%{name}.1.gz
%defattr(0644,root,root,0755)
%{_var}/lib/%{name}
%doc %{_datadir}/doc/packages/%{name}

%package nagios-plugin
Summary:        Nagios plugin based on the logwarn(1) utility
Group:          System/Utilities
Requires:       bash
Requires:       logwarn >= %{version}
Buildarch:      noarch

%description nagios-plugin
A Nagios plugin based on the logwarn(1) utility.

%files nagios-plugin
%defattr(0755,root,root,0755)
%attr(0755,root,root) %{_prefix}/lib/nagios

%changelog
