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
typedef struct logentry_id {
    // term of this log entry
    uint64_t term;
    // order it was applied to this term's log
    uint32_t master_idx;
    // unique counter on client
    uint32_t client_idx;
} logentry_id;


typedef int (*apply_log_cb)(void *state_machine, struct iovec *entry);


/**
 * State machines do three things:
 * 1)  Have committed log entries applied to them.
 * 2)  Maintain a snapshot
 * 3)  
 */
typedef struct statemachine_ops {
    /** Callback to apply a log to our state machine */
    int(*apply_log_cb)(void *state_machine, struct iovec *entry);

    /** 
     * Optional method.  Optimization for state machines that 
     * employ copy-on-write or another scheme whereby they 
     * can snapshot themselves more cheaply than streaming a full image.
     * 
     * Callback to order the state machine to take a snapshot, 
     * and then delete any previous snapshots before returning. 
     * 
     * If this method is null, the framework will use streamout_snapshot
     * and streamin_snapshot to manage snapshotting.
     */
    int (*take_snapshot_cb)(void *state_machine, logentry_id entry_id);

    /** Callback to retrieve the ID of the current snapshot.  Returns all zeroes if there is none.  */
    logentry_id (*get_last_snapshot_id_cb)(void *state_machine);

    /** Stream out the last snapshot image we took. 
     *  Note:  Not the current state, the last snapshot.  
     *  Note:  This will be called from a separate thread, snapshots need to be threadsafe.  */
    int (*streamout_snapshot_cb)(void *state_machine, int out_fd);


    /** Delete all local state and stream in a snapshot.  */
    int (*streamin_snapshot_cb)(void *state_machine, int in_fd);
} statemachine_ops;

/** Serves raft using the current thread for all calls except streamout_snapshot */
int serve_raft(char *path_to_hosts, void *state_machine, statemachine_ops ops);


#ifdef __cplusplus
}
#endif
