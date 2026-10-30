#!/bin/bash
#
# Generates an mbox containing many small messages, to exercise message
# indices that no longer fit into the 16-bit half-word of the old
# (mbox index | message index) packing.
#
# Usage:
#   prepare_mbox_many_messages.sh FILE COUNT
#
# Message numbering starts at 0. All messages share the same addresses and
# subject, so the database (and its dump) stays small; each message carries
# a body keyword derived from (index mod 100). Messages 0, 65535, 65536 and
# COUNT-1 (if these differ) get unique body markers instead, so that single
# messages on either side of the old 65536 boundary can be searched for.
#
# Additionally, the split parts of the marker messages are written to
# messages/mbox_split/<basename-of-FILE>/part.<index>, in the same format
# that split_mbox.sh produces, so that search results for these messages
# can be asserted.
#
set -e

OUT="${1:?usage: prepare_mbox_many_messages.sh FILE COUNT}"
COUNT="${2:?usage: prepare_mbox_many_messages.sh FILE COUNT}"

if [ ! "$COUNT" -ge 1 ] 2>/dev/null
then
    echo "COUNT must be a positive number" >&2
    exit 1
fi

# Directory layout matches what the test framework's add_messages produces
# for mbox sources: the mbox under messages/mbox/, the split parts under
# messages/mbox_split/.
SPLIT_DIR="messages/mbox_split/$(basename "$OUT")"
mkdir -p "$(dirname "$OUT")" "$SPLIT_DIR"

awk -v out="$OUT" -v split_dir="$SPLIT_DIR" -v count="$COUNT" '
BEGIN {
    # header shared by all messages
    hdr = "From: sender@example.com\n" \
          "To: watcher@example.com\n" \
          "Subject: many messages test\n" \
          "Date: Mon, 01 Jan 2024 00:00:00 +0000\n\n"
    # boundary message numbers that get a unique body marker
    m[0] = 1; m[65535] = 1; m[65536] = 1; m[count - 1] = 1;
    for (i = 0; i < count; i++) {
        print "From mairix@test Mon Jan  1 00:00:00 2024" > out
        printf "%s", hdr > out
        if (i in m) {
            if (i == 0)          marker = "markerfirst"
            else if (i == 65535) marker = "markerbeforeboundary"
            else if (i == 65536) marker = "markerafterboundary"
            else                 marker = "markerlast"
            print marker > out
            # split part = the message without its "From " separator line
            print "From: sender@example.com" > split_dir "/part." i
            print "To: watcher@example.com" > split_dir "/part." i
            print "Subject: many messages test" > split_dir "/part." i
            print "Date: Mon, 01 Jan 2024 00:00:00 +0000" > split_dir "/part." i
            print "" > split_dir "/part." i
            print marker > split_dir "/part." i
        } else {
            print "keyword" (i % 100) " filler" > out
        }
        # blank line closing the message (mbox separation)
        print "" > out
    }
}'
