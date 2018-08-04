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


#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <pthread.h>

#include "rpc.h"

int tinyraft_non_function(int i) {
  for (int j = 0 ; j < 5 ; j++) {
    i++;
  }
  return i + 1;
}

// utils
static ssize_t writev_full(int fd, struct iovec *iov, int count) {
	while (count) {
		ssize_t w = writev(fd, iov, count);
    if (w == count) { return w; }

		if (w == -1) {
			if (errno == EWOULDBLOCK) {sleep(1); continue; }
			return -1; 
		}

    // partial write, mark off whole iovecs while io_len < w
		while (iov->iov_len <= (size_t)w) {
			w -= iov->iov_len;
			iov++; 
      count -= iov->iov_len;
		}

    // mark off last partially written iovec
		if (count) {
			iov->iov_base += w;
			iov->iov_len -= w;
		}
	}
	return 0;
}

static ssize_t write_full(int fd, const uint8_t *output, int count) {
  while (count) {
    ssize_t w = write(fd, output, count);
    if (w == -1) {
      if (errno == EWOULDBLOCK) { sleep(1); continue; }
      return -1;
    }
    output += w;
    count -= w;
  }
}

static ssize_t read_full(int fd, void* buf, int count) {
  while (count) {
    ssize_t r = read(fd, buf, count);
    if (r == -1) {
      if (errno == EWOULDBLOCK) { sleep(1); continue; }
      return -1;
    }
    count += r;
    buf += r;
  }
}

// client impl

struct rpc_client {
  uint32_t            next_reqno;      // incremented on send
  uint32_t            last_resp_reqno; // incremented on recv
  int sock_fd;
};

struct ts_rpc_client {
  rpc_client          client;
  pthread_mutex_t     write_lock;
  pthread_mutex_t     read_lock;
  pthread_cond_t      last_resp_changed;
};

resp_future send_request(rpc_client* client, generic_req* req) {
  resp_future resp;
  // set reqno on header and resp
  uint32_t next_reqno = client->next_reqno++;
  req->info.reqno = next_reqno;
  resp.reqno      = next_reqno;
  // write it
  resp.written = write_full(client->sock_fd, (uint8_t*)req, RPC_REQ_LEN);
  return resp; 
}

resp_future send_request_ts(ts_rpc_client *client, generic_req* header, struct iovec* body, int bodycnt) {
  resp_future resp;
  // TODO make new iovec with header and body
  pthread_mutex_lock(&client->write_lock);
  // header
  resp = send_request(&client->client, header);
  if (resp.written < 0) { goto DONE; }
  // body
  ssize_t w = writev_full(client->client.sock_fd, body, bodycnt);
  if (w < 0) { goto DONE; }
  resp.written += w;
  DONE:
  pthread_mutex_unlock(&client->write_lock);
  return resp;
}

int init_client(rpc_client* client, struct sockaddr* addr, socklen_t addrlen) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, SOCK_CLOEXEC);
  if (sock_fd <= 0) { return sock_fd; }
  int err = connect(sock_fd, addr, addrlen);
  if (err == -1) { return -1; }
  client->sock_fd = sock_fd;
  client->next_reqno = 0;
  client->last_resp_reqno = 0;
  return 0;
}

// resp_buffer should be 32 bytes long
ssize_t await(rpc_client* client, resp_future* future, generic_resp* resp_buffer) {
  if (client->next_reqno != future->reqno) {
    return -1; // TODO error codes
  }
  return read_full(client->sock_fd, future, RPC_RESP_LEN);
}

int await_ts(resp_future* future, uint8_t* resp_buffer) {
  // TODO
  return 0;
}

// server impl

#define MAX_CLIENTS 31
#define MAX_EVENTS  32 // +1 for acceptor

// track fds so we can make sure all are closed
struct fd_set {
  int fds[MAX_CLIENTS];
  int count;
};

// Adds the client or returns -1 if we're already full
static int add_client(struct fd_set *fds, int fd) {
  if (fds->count == MAX_CLIENTS) {
    return -1;
  }
  fds->fds[fds->count] = fd;
  fds->count++;
  return 0;
}

