/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004,2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **********************************************************************
 */

#ifndef READER_H
#define READER_H

#ifdef HAS_STDINT_H
#include <stdint.h>
#elif defined(HAS_INTTYPES_H)
#include <inttypes.h>
#else
#error "No <stdint.h> or <inttypes.h>"
#endif

/* MX, then a high byte, then the version no. */
#define HEADER_MAGIC0 'M'
#define HEADER_MAGIC1 'X'
#define HEADER_MAGIC2 0xA5
/* Version 4 : sizes, offsets and times are 64-bit (lo/hi word pairs)
 * Version 5 : all header slots are 64-bit (lo/hi word pairs); all
 *             character-data offsets in tables are 64-bit (lo words
 *             followed by hi words, adjacent in one region) */
#define HEADER_MAGIC3 0x05

/*{{{ Constants for file data positions.
 * The header is a table of 64-bit slots; slot i is held as two 32-bit
 * words : lo word at byte 8*i, hi word at byte 8*i+4.
 * Values are either counts or word offsets (in 32-bit word units)
 * pointing at the various tables, except UI_MSG_TYPE_AND_FLAGS which is
 * a byte offset. */

#define UI_MAGIC           0
#define UI_ENDIAN          1
#define UI_N_MSGS          2

/* Byte offset to byte-per-message table encoding message types */
#define UI_MSG_TYPE_AND_FLAGS 3

/* Word offset to table of per-message character offsets (or, for mbox
 * messages, the encoded (mbox, msg) indices) : 2*n_msgs words (n lo words
 * followed by n hi words) */
#define UI_MSG_CDATA       4
/* For maildir/MH : mtime of file containing message.
 * For mbox msgs : the offset into the file.  2*n_msgs words. */
#define UI_MSG_MTIME       5
/* For all formats : message size.  2*n_msgs words. */
#define UI_MSG_SIZE        6
/* These are common to Maildir,MH,mbox messages */
/* Message Date: header as time_t.  2*n_msgs words. */
#define UI_MSG_DATE        7
/* Thread group index table : n_msgs words (32-bit values) */
#define UI_MSG_TID         8

/* Header positions for mbox (file-level) information */
#define UI_MBOX_N          9
#define UI_MBOX_PATHS     10   /* 2*n_mboxen words */
#define UI_MBOX_ENTRIES   11   /* n_mboxen words, 32-bit counts */
#define UI_MBOX_MTIME     12   /* 2*n_mboxen words */
#define UI_MBOX_SIZE      13   /* 2*n_mboxen words */
#define UI_MBOX_CKSUM     14   /* 2*n_mboxen words */
#define UI_HASH_KEY       15

/* Header positions for token tables : 3 slots each, holding
 * (n, word offset of tok offsets (2n), word offset of enc offsets (2n)) */
#define UI_TO_BASE        16
#define UI_CC_BASE        19
#define UI_FROM_BASE      22
#define UI_SUBJECT_BASE   25
#define UI_BODY_BASE      28
#define UI_ATTACHMENT_NAME_BASE 31
/* msg_ids has 4 slots : (n, tok, enc0, enc1) */
#define UI_MSGID_BASE     34

/* Number of 32-bit words in the header */
#define UI_HEADER_LEN     76
#define UC_HEADER_LEN     ((UI_HEADER_LEN) << 2)

#define UI_N_OFFSET        0
#define UI_TOK_OFFSET      1
#define UI_ENC_OFFSET      2

