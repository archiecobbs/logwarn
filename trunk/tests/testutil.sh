#!/bin/bash

# Common definitions
NAME=`basename $0`
TESTDIR=`pwd`
LOGWARN="${TESTDIR}/../logwarn"

# Log functions
log()
{
    echo ${NAME}: ${1+"$@"} 1>&2
}

# Error function
errout()
{
    log ${1+"$@"}
    exit 1
}

#
# Get a file's inode.
#
# Usage: get_inode filename
#
# Returns zero for `-'.
#
get_inode()
{
    local file="$1"
    if [ "${file}" = "-" ]; then
        echo 0
        return
    fi
    case `uname -s` in
        *BSD|Darwin)
            statfmt="-f %i"
            ;;
        *)
            statfmt="--format=%i"
            ;;
    esac
    stat ${statfmt} "${file}"
}

#
# Create a state file
#
# Usage: create_state_file filename logfile [ linenum [ position [ matching ] ] ]
#
create_state_file()
{
    local statefile="$1"
    local logfile="$2"
    local linenum="$3"
    local position="$4"
    local matching="$5"
    if [ "${linenum}" = "" ]; then
        linenum="1"
    fi
    if [ "${position}" = "" ]; then
        position="0"
    fi
    if [ "${matching}" = "" ]; then
        matching="false"
    fi
    local inode=`get_inode "${logfile}"`
    printf 'INODENUM="%d"\nLINENUM="%d"\nPOSITION="%d"\nMATCHING="%s"\n' \
      "${inode}" "${linenum}" "${position}" "${matching}" > "${statefile}"
}

#
# Create a state file for an empty file
#
# Usage: reset_state_file filename logfile
#
reset_state_file()
{
    create_state_file "$1" "$2"
}

#
# Verify a string
#
# Usage: verify_value name expected actual
#
verify_value()
{
    local name="$1"
    local expected="$2"
    local actual="$3"
    [ "${actual}" = "${expected}" ] || errout "ERROR: mismatch for ${name}: expected '${expected}' but got '${actual}'"
}

#
# Verify a state file
#
# Usage: verify_state_file statefile logfile linenum position matching
#
verify_state_file()
{
    local statefile="$1"
    local logfile="$2"
    local linenum="$3"
    local position="$4"
    local matching="$5"
    local inode=`get_inode "${logfile}"`
    . "${statefile}"
    verify_value INODENUM "${inode}" "${INODENUM}"
    verify_value LINENUM "${linenum}" "${LINENUM}"
    verify_value POSITION "${position}" "${POSITION}"
    verify_value MATCHING "${matching}" "${MATCHING}"
}

#
# Verify expected output
#
# Usage: verify_output expected-output logwarn-params ...
#
verify_output()
{
    local expected="$1"
    shift
    TEMPFILE=`mktemp -q /tmp/logwarntest.XXXXXX`
    [ $? -eq 0 ] || errout "can't create temporary file"
    trap "rm -f ${TEMPFILE}" 0 2 3 5 10 13 15
    "${LOGWARN}" ${1+"$@"} > "${TEMPFILE}"
    diff -u "${expected}" "${TEMPFILE}" || errout "ERROR: incorrect output from test"
    trap - 0 2 3 5 10 13 15
    rm -f "${TEMPFILE}"
}