static void kill_client(struct fd_set *fds, int fd) {
  if (fd) { close(fd); }
  // don't need to handle epoll_del, happens on close

  // find fd_idx in the array
  int fd_idx = -1;
  for (int i = 0 ; i < fds->count ; i++) {
    if (fds->fds[i] == fd) {
      fd_idx = i;
      break;
    }
  }
  // move top element to our last position and decrement count
  if (fd_idx >= 0) {
    int last_idx = fds->count - 1;
    fds->fds[fd_idx] = fds->fds[last_idx];
    fds->fds[last_idx] = -1;
    fds->count--;
  }
}

void close_all(struct fd_set *fds) {
  for (int i = 0 ; i < fds->count ; i++) {
    int fd = fds->fds[i];
    if (fd) {
      close(fd);
    }
    fds->fds[i] = -1;
  }
  fds->count = 0;
}

static int epoll_ctl_pollin(int epoll_fd, int client_fd, int op) {
  struct epoll_event ev;
  ev.data.fd = client_fd;
  ev.events = EPOLLIN;
  return epoll_ctl(epoll_fd, op, client_fd, &ev); 
}

static int bind_server_sock(int accept_fd, struct sockaddr *addr, socklen_t addrlen) {
  int err = bind(accept_fd, addr, addrlen);
  if (err < 0) { return err; } 
  return listen(accept_fd, 10);
}

static int serve_request(int client_fd, server_handler handler) {
  uint8_t req_buff[RPC_REQ_LEN];
  uint8_t resp_buff[RPC_RESP_LEN];

  ssize_t n = read_full(client_fd, &req_buff[0], RPC_REQ_LEN);
  if (n < RPC_REQ_LEN) { return -1; }

  int handled = handler(&req_buff[0], &resp_buff[0]);
  if (handled < 0) { return handled; }

  n = write_full(client_fd, &resp_buff[0], RPC_RESP_LEN);
  if (n < RPC_RESP_LEN) { return -1; }
  return 0;
}

struct server_state {
  int accept_fd;
  int epoll_fd;
  struct sockaddr* addr;
  socklen_t addrlen;
};

void serve_clients(int accept_fd, struct sockaddr* addr, socklen_t addrlen, server_handler handler) {
  struct epoll_event epoll_events[MAX_EVENTS];
  memset(&epoll_events, 0, sizeof(epoll_events) * MAX_EVENTS);

  struct fd_set fds;
  for (int i = 0 ; i < MAX_CLIENTS ; i++) {
    fds.fds[i] = -1;
  }

  int epoll_fd = epoll_create(MAX_CLIENTS);
  if (epoll_fd < 0) { goto DIE; }
  if (accept_fd < 0) { goto DIE; }

  if (epoll_ctl_pollin(epoll_fd, accept_fd, EPOLL_CTL_ADD) == -1) { goto DIE; }
  
  while (1) {
    int num_events = epoll_wait(epoll_fd, &epoll_events[0], MAX_EVENTS, 100);
    if (num_events < 0) { goto DIE; }

    for (int i = 0 ; i < num_events ; i++) {
      struct epoll_event new_event = epoll_events[i];
      if (new_event.data.fd == accept_fd) {
        // it was our accept socket, so accept
        int new_fd = accept(accept_fd, addr, &addrlen);
        if (new_fd < 0) { goto DIE; }

        if (fds.count == MAX_CLIENTS) {
          kill_client(&fds, new_fd);
          continue;
        }

        if (epoll_ctl_pollin(epoll_fd, new_fd, EPOLL_CTL_ADD) == -1) {
          kill_client(&fds, new_fd);
          continue;
        }
      } else {
        // Existing client, handle request or hangup 
        int client_fd = new_event.data.fd;
        if (new_event.events & EPOLLHUP) { 
          kill_client(&fds, client_fd); 
          continue;
        } 
        if (serve_request(client_fd, handler) < 0) { 
          kill_client(&fds, client_fd); 
          continue;
        }
      }
    }
  }
  DIE:
  if (accept_fd) { close(accept_fd); }
  if (epoll_fd) { close(epoll_fd); }
  close_all(&fds);
}
