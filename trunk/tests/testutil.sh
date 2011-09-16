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

# Get file's inode
get_inode()
{
    local file="$1"
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

# Create a state file 
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

# Create a state file for an empty file
reset_state_file()
{
    create_state_file "$1" "$2"
}

# Verify a string
verify_value()
{
    local name="$1"
    local expected="$2"
    local actual="$3"
    [ "${actual}" = "${expected}" ] || errout "ERROR: mismatch for ${name}: expected '${expected}' but got '${actual}'"
}

# Verify a state file
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

# Verify expected output
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

