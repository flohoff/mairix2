/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005,2006
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

/* Write the database to disc. */

#include "mairix.h"
#include "reader.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

struct write_map_toktable {/*{{{*/

  /* Table of character offsets to null-terminated token texts */
  int tok_offset;

  /* Table of character offsets to byte strings containing compressed
   * delta-encoding of file indices matching the token */
  int enc_offset;
};/*}}}*/
struct write_map_toktable2 {/*{{{*/

  /* Table of character offsets to null-terminated token texts */
  int tok_offset;

  /* Table of character offsets to byte strings containing compressed
   * delta-encoding of file indices matching the token */
  int enc0_offset;
  int enc1_offset;
};/*}}}*/

struct write_map {/*{{{*/
/* Contain offset information for the various tables.
   UI stuff in 4 byte units rel to base addr.
   Char stuff in byte units rel to base addr. */

  /* Path information */
  int path_offset;
  /* Each of the following tables holds lo words followed by a table of hi
   * words, to store 64-bit values. */
  int mtime_offset; /* Message file mtimes (maildir/mh), mbox start offset (mbox) */
  int size_offset; /* Message sizes (maildir/mh), span in mbox (mbox) */
  int date_offset; /* Message dates (all folder types) */
  int tid_offset;  /* Thread group index table (all folder types) */

  int mbox_paths_offset;
  int mbox_entries_offset;
  int mbox_mtime_offset;
  int mbox_size_offset;
  /* Character offset to checksum of first msg in the mbox.  Positions of
   * subsequent messages computed by indexing - no explicit table entries
   * anywhere. */
  int mbox_checksum_offset;

  struct write_map_toktable to;
  struct write_map_toktable cc;
  struct write_map_toktable from;
  struct write_map_toktable subject;
  struct write_map_toktable body;
  struct write_map_toktable attachment_name;
  struct write_map_toktable2 msg_ids;

  /* To get base address for character data */
  int beyond_last_ui_offset;
};
/*}}}*/

/* Split a 64-bit value into the lo/hi word tables; the hi words live at
 * lo_table[n] onwards */
static void write_lo_hi(unsigned int *lo_table, int n, int i, uint64_t value)/*{{{*/
{
  lo_table[i] = (unsigned int) (value & 0xffffffffull);
  lo_table[n + i] = (unsigned int) (value >> 32);
}
/*}}}*/

static void create_rw_mapping(char *filename, size_t len, int *out_fd, char **out_data)/*{{{*/
{
  int fd;
  char *data;
  struct stat sb;

  fd = open(filename, O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    report_error("open", filename);
    unlock_and_exit(2);
  }

  if (fstat(fd, &sb) < 0) {
    report_error("stat", filename);
    unlock_and_exit(2);
  }

  if (sb.st_size < (off_t) len) {
    /* Extend */
    if (lseek(fd, (off_t) len - 1, SEEK_SET) < 0) {
      report_error("lseek", filename);
      unlock_and_exit(2);
    }
    if (write(fd, "\000", 1) < 0) {
      report_error("write", filename);
      unlock_and_exit(2);
    }
  } else if (sb.st_size > len) {
    /* Truncate */
    if (ftruncate(fd, len) < 0) {
      report_error("ftruncate", filename);
      unlock_and_exit(2);
    }
  } else {
    /* Exactly the right length already - nothing to do! */
  }

  data = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    report_error("writer:mmap", filename);
    unlock_and_exit(2);
  }

  *out_data = data;
  *out_fd = fd;
}
/*}}}*/

