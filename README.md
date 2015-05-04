**Logwarn** searches for interesting messages in log files, where ``interesting'' is defined by a user-supplied list of positive and negative extended regular expressions.

Each log message is compared against each pattern in the order given.  If the log message matches a positive pattern before matching a negative pattern then it's printed to standard output.

**Logwarn** keeps track of its position between invocations, so each matching line is only ever output once.  It also finds messages in log files that have been rotated (and possibly compressed) since the previous invocation.

**Logwarn** also includes support for log messages that span multiple lines.

**Logwarn** is written in C for efficient execution.

A [Nagios](http://www.nagios.org/) plugin is also included: see the [NagiosPlugin](https://github.com/archiecobbs/logwarn/wiki/NagiosPlugin) wiki page for more info.

You can view the [ManPage](https://github.com/archiecobbs/logwarn/wiki/ManPage) online.
