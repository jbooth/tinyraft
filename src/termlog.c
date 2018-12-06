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
#include "termlog.h"

/* Design:
 * 
 * Logs are stored in a directory named after the hex of the term_id (16 chars).
 * We have 2 files, 'entries' and 'answers'.  
 * Entries contains input to the state machine while answers contains output.
 * 
 * Contents of entries file have 3 sections:
 *  log_header    global info, mmapped as struct
 *  log_entry_md*     entry metadata table, mmappped and indexed by entry_idx
 *  log_content   contents of AppendEntries requests on their way through the system
 * 
 * We also have an answers file,  attached to term_log and pointed at by each entry's 
 * answer_pos, answer_len.
 */

static size_t header_entries_size(int num_entries) {
  return sizeof(traft_log_header) + (sizeof(traft_log_entry_md) * num_entries);
}

static int close_log(traft_termlog *log) {
  munmap(log->header, log->map_len);
  close(log->entries_fd);
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

// Opens entries file, sets up mmaps, inits locks, used by open_log and create_log
static int init_termlog(traft_termlog *log, const char *entries_file) {
  traft_log_header header;
  if (read(log->entries_fd, &header, sizeof(traft_log_header)) == -1) { return -1; }
  if (memcmp(header.magic, "RAFT", 4) != 0) {
    return -1;
  }

  size_t map_len = header_entries_size(header.max_entries);
  uint8_t *map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, 0, log->entries_fd, 0);
  if (map == MAP_FAILED) {
    return -1;
  }

  log->map_len = map_len;
  log->header = (traft_log_header*) map;
  log->entries = (traft_log_entry_md*) (map + sizeof(header));
  // TODO set up cfg and termkey
  return traft_rwlock_init(&log->lock);
}

int traft_termlog_open(traft_termlog *log, const char *basedir, uint64_t term_id) {
  char file_path[4096]; // 4096 is excessive, but it's the posix max
  if (strnlen(basedir,4096) > 4000) {
    return -1;
  }
  build_path(file_path, basedir, term_id, "entries");
  log->entries_fd = open(file_path, O_CLOEXEC, O_RDWR);
  if (log->entries_fd == -1) {
    return -1;
  }
  return init_termlog(log, file_path);
}

int traft_termlog_create(traft_termlog *log, const char *basedir, uint64_t term_id, uint32_t num_entries) {
  // create/allocate files
  char file_path[4096]; // 4096 is excessive, but it's the posix max
  if (strnlen(basedir,4096) > 4000) {
    return -1;
  }
  build_path(file_path, basedir, term_id, "entries");
  log->entries_fd = open(file_path, O_CLOEXEC | O_CREAT, O_RDWR);
  if (log->entries_fd == -1) { return -1; }
  if (posix_fallocate(log->entries_fd, 0, header_entries_size(num_entries)) != 0) {
    return -1;
  }
  // set up mmaps
  if (init_termlog(log, file_path) != 0) { return -1; }
  // Initialize header and fsync
  log->header->term = term_id;
  log->header->max_entries = num_entries;
  log->header->local_committed_idx = 0;
  log->header->quorum_committed_idx = 0;
  memcpy(log->header->magic, "RAFT", 4);
  return fdatasync(log->entries_fd);
}

static int append_entry_internal(traft_termlog *log, traft_appendentry_req *header, traft_buff *entry) {
  uint32_t prev_idx = log->header->local_committed_idx;
  off_t    next_entry_pos = log->entries[prev_idx].entry_pos + log->entries[prev_idx].entry_len;
  
  // Assert we can add this entry
  if (header->prev_idx != prev_idx) {
    return -1;
  }
  if (header->prev_idx + 1 == log->header->max_entries) {
    return -1;
  }
  // Execute write
  off_t prev_eof = lseek(log->entries_fd, 0, SEEK_END);
  if (prev_eof == -1) { return -1; }
  if (traft_write_all(log->entries_fd, header, RPC_REQ_LEN) == -1) {
    return -1;
  }
  if (traft_write_all(log->entries_fd, entry, header->info.body_len) == -1) {
    return -1;
  }
  // Update entries and notify readers
  traft_rwlock_wrlock(&log->lock);
  log->header->local_committed_idx++;
  log->entries[header->this_idx].entry_pos = (uint32_t)prev_eof;
  log->entries[header->this_idx].entry_len = RPC_REQ_LEN + header->info.body_len;
  traft_rwlock_wrunlock(&log->lock);
  // Fsync before return
  if (fdatasync(log->entries_fd) == -1) {
    return -1; // couldn't fsync, go die
  }
  return 0;
}

int traft_termlog_wait_more(traft_termlog *log, uint32_t last_idx, uint32_t *newest_idx, int max_wait_ms) {
  struct timeval start;
  gettimeofday(&start, NULL);
  traft_rwlock_rdlock(&log->lock);
  *newest_idx = log->header->local_committed_idx;
  struct timeval now;
  gettimeofday(&now, NULL);
  int64_t elapsed_ms = (now.tv_sec - start.tv_sec) * 1000;
  elapsed_ms += (now.tv_usec - start.tv_usec) / 1000;

  while (*newest_idx <= last_idx && elapsed_ms < max_wait_ms) {
    // Wait on cond
    struct timespec wait_time;
    wait_time.tv_sec = 0;
    wait_time.tv_nsec = 0;
    if (max_wait_ms > elapsed_ms) { 
      wait_time.tv_nsec = (max_wait_ms - elapsed_ms) * 1000000;
    }
    // TODO writelock
    //pthread_cond_timedwait(&log->entries_changed, &log->entries_lock, wait_time);
    // Get vals to recheck condition
    *newest_idx = log->header->local_committed_idx;
    gettimeofday(&now, NULL);
    elapsed_ms = (now.tv_sec - start.tv_sec) * 1000;
    elapsed_ms += (now.tv_usec - start.tv_usec) / 1000;
  }
  traft_rwlock_rdunlock(&log->lock);
  return 0;
}