static size_t toktable_char_length(struct toktable *tab)/*{{{*/
{
  int result = 0;
  int i;
  for (i=0; i<tab->size; i++) {
    if (tab->tokens[i]) {
      result += (1 + strlen(tab->tokens[i]->text));
      result += (1 + tab->tokens[i]->match0.n);
    }
  }
  return result;
}
/*}}}*/
static size_t toktable2_char_length(struct toktable2 *tab)/*{{{*/
{
  int result = 0;
  int i;
  for (i=0; i<tab->size; i++) {
    if (tab->tokens[i]) {
      result += (1 + strlen(tab->tokens[i]->text));
      result += (1 + tab->tokens[i]->match0.n);
      result += (1 + tab->tokens[i]->match1.n);
    }
  }
  return result;
}
/*}}}*/
static size_t char_length(struct database *db)/*{{{*/
{
  /* Return total length of character data to be written. */
  size_t result;
  int i;

  result = 0;

  /* For type table. */
  result += db->n_msgs;

  for (i=0; i<db->n_msgs; i++) {
    switch (db->type[i]) {
      case MTY_DEAD:
        break;
      case MTY_MBOX:
        result += sizeof(uint64_t);
        break;
      case MTY_FILE:
      case MTY_IMAP:
        assert(db->msgs[i].src.mpf.path);
        result += (1 + strlen(db->msgs[i].src.mpf.path));
        break;
    }
  }

  for (i=0; i<db->n_mboxen; i++) {
    struct mbox *mb = &db->mboxen[i];
    result += mb->n_msgs * sizeof(checksum_t);
    if (mb->path) {
      result += (1 + strlen(mb->path));
    }
  }

  result += toktable_char_length(db->to);
  result += toktable_char_length(db->cc);
  result += toktable_char_length(db->from);
  result += toktable_char_length(db->subject);
  result += toktable_char_length(db->body);
  result += toktable_char_length(db->attachment_name);
  result += toktable2_char_length(db->msg_ids);

  return result;
}
/*}}}*/

