#!/bin/bash
#
# Generates an mbox whose byte offsets exceed the range of 32-bit values,
# to exercise the 64-bit message offsets and sizes in the database.
#
# Usage:
#   prepare_mbox_huge_offsets.sh FILE
#
# The file contains three messages:
#   msg 0 : starts at offset 0, its body is extended to just below 4.25 GiB
#           by a hole in the file (reads back as zero bytes), so its size
#           exceeds 2^31.
#   msg 1 : starts above the 4 GiB boundary (offset > 2^32).
#   msg 2 : follows msg 1.
#
# The hole makes the file sparse: it only occupies a few blocks on disk,
# but its size (and the message offsets inside it) exceed the 32-bit range.
#
# The split parts of messages 1 and 2 (the messages that can be searched
# for quickly) are written to messages/mbox_split/<basename>/part.<n> in
# the format produced by split_mbox.sh.
#
set -e

OUT="${1:?usage: prepare_mbox_huge_offsets.sh FILE}"
SPLIT_DIR="messages/mbox_split/$(basename "$OUT")"

# msg 1 must start above 2^32 = 4294967296. Give msg 0 a body reaching
# 4300000000 bytes, so msg 1 starts beyond the 32-bit range with margin.
MSG1_SIZE=4300000000

HDR0="From mairix@test Mon Jan  1 00:00:00 2024
From: sender0@example.com
To: watcher@example.com
Subject: huge offsets test msg0
Date: Mon, 01 Jan 2024 00:00:00 +0000

"

# Write msg 0's header, then extend the file to MSG1_SIZE bytes (creating a
# sparse hole), then a newline closing the hole (mbox separators are
# "From " lines at the start of a line, so the hole has to be closed with
# a newline before msg 1's separator), then msgs 1 and 2.
mkdir -p "$(dirname "$OUT")"
printf '%s' "$HDR0" > "$OUT"
truncate -s "$MSG1_SIZE" "$OUT"
printf '\n' >> "$OUT"

MSG1="From mairix@test Mon Jan  1 00:00:00 2024
From: sender1@example.com
To: watcher@example.com
Subject: huge offsets test msg1
Date: Mon, 01 Jan 2024 00:00:00 +0000

hugemsgone
"
printf '%s' "$MSG1" >> "$OUT"
# the blank line closing msg 1 (part of msg 1's stored span; the separator
# for the next "From " line)
printf '\n' >> "$OUT"

MSG2="From mairix@test Mon Jan  1 00:00:00 2024
From: sender2@example.com
To: watcher@example.com
Subject: huge offsets test msg2
Date: Mon, 01 Jan 2024 00:00:00 +0000

hugemsgtwo
"
printf '%s' "$MSG2" >> "$OUT"
# the blank line closing msg 2 (part of msg 2's stored span)
printf '\n' >> "$OUT"

# split parts for the searchable messages = message without its "From "
# separator line (as split_mbox.sh produces them). Note: printf (not a
# here-string) feeds sed, because a here-string would append an extra
# trailing newline to the message.
mkdir -p "$SPLIT_DIR"
printf '%s' "$MSG1" | sed '1d' > "$SPLIT_DIR/part.1"
printf '%s' "$MSG2" | sed '1d' > "$SPLIT_DIR/part.2"
