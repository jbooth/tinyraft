#include <string.h>
#include "wiretypes.h"
#include "buffers.h"
#include "raftlet.h"
#include "raftlet_state.h"

static int handle_append_entry(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    // check that we think is leader

    // check if need to start new termlog

    // append to current
    return 0;
}

static int handle_new_entry(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    pthread_mutex_lock(&raftlet->state.guard);
    // check we are leader
    
    // transcode

    // check if need to start new termlog

    pthread_mutex_unlock(&raftlet->state.guard);
    // append to current


    return 0;
}

const uint8_t traft_null_pubkey[32] = {0};

static int handle_request_vote(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_req *header, traft_buff *req, traft_resp *resp) {
    traft_reqvote_req *reqvote_req = (traft_reqvote_req*) header;
    
    traft_vote_resp *vote_resp = (traft_vote_resp*) resp;
    
    pthread_mutex_lock(&raftlet->state.guard);
    // If the proposed entry is more recent than our max, and we haven't already voted for someone else..
    if (reqvote_req->proposed_term > raftlet->state.current_term_id && 
        reqvote_req->last_log_term >= raftlet->state.max_committed_local.term_id &&
        reqvote_req->last_log_idx >= raftlet->state.max_committed_local.idx && 
        (memcmp(client->client_id, raftlet->state.last_voted_for, sizeof(traft_publickey_t)) == 0  ||
        memcmp(raftlet->state.last_voted_for, traft_null_pubkey, sizeof(traft_publickey_t)) == 0)) {
        // Vote yes
        vote_resp->vote_granted = 1;
    } else {
        // Vote no
        vote_resp->vote_granted = 0;
        vote_resp->current_term = raftlet->state.current_term_id;
    }
    pthread_mutex_unlock(&raftlet->state.guard);

    return 0;
}

int traft_raftlet_handle_req(traft_raftlet_s *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    traft_req req_header = traft_buff_get_header(req);
    switch (req_header.info.req_type) {
        case TRAFT_REQTYPE_APPENDENTRY:
            return handle_append_entry(raftlet, client, req, resp);
        case TRAFT_REQTYPE_REQVOTE:
            return handle_request_vote(raftlet, client, &req_header, req, resp);
        case TRAFT_REQTYPE_NEWENTRY:
            return handle_new_entry(raftlet, client, req, resp);
        default:
            break;
    }
    // Unrecognized request type
    return -1;
}