static void compute_mapping(struct database *db, struct write_map *map)/*{{{*/
{
  /* All values are 32-bit word offsets.  64-bit values (mtimes/offsets,
   * sizes, dates and all character data offsets) are stored as a table of
   * lo words followed by a table of hi words. */
  size_t total = UI_HEADER_LEN;

  map->path_offset  = total, total += 2 * db->n_msgs; /* lo words + hi words */
  map->mtime_offset = total, total += 2 * db->n_msgs;
  map->date_offset  = total, total += 2 * db->n_msgs;
  map->size_offset  = total, total += 2 * db->n_msgs;
  map->tid_offset   = total, total += db->n_msgs;

  map->mbox_paths_offset = total, total += 2 * db->n_mboxen;
  map->mbox_entries_offset = total, total += db->n_mboxen;
  map->mbox_mtime_offset = total, total += 2 * db->n_mboxen;
  map->mbox_size_offset  = total, total += 2 * db->n_mboxen;
  map->mbox_checksum_offset = total, total += 2 * db->n_mboxen;

  map->to.tok_offset = total, total += 2 * db->to->n;
  map->to.enc_offset = total, total += 2 * db->to->n;

  map->cc.tok_offset = total, total += 2 * db->cc->n;
  map->cc.enc_offset = total, total += 2 * db->cc->n;

  map->from.tok_offset = total, total += 2 * db->from->n;
  map->from.enc_offset = total, total += 2 * db->from->n;

  map->subject.tok_offset = total, total += 2 * db->subject->n;
  map->subject.enc_offset = total, total += 2 * db->subject->n;

  map->body.tok_offset = total, total += 2 * db->body->n;
  map->body.enc_offset = total, total += 2 * db->body->n;

  map->attachment_name.tok_offset = total, total += 2 * db->attachment_name->n;
  map->attachment_name.enc_offset = total, total += 2 * db->attachment_name->n;

  map->msg_ids.tok_offset = total, total += 2 * db->msg_ids->n;
  map->msg_ids.enc0_offset = total, total += 2 * db->msg_ids->n;
  map->msg_ids.enc1_offset = total, total += 2 * db->msg_ids->n;

  map->beyond_last_ui_offset = total;
}
/*}}}*/
static void write_header(char *data, unsigned int *uidata, struct database *db, struct write_map *map)/*{{{*/
{
  /* Endianness-independent writes - at least the magic number will be
   * recognized if the database is read by this program on a machine of
   * opposite endianness. */
  unsigned char *ucdata = (unsigned char *) data;

  ucdata[0] = HEADER_MAGIC0;
  ucdata[1] = HEADER_MAGIC1;
  ucdata[2] = HEADER_MAGIC2;
  ucdata[3] = HEADER_MAGIC3;

  wr_header_slot(uidata, UI_ENDIAN, 0x44332211); /* For checking reversed endianness on read */
  wr_header_slot(uidata, UI_N_MSGS, db->n_msgs);
  wr_header_slot(uidata, UI_MSG_CDATA, map->path_offset); /* offset table of ptrs to filenames */
  wr_header_slot(uidata, UI_MSG_MTIME, map->mtime_offset); /* lo words of mtime / mbox offset table */
  wr_header_slot(uidata, UI_MSG_DATE, map->date_offset); /* lo words of Date: header lines as time_t */
  wr_header_slot(uidata, UI_MSG_SIZE, map->size_offset); /* lo words of table of message sizes in bytes */
  wr_header_slot(uidata, UI_MSG_TID, map->tid_offset); /* offset of table of thread group numbers */

  wr_header_slot(uidata, UI_MBOX_N, db->n_mboxen);
  wr_header_slot(uidata, UI_MBOX_PATHS, map->mbox_paths_offset);
  wr_header_slot(uidata, UI_MBOX_ENTRIES, map->mbox_entries_offset);
  wr_header_slot(uidata, UI_MBOX_MTIME, map->mbox_mtime_offset); /* lo words */
  wr_header_slot(uidata, UI_MBOX_SIZE, map->mbox_size_offset); /* lo words */
  wr_header_slot(uidata, UI_MBOX_CKSUM, map->mbox_checksum_offset);

  wr_header_slot(uidata, UI_HASH_KEY, db->hash_key);

  wr_header_slot(uidata, UI_TO_N, db->to->n);
  wr_header_slot(uidata, UI_TO_TOK, map->to.tok_offset);
  wr_header_slot(uidata, UI_TO_ENC, map->to.enc_offset);

  wr_header_slot(uidata, UI_CC_N, db->cc->n);
  wr_header_slot(uidata, UI_CC_TOK, map->cc.tok_offset);
  wr_header_slot(uidata, UI_CC_ENC, map->cc.enc_offset);

  wr_header_slot(uidata, UI_FROM_N, db->from->n);
  wr_header_slot(uidata, UI_FROM_TOK, map->from.tok_offset);
  wr_header_slot(uidata, UI_FROM_ENC, map->from.enc_offset);

  wr_header_slot(uidata, UI_SUBJECT_N, db->subject->n);
  wr_header_slot(uidata, UI_SUBJECT_TOK, map->subject.tok_offset);
  wr_header_slot(uidata, UI_SUBJECT_ENC, map->subject.enc_offset);

  wr_header_slot(uidata, UI_BODY_N, db->body->n);
  wr_header_slot(uidata, UI_BODY_TOK, map->body.tok_offset);
  wr_header_slot(uidata, UI_BODY_ENC, map->body.enc_offset);

  wr_header_slot(uidata, UI_ATTACHMENT_NAME_N, db->attachment_name->n);
  wr_header_slot(uidata, UI_ATTACHMENT_NAME_TOK, map->attachment_name.tok_offset);
  wr_header_slot(uidata, UI_ATTACHMENT_NAME_ENC, map->attachment_name.enc_offset);

  wr_header_slot(uidata, UI_MSGID_N, db->msg_ids->n);
  wr_header_slot(uidata, UI_MSGID_TOK, map->msg_ids.tok_offset);
  wr_header_slot(uidata, UI_MSGID_ENC0, map->msg_ids.enc0_offset);
  wr_header_slot(uidata, UI_MSGID_ENC1, map->msg_ids.enc1_offset);

  return;
}
/*}}}*/
static char *write_type_and_flag_table(struct database *db, unsigned int *uidata, char *data, char *cdata)/*{{{*/
{
  int i;
  for (i=0; i<db->n_msgs; i++) {
    struct msgpath *msgdata = db->msgs + i;
    switch (db->type[i]) {
      case MTY_FILE:
        cdata[i] = DB_MSG_FILE;
        break;
      case MTY_MBOX:
        cdata[i] = DB_MSG_MBOX;
        break;
      case MTY_DEAD:
        cdata[i] = DB_MSG_DEAD;
        break;
      case MTY_IMAP:
        cdata[i] = DB_MSG_IMAP;
        break;
    }

    if (msgdata->seen)    cdata[i] |= FLAG_SEEN;
    if (msgdata->replied) cdata[i] |= FLAG_REPLIED;
    if (msgdata->flagged) cdata[i] |= FLAG_FLAGGED;
  }
  wr_header_slot(uidata, UI_MSG_TYPE_AND_FLAGS, (size_t) (cdata - data));
  return cdata + db->n_msgs;
}
/*}}}*/
static char *write_messages(struct database *db, struct write_map *map, unsigned int *uidata, char *data, char *cdata)/*{{{*/
{
  int i;
  char *start_cdata = cdata;

  for (i=0; i<db->n_msgs; i++) {
    int slen;
    switch (db->type[i]) {
      case MTY_FILE:
        slen = strlen(db->msgs[i].src.mpf.path);
        write_lo_hi(uidata + map->path_offset, db->n_msgs, i, (size_t) (cdata - data));
        write_lo_hi(uidata + map->mtime_offset, db->n_msgs, i, (uint64_t) db->msgs[i].src.mpf.mtime);
        write_lo_hi(uidata + map->size_offset, db->n_msgs, i, db->msgs[i].src.mpf.size);
        write_lo_hi(uidata + map->date_offset, db->n_msgs, i, (uint64_t) db->msgs[i].date);
        uidata[map->tid_offset + i]  = db->msgs[i].tid;
        memcpy(cdata, db->msgs[i].src.mpf.path, 1 + slen); /* include trailing null */
        cdata += (1 + slen);
        break;
      case MTY_IMAP:
        slen = strlen(db->msgs[i].src.mpf.path);
        write_lo_hi(uidata + map->path_offset, db->n_msgs, i, (size_t) (cdata - data));
        write_lo_hi(uidata + map->date_offset, db->n_msgs, i, (uint64_t) db->msgs[i].date);
        uidata[map->tid_offset + i]  = db->msgs[i].tid;
        memcpy(cdata, db->msgs[i].src.mpf.path, 1 + slen); /* include trailing null */
        cdata += (1 + slen);
        break;
      case MTY_MBOX:
        {
          int mbno = db->msgs[i].src.mbox.file_index;
          int msgno = db->msgs[i].src.mbox.msg_index;
          struct mbox *mb = &db->mboxen[mbno];
          write_lo_hi(uidata + map->path_offset, db->n_msgs, i, encode_mbox_indices(mbno, msgno));
          write_lo_hi(uidata + map->mtime_offset, db->n_msgs, i, mb->start[msgno]);
          write_lo_hi(uidata + map->size_offset, db->n_msgs, i, mb->len[msgno]);
          write_lo_hi(uidata + map->date_offset, db->n_msgs, i, (uint64_t) db->msgs[i].date);
          uidata[map->tid_offset + i]  = db->msgs[i].tid;
        }
        break;
      case MTY_DEAD:
        write_lo_hi(uidata + map->path_offset, db->n_msgs, i, 0); /* Can't ever happen for real */
        write_lo_hi(uidata + map->mtime_offset, db->n_msgs, i, 0); /* For cleanliness */
        write_lo_hi(uidata + map->size_offset, db->n_msgs, i, 0);  /* For cleanliness */
        /* The following line is necessary, otherwise 'random' tid
         * information is written to the database, which can crash the search
         * functions. */
        uidata[map->tid_offset + i]  = db->msgs[i].tid;
        break;
    }
  }
  if (verbose) {
    printf("Wrote %d messages (%d bytes of tables, %d bytes of text)\n",
           db->n_msgs, 4*8*db->n_msgs, (int)(cdata - start_cdata));
  }
  return cdata; /* new value */
}
/*}}}*/
#if 0
static int compare_tokens(const void *a, const void *b)/*{{{*/
{
  const struct token **aa = (const struct token **) a;
  const struct token **bb = (const struct token **) b;
  return strcmp((*aa)->text, (*bb)->text);
}
/*}}}*/
#endif