#define UI_TO_N           (UI_TO_BASE + UI_N_OFFSET)
#define UI_TO_TOK         (UI_TO_BASE + UI_TOK_OFFSET)
#define UI_TO_ENC         (UI_TO_BASE + UI_ENC_OFFSET)
#define UI_CC_N           (UI_CC_BASE + UI_N_OFFSET)
#define UI_CC_TOK         (UI_CC_BASE + UI_TOK_OFFSET)
#define UI_CC_ENC         (UI_CC_BASE + UI_ENC_OFFSET)
#define UI_FROM_N         (UI_FROM_BASE + UI_N_OFFSET)
#define UI_FROM_TOK       (UI_FROM_BASE + UI_TOK_OFFSET)
#define UI_FROM_ENC       (UI_FROM_BASE + UI_ENC_OFFSET)
#define UI_SUBJECT_N      (UI_SUBJECT_BASE + UI_N_OFFSET)
#define UI_SUBJECT_TOK    (UI_SUBJECT_BASE + UI_TOK_OFFSET)
#define UI_SUBJECT_ENC    (UI_SUBJECT_BASE + UI_ENC_OFFSET)
#define UI_BODY_N         (UI_BODY_BASE + UI_N_OFFSET)
#define UI_BODY_TOK       (UI_BODY_BASE + UI_TOK_OFFSET)
#define UI_BODY_ENC       (UI_BODY_BASE + UI_ENC_OFFSET)
#define UI_ATTACHMENT_NAME_N    (UI_ATTACHMENT_NAME_BASE + UI_N_OFFSET)
#define UI_ATTACHMENT_NAME_TOK  (UI_ATTACHMENT_NAME_BASE + UI_TOK_OFFSET)
#define UI_ATTACHMENT_NAME_ENC  (UI_ATTACHMENT_NAME_BASE + UI_ENC_OFFSET)
#define UI_MSGID_N        (UI_MSGID_BASE + UI_N_OFFSET)
#define UI_MSGID_TOK      (UI_MSGID_BASE + UI_TOK_OFFSET)
#define UI_MSGID_ENC0     (UI_MSGID_BASE + UI_ENC_OFFSET)
#define UI_MSGID_ENC1     (UI_MSGID_ENC0 + 1)

/*}}}*/

/*{{{ Literals used for encoding messages types in database file */
#define DB_MSG_DEAD 0
/* maildir/MH : one file per message */
#define DB_MSG_FILE 1
/* mbox : multiple files per message */
#define DB_MSG_MBOX 2
/* message comes from IMAP server */
#define DB_MSG_IMAP 3
/*}}}*/

#define FLAG_SEEN    (1<<3)
#define FLAG_REPLIED (1<<4)
#define FLAG_FLAGGED (1<<5)

/* Read the 64-bit value in header slot i */
static inline uint64_t rd_header_slot(const unsigned int *uidata, int slot) {
  return ((uint64_t)(unsigned int)uidata[2*slot]) | (((uint64_t)(unsigned int)uidata[2*slot+1]) << 32);
}
/* Write the 64-bit value into header slot i */
static inline void wr_header_slot(unsigned int *uidata, int slot, uint64_t value) {
  uidata[2*slot]   = (unsigned int) (value & 0xffffffffull);
  uidata[2*slot+1] = (unsigned int) (value >> 32);
}

/* Build a 64-bit value from a table of lo words followed by a table of hi
 * words */
static inline uint64_t rd_lo_hi(const unsigned int *lo, const unsigned int *hi, int i) {
  return ((uint64_t)(unsigned int)lo[i]) | (((uint64_t)(unsigned int)hi[i]) << 32);
}

struct toktable_db {/*{{{*/
  unsigned int n; /* number of entries in this table */
  unsigned int *tok_offsets;      /* lo words of char offsets of token texts */
  unsigned int *tok_offsets_hi;   /* hi words */
  unsigned int *enc_offsets;      /* lo words of char offsets of encodings */
  unsigned int *enc_offsets_hi;   /* hi words */
};
/*}}}*/
struct toktable2_db {/*{{{*/
  unsigned int n; /* number of entries in this table */
  unsigned int *tok_offsets;      /* lo words */
  unsigned int *tok_offsets_hi;   /* hi words */
  unsigned int *enc0_offsets;     /* lo words */
  unsigned int *enc0_offsets_hi;  /* hi words */
  unsigned int *enc1_offsets;     /* lo words */
  unsigned int *enc1_offsets_hi;  /* hi words */
};
/*}}}*/
struct read_db {/*{{{*/
  /* Raw file parameters, needed later for munmap */
  char *data;
  size_t len;

