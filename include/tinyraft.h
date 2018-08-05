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

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define VERSION 1

#include <stdint.h>
/*
TODO 
add startup methods
start canvasser
wire thru to RPC
*/

/** 128 bit unique ID for a log entry */
typedef struct raft_logentry_id {
    // term of this log entry
    uint64_t term;
    // order it was applied to this term's log
    uint32_t master_idx;
    // unique counter on client
    uint32_t client_idx;
} logentry_id;


/**
 * State machines do three things:
 * 1)  Have committed log entries applied to them.
 * 2)  Maintain a snapshot
 * 3)  
 */
typedef struct raft_statemachine_ops {
    /** Callback to apply a log to our state machine */
    int(*apply_log_cb)(void *state_machine, struct iovec *entry);



    /** Stream out the last snapshot image we took. 
     *  Note:  Must return error if assert_snapshot != the last taken snapshot.
     *  Note:  This will be called from a separate thread, it must be threadsafe.
     */
    int (*streamout_snapshot_cb)(void *state_machine, int out_fd, logentry_id assert_snapshot);


    /** Delete all local state and stream in a snapshot.  */
    int (*streamin_snapshot_cb)(void *state_machine, int in_fd);

    /** 
     * Optional method, can be NULL.  Optimization for state machines that 
     * employ copy-on-write or another scheme whereby they 
     * can snapshot themselves more cheaply than streaming a full image.
     * 
     * Callback to order the state machine to take a snapshot of current state, 
     * store entry_id as the ID of our snapshot, and delete any previous snapshots.  
     * We only maintain 1 snapshot per state machine. 
     * 
     * If this method is null, the framework will use streamout_snapshot
     * and streamin_snapshot to manage snapshotting.
     */
    int (*take_snapshot_cb)(void *state_machine, logentry_id entry_id);

    /**
     * Optional, implement this if you implement take_snapshot_cb
     * Callback to retrieve the ID of the last taken snapshot.
     */
    logentry_id (*get_last_snapshot_id_cb)(void *state_machine);
} statemachine_ops;


/** Represents a member of the cluster. */
typedef struct peer {
  uuid_t peer_id;
  struct sockaddr *addr;
  socklen_t addrlen;
} peer;

/** Serves raft using the current thread for all calls to state machine except streamout_snapshot */
int serve_raft(peer *peers, int num_peers, void *state_machine, statemachine_ops ops);


#ifdef __cplusplus
}
#endif
