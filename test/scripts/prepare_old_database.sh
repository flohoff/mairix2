#!/bin/bash
#
# Generates a minimal mairix database in a superseded on-disk format, to
# check that current mairix refuses to use it (instead of misreading it
# with the current format's assumptions).
#
# Usage:
#   prepare_old_database.sh VERSION
#
# where VERSION selects the old format version (3 or 4). The database is
# written to ./database (i.e. relative to the data directory).
#
set -e

VERSION="${1:-3}"

case "$VERSION" in
    "3" ) VERSION_BYTE='\003' ;;
    "4" ) VERSION_BYTE='\004' ;;
    * )
	echo "Unknown database version \"$VERSION\" (expected 3 or 4)" >&2
	exit 1
	;;
esac

# Header magic: 'M', 'X', 0xA5, <version byte>.
# This is followed by the 32-bit word table; the layout below does not have
# to match the old format exactly, because a conforming mairix must bail out
# on the version byte alone, before interpreting any further word.
{
    printf "MX\245${VERSION_BYTE}"
    printf '\001\002\003\004'  # junk (real format: endianness marker word)
    printf '\005\006\007\010'  # junk
    printf '\011\012\013\014'  # junk
} > database.tmp

mv database.tmp database
