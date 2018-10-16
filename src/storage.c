/*
   Copyright 2018 Google LLC

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   Author Jay Booth
*/

#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sodium.h>
#include "tinyraft.h"
#include "storage.h"
#include "rpc.h"

/*
 * Logs are stored in a directory named after the hex of the term_id (16 chars).
 * We have 2 files, 'entries' and 'answers'.  
 * Entries contains input to the state machine while answers contains output.
 * 
 * Contents of entries file have 3 sections:
 *  term_header    global info, mmapped as struct
 *  entry_md*     entry metadata table, mmappped and indexed by entry_idx
 *  log_content   contents of AppendEntries requests on their way through the system
 * 
 * We also have an answers file,  attached to term_log and pointed at by each entry's 
 * answer_pos, answer_len.
 */

static size_t header_entries_size(int num_entries) {
  return sizeof(term_header) + (sizeof(entry_md) * num_entries);
}

static int close_log(term_log* log) {
  munmap(log->header, log->map_len);
  close(log->entries_fd);
  close(log->answers_fd);
  return 0;
}

// Assumes dest is big enough to hold our full path.
static void build_path(char *dest, const char *basedir, uint64_t term_id, const char *filename) {
  char term_hex[17];
  sodium_bin2hex(term_hex, 17, (uint8_t*) &term_id, 8);

  strcpy(dest, basedir);
  strcat(dest, "/");
  strcat(dest, term_hex);
  strcat(dest, "/");
  strcat(dest, filename);
}

// Opens entries file and sets up mmaps, used by open_log and create_log
static int map_entries(term_log *log, const char *entries_file) {
  term_header header;
  if (read(log->entries_fd, &header, sizeof(term_header)) == -1) { return -1; }
  if (memcmp(header.magic, "RAFT", 4) != 0) {
    return -1;
  }

  size_t map_len = header_entries_size(header.num_entries);
  uint8_t *map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, 0, log->entries_fd, 0);
  if (map == MAP_FAILED) {
    return -1;
  }

  log->map_len = map_len;
  log->header = (term_header*) map;
  log->entries = (entry_md*) (map + sizeof header);
}

int open_log(term_log *log, const char *basedir, uint64_t term_id) {
  char file_path[4096]; // 4096 is excessive, but it's the posix max
  if (strnlen(basedir,4096) > 4000) {
    return -1;
  }
  build_path(file_path, basedir, term_id, "answers");
  log->answers_fd = open(file_path, O_CLOEXEC, O_RDWR);
  if (log->answers_fd == -1) {
    return -1;
  }
  build_path(file_path, basedir, term_id, "entries");
  log->entries_fd = open(file_path, O_CLOEXEC, O_RDWR);
  if (log->entries_fd == -1) {
    return -1;
  }
  return map_entries(log, file_path);
}

int create_log(term_log *log, const char *basedir, uint64_t term_id, uint32_t num_entries) {
  // create/allocate files
  char file_path[4096]; // 4096 is excessive, but it's the posix max
  if (strnlen(basedir,4096) > 4000) {
    return -1;
  }
  build_path(file_path, basedir, term_id, "answers");
  log->answers_fd = open(file_path, O_CLOEXEC | O_CREAT, O_RDWR);
  if (log->answers_fd == -1) { return -1; }
  build_path(file_path, basedir, term_id, "entries");
  log->entries_fd = open(file_path, O_CLOEXEC | O_CREAT, O_RDWR);
  if (log->entries_fd == -1) { return -1; }
  if (posix_fallocate(log->entries_fd, 0, header_entries_size(num_entries)) != 0) {
    return -1;
  }
  // set up mmaps
  if (map_entries(log, file_path) != 0) { return -1; }
  // initialize header
  log->header->term = term_id;
  log->header->num_entries = num_entries;
  log->header->max_committed = 0;
  log->header->quorum_committed = 0;
  memcpy(log->header->magic, "RAFT", 4);
  return fdatasync(log->entries_fd);
}

// We manage 2 log_parts:  current and previous
// When current fills up, we delete previous and create a new one
typedef struct log_store {
  term_log*    previous;
  term_log*    current;
} log_store;
