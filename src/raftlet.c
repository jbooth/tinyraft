#include "wiretypes.h"
#include "buffers.h"
#include "raftlet.h"
#include "raftlet_state.h"

static int handle_append_entry(traft_raftlet *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    return 0;
}

static int handle_new_entry(traft_raftlet *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    return 0;
}

static int handle_request_vote(traft_raftlet *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    traft_reqvote_req reqvote_req = (traft_reqvote_req) traft_buff_get_header(req);
    traft_vote_resp *vote_resp = (traft_vote_resp*) resp;
    
    pthread_mutex_lock(&raftlet->state.guard);
    if (reqvote_req.proposed_term > raftlet->state.current_term_id && 
        reqvote_req.last_log_term >= raftlet->state.max_committed_local.term_id && 
        reqvote_req.last_log_idx >= raftlet->state.) {

    }
    pthread_mutex_unlock(&raftlet->state.guard);

    return 0;
}

int traft_raftlet_handle_req(traft_raftlet *raftlet, traft_conninfo_t *client, traft_buff *req, traft_resp *resp) {
    traft_req req_header = traft_buff_get_header(req);
    switch (req_header.info.req_type) {
        case TRAFT_REQTYPE_APPENDENTRY:
            return handle_append_entry(raftlet, client, req, resp);
        case TRAFT_REQTYPE_REQVOTE:
            return handle_request_vote(raftlet, client, req, resp);
        case TRAFT_REQTYPE_NEWENTRY:
            return handle_new_entry(raftlet, client, req, resp);
    }
    // Unrecognized request type
    return -1;
}

