#!/bin/bash
#
# Generates a directory with many mbox files, each containing a single
# message, to exercise mbox indices that no longer fit into the 16-bit
# half-word of the old (mbox index | message index) packing.
#
# Usage:
#   prepare_many_mboxes.sh DIR COUNT
#
# The mboxes are named m000000 .. m<COUNT-1>, so that mairix's (sorted)
# wildcard expansion produces a deterministic order. Message numbering
# within each mbox starts at 0.
#
# The mboxes with numbers 0, 65535, 65536 and COUNT-1 (if these differ) get
# a unique body marker (mbxmarkerfirst, mbxmarkerbeforeboundary,
# mbxmarkerafterboundary, mbxmarkerlast), so that single mboxes on either
# side of the old 65536 boundary can be searched for. For these mboxes, the
# split parts of their messages are written to
# messages/mbox_split/manymboxes/m<N>/part.0, in the same format that
# split_mbox.sh produces, so that search results for them can be asserted.
#
set -e

DIR="${1:?usage: prepare_many_mboxes.sh DIR COUNT}"
COUNT="${2:?usage: prepare_many_mboxes.sh DIR COUNT}"

if [ ! "$COUNT" -ge 1 ] 2>/dev/null
then
    echo "COUNT must be a positive number" >&2
    exit 1
fi

# Directory layout matches what the test framework's add_messages produces
# for mbox sources: the mboxes under messages/mbox/, the split parts under
# messages/mbox_split/.
SPLIT_DIR="messages/mbox_split/manymboxes"
mkdir -p "$DIR"

# The mboxes that get a body marker also get their split part written; the
# parts live one subdirectory per mbox, which has to be created up front
# (awk cannot create directories).
MARKER_MBOXES=""
for M in 0 65535 65536 $((COUNT - 1))
do
    if [ "$M" -lt "$COUNT" ]
    then
	mkdir -p "$SPLIT_DIR/$(printf 'm%06d' "$M")"
	MARKER_MBOXES="${MARKER_MBOXES}:${M}"
    fi
done
MARKER_MBOXES="${MARKER_MBOXES#:}"

# One print (plus explicit close) per file: without the close, awk's
# limited table of open files makes this loop thrash for large counts.
awk -v dir="$DIR" -v split_dir="$SPLIT_DIR" -v count="$COUNT" -v markers="$MARKER_MBOXES" '
BEGIN {
    # boundary mbox numbers that get a unique body marker; the numbers are
    # passed as a colon-separated list, the keys of m[] are the numbers
    # themselves
    n_markers = split(markers, marker_list, ":")
    for (j = 1; j <= n_markers; j++) {
        m[marker_list[j]] = 1
    }
    for (i = 0; i < count; i++) {
        name = sprintf("%s/m%06d", dir, i)
        if (i in m) {
            if (i == 0)          marker = "mbxmarkerfirst"
            else if (i == 65535) marker = "mbxmarkerbeforeboundary"
            else if (i == 65536) marker = "mbxmarkerafterboundary"
            else                 marker = "mbxmarkerlast"
        } else {
            marker = "keywordA"
        }
        msg = "From mairix@test Mon Jan  1 00:00:00 2024\n" \
              "From: sender@example.com\n" \
              "To: watcher@example.com\n" \
              "Subject: many mboxes test\n" \
              "Date: Mon, 01 Jan 2024 00:00:00 +0000\n\n" \
              marker " filler\n\n"
        print msg > name
        close(name)
        # split part = the message without its "From " separator line
        if (i in m) {
            part = sprintf("%s/m%06d/part.0", split_dir, i)
            print "From: sender@example.com\n" \
                  "To: watcher@example.com\n" \
                  "Subject: many mboxes test\n" \
                  "Date: Mon, 01 Jan 2024 00:00:00 +0000\n\n" \
                  marker " filler\n" > part
            close(part)
        }
    }
}'