static  char *write_mbox_headers(struct database *db, struct write_map *map, unsigned int *uidata, char *data, char *cdata)/*{{{*/
{
  int i, len;
  char *start_cdata = cdata;

  for (i=0; i<db->n_mboxen; i++) {
    struct mbox *mb = &db->mboxen[i];
    uidata[map->mbox_entries_offset + i] = mb->n_msgs;
    write_lo_hi(uidata + map->mbox_mtime_offset, db->n_mboxen, i, (uint64_t) mb->current_mtime);
    write_lo_hi(uidata + map->mbox_size_offset, db->n_mboxen, i, mb->current_size);
    if (mb->path) {
      write_lo_hi(uidata + map->mbox_paths_offset, db->n_mboxen, i, (size_t) (cdata - data));
      len = strlen(mb->path);
      memcpy(cdata, mb->path, 1+len);
      cdata += 1+len;
    } else {
      write_lo_hi(uidata + map->mbox_paths_offset, db->n_mboxen, i, 0);
    }
  }
  if (verbose) {
    printf("Wrote %d mbox headers (%d bytes of tables, %d bytes of paths)\n",
        db->n_mboxen, 4*8*db->n_mboxen, (int)(cdata - start_cdata));
  }
  return cdata;
}
/*}}}*/
static char * write_mbox_checksums(struct database *db, struct write_map *map, unsigned int *uidata, char *data, char *cdata)/*{{{*/
{
  int i, j;
  char *start_cdata = cdata;

  for (i=0; i<db->n_mboxen; i++) {
    struct mbox *mb = &db->mboxen[i];
    write_lo_hi(uidata + map->mbox_checksum_offset, db->n_mboxen, i, (size_t) (cdata - data));
    for (j=0; j<mb->n_msgs; j++) {
      memcpy(cdata, mb->check_all[j], sizeof(checksum_t));
      cdata += sizeof(checksum_t);
    }
  }
  if (verbose) {
    printf("Wrote %d bytes of mbox message checksums\n",
           (int)(cdata - start_cdata));
  }
  return cdata;
}
/*}}}*/