  /* Pathname information */
  int n_msgs;
  unsigned char *msg_type_and_flags;
  unsigned int *path_offsets;      /* lo words : char offset of the path,
                                    * or for mbox messages the packed
                                    * (mbox index | msg index << 32) */
  unsigned int *path_offsets_hi;   /* hi words */
  /* Depending on the message type, mtime_table holds either the mtime of
   * the message file (maildir/MH) or the byte offset of the message inside
   * its mbox file. */
  unsigned int *mtime_table;     /* lo words */
  unsigned int *mtime_hi_table;  /* hi words */
  unsigned int *size_table;      /* lo words : file size or span inside mbox */
  unsigned int *size_hi_table;   /* hi words */
  unsigned int *date_table;      /* lo words */
  unsigned int *date_hi_table;   /* hi words */
  unsigned int *tid_table;

  int n_mboxen;
  unsigned int *mbox_paths_table;      /* lo words */
  unsigned int *mbox_paths_hi_table;   /* hi words */
  unsigned int *mbox_entries_table;    /* 32-bit counts */
  unsigned int *mbox_mtime_table;      /* lo words */
  unsigned int *mbox_mtime_hi_table;
  unsigned int *mbox_size_table;       /* lo words */
  unsigned int *mbox_size_hi_table;
  unsigned int *mbox_checksum_table;   /* lo words */
  unsigned int *mbox_checksum_hi_table;

  unsigned int hash_key;

  struct toktable_db to;
  struct toktable_db cc;
  struct toktable_db from;
  struct toktable_db subject;
  struct toktable_db body;
  struct toktable_db attachment_name;
  struct toktable2_db msg_ids;

};
/*}}}*/

static inline uint64_t rd_msg_start_or_mtime(struct read_db *db, int i) {
  return rd_lo_hi(db->mtime_table, db->mtime_hi_table, i);
}
static inline uint64_t rd_msg_size(struct read_db *db, int i) {
  return rd_lo_hi(db->size_table, db->size_hi_table, i);
}
static inline time_t rd_msg_date(struct read_db *db, int i) {
  return (time_t) rd_lo_hi(db->date_table, db->date_hi_table, i);
}
static inline time_t rd_mbox_mtime(struct read_db *db, int i) {
  return (time_t) rd_lo_hi(db->mbox_mtime_table, db->mbox_mtime_hi_table, i);
}
static inline uint64_t rd_mbox_file_size(struct read_db *db, int i) {
  return rd_lo_hi(db->mbox_size_table, db->mbox_size_hi_table, i);
}
/* Byte offset of message i's path (or its encoded mbox index) in the db */
static inline uint64_t rd_msg_path_offset(struct read_db *db, int i) {
  return rd_lo_hi(db->path_offsets, db->path_offsets_hi, i);
}

struct read_db *open_db(char *filename);
void close_db(struct read_db *x);

static inline int rd_msg_type(struct read_db *db, int i) {
  return db->msg_type_and_flags[i] & 0x7;
}

/* int_list_reader reads a sorted list of integers in a
   variable-length encoding of increments from the previous entry. */
struct int_list_reader {
  unsigned int accumulator;
  const unsigned char *pos;
  const unsigned char *end;
};
struct matches;
void matches_int_list_reader_init(struct int_list_reader *, const struct matches *);
void read_db_int_list_reader_init(
  struct int_list_reader *,
  const struct read_db *,
  uint64_t start_offset
);
/* Returns 0 on end of list or bounds exceeded. */
int int_list_reader_read(struct int_list_reader *, int *result);
/* Consumes the list like int_list_reader_read but copies it
   to a newly initialised struct match. */
void int_list_reader_copy(struct int_list_reader *, struct matches *);

/* Get token from database, respecting file bounds.
   Returns NULL if the database is corrupt. */
const char *get_db_token(const struct read_db *db, uint64_t token_offset);

#endif /* READER_H */