static char *write_toktable(struct toktable *tab, struct write_map_toktable *map, unsigned int *uidata, char *data, char *cdata, char *header_name)/*{{{*/
{
  int i, j, n, max;
  char *start_cdata, *mid_cdata;
  struct token **stok;
  stok = new_array(struct token *, tab->n);
  max = tab->size;
  n = tab->n;

  for (i=0, j=0; i<max; i++) {
    struct token *tok = tab->tokens[i];
    if (tok) {
      stok[j++] = tok;
    }
  }

  assert(j == n);

#if 0
  /* The search functions don't rely on the tokens being sorted.  So not
   * sorting here will save time. */
  qsort(stok, n, sizeof(struct token *), compare_tokens);
#endif

  start_cdata = cdata;

  /* FIXME : Eventually, the tokens have to be sorted - need to feed them from
   * a different data structure (array with no holes) */
  for (i=0; i<n; i++) {
    int slen;
    uidata[map->tok_offset + i] = cdata - data;
    slen = strlen(stok[i]->text);
    memcpy(cdata, stok[i]->text, 1 + slen);
    cdata += (1 + slen);
  }

  mid_cdata = cdata;

  for (i=0; i<n; i++) {
    int dlen;
    dlen = stok[i]->match0.n;
    write_lo_hi(uidata + map->enc_offset, n, i, (size_t) (cdata - data));
    memcpy(cdata, stok[i]->match0.msginfo, dlen);
    cdata += dlen;
    *cdata++ = 0xff; /* termination character */
  }

  if (verbose) {
    printf("%s: Wrote %d tokens (%d bytes of tables, %d bytes of text, %d bytes of hit encoding)\n",
            header_name, n, 2*4*n, (int)(mid_cdata - start_cdata), (int)(cdata - mid_cdata));
  }

  free(stok);
  return cdata;
}
/*}}}*/
static char *write_toktable2(struct toktable2 *tab, struct write_map_toktable2 *map, unsigned int *uidata, char *data, char *cdata, char *header_name)/*{{{*/
{
  int i, j, n, max;
  char *start_cdata, *mid_cdata;
  struct token2 **stok;
  stok = new_array(struct token2 *, tab->n);
  max = tab->size;
  n = tab->n;

  for (i=0, j=0; i<max; i++) {
    struct token2 *tok = tab->tokens[i];
    if (tok) {
      stok[j++] = tok;
    }
  }

  assert(j == n);

#if 0
  /* The search functions don't rely on the tokens being sorted.  So not
   * sorting here will save time. */
  qsort(stok, n, sizeof(struct token *), compare_tokens);
#endif

  start_cdata = cdata;

  /* FIXME : Eventually, the tokens have to be sorted - need to feed them from
   * a different data structure (array with no holes) */
  for (i=0; i<n; i++) {
    int slen;
    write_lo_hi(uidata + map->tok_offset, n, i, (size_t) (cdata - data));
    slen = strlen(stok[i]->text);
    memcpy(cdata, stok[i]->text, 1 + slen);
    cdata += (1 + slen);
  }

  mid_cdata = cdata;

  for (i=0; i<n; i++) {
    int dlen;
    dlen = stok[i]->match0.n;
    write_lo_hi(uidata + map->enc0_offset, n, i, (size_t) (cdata - data));
    memcpy(cdata, stok[i]->match0.msginfo, dlen);
    cdata += dlen;
    *cdata++ = 0xff; /* termination character */
  }

  for (i=0; i<n; i++) {
    int dlen;
    dlen = stok[i]->match1.n;
    write_lo_hi(uidata + map->enc1_offset, n, i, (size_t) (cdata - data));
    memcpy(cdata, stok[i]->match1.msginfo, dlen);
    cdata += dlen;
    *cdata++ = 0xff; /* termination character */
  }

  if (verbose) {
    printf("%s: Wrote %d tokens (%d bytes of tables, %d bytes of text, %d bytes of hit encoding)\n",
            header_name, n, 2*4*n, (int)(mid_cdata - start_cdata), (int)(cdata - mid_cdata));
  }

  free(stok);
  return cdata;
}
/*}}}*/
void write_database(struct database *db, char *filename, int do_integrity_checks)/*{{{*/
{
  size_t file_len;
  int fd;
  char *data, *cdata;
  unsigned int *uidata;
  struct write_map map;

  if (do_integrity_checks) {
    check_database_integrity(db);
  }

  if (!verify_mbox_size_constraints(db)) {
    unlock_and_exit(1);
  }

  /* Work out mappings */
  compute_mapping(db, &map);

  file_len = (size_t) char_length(db) + (4 * (size_t) map.beyond_last_ui_offset);

  create_rw_mapping(filename, file_len, &fd, &data);
  uidata = (unsigned int *) data; /* align(int) < align(page)! */
  cdata = data + (4 * (size_t) map.beyond_last_ui_offset);

  /* Zero the whole UI table region.  The table writers only store the words
   * they need (e.g. lo words of values below 2^32), so hi-word slots must be
   * reset - stale bytes from a previous database state at the same location
   * would otherwise be read back as part of 64-bit values. */
  memset(uidata + UI_HEADER_LEN, 0, 4 * (size_t) (map.beyond_last_ui_offset - UI_HEADER_LEN));

  write_header(data, uidata, db, &map);
  cdata = write_type_and_flag_table(db, uidata, data, cdata);
  cdata = write_messages(db, &map, uidata, data, cdata);
  cdata = write_mbox_headers(db, &map, uidata, data, cdata);
  cdata = write_mbox_checksums(db, &map, uidata, data, cdata);
  cdata = write_toktable(db->to, &map.to, uidata, data, cdata, "To");
  cdata = write_toktable(db->cc, &map.cc, uidata, data, cdata, "Cc");
  cdata = write_toktable(db->from, &map.from, uidata, data, cdata, "From");
  cdata = write_toktable(db->subject, &map.subject, uidata, data, cdata, "Subject");
  cdata = write_toktable(db->body, &map.body, uidata, data, cdata, "Body");
  cdata = write_toktable(db->attachment_name, &map.attachment_name, uidata, data, cdata, "Attachment Name");
  cdata = write_toktable2(db->msg_ids, &map.msg_ids, uidata, data, cdata, "(Threading)");

  /* Write data */
  /* Unmap / close file */
  if (munmap(data, file_len) < 0) {
    report_error("munmap", filename);
    unlock_and_exit(2);
  }
  if (fsync(fd) < 0) {
    report_error("fsync", filename);
    unlock_and_exit(2);
  }
  if (close(fd) < 0) {
    report_error("close", filename);
    unlock_and_exit(2);
  }
}
  /*}}}*/
