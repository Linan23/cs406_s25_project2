// wserver.c — multi‐threaded HTTP server with FIFO/SFF scheduling
// Usage: ./wserver [-d basedir] [-p port] [-t threads>0] [-b buffers>0] [-s FIFO|SFF]
//   Master thread: accept() → peek+stat for SFF → enqueue
//   Worker threads: dequeue (FIFO or smallest‐first) → request_handle()
//   Signal handler: on SIGINT/SIGTERM sets stop, closes listen_fd, wakes workers

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h> // for atoi(), exit()
#include <unistd.h> // for getopt(), optarg, getpid()
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h> // off_t
#include <pthread.h>  // pthreads, mutexes, condvars
#include <signal.h>   // for signal handling
#include <string.h>
#include <errno.h>

#include "request.h"
#include "io_helper.h"

#define MAXBUF 8192

char default_root[] = ".";

static volatile sig_atomic_t stop = 0; // flag to signal shutdown
static int listen_fd_global;           // listening socket

void *worker(void *arg);

// Implementation of additional features
int threads = 1;         // number of worker threads
int buffers = 1;         // size of the request queue
char *schedalg = "FIFO"; // fifo or sff

// one request in the queue
struct request_entry
{
  int conn_fd;    // client connection socket
  off_t filesize; // file size for sff
};

// circular buffer, synchronize primitives
struct request_queue
{
  struct request_entry *buf;
  int head, tail, count;
  int capacity;

  pthread_mutex_t mutex;    // protects queue state
  pthread_cond_t not_empty; // workers wait here if count==0
  pthread_cond_t not_full;  // master waits here if count==capacity
} queue;

// external parser for uri
extern int request_parse_uri(
    char *uri,
    char *filename,
    char *cgiargs);

// signal handler for sigint/term
void handle_sigint(int sig)
{
  stop = 1;                                 // stop main loop
  pthread_cond_broadcast(&queue.not_empty); // wake any waiting worker
  if (listen_fd_global >= 0)
  {
    close(listen_fd_global); // close listening socket
    listen_fd_global = -1;   // flag for close
  }
}

// ./wserver [-d <basedir>] [-p <portnum>]
//
int main(int argc, char *argv[])
{
  int c;
  char *root_dir = default_root;
  int port = 10000;

  /* parse flags */
  while ((c = getopt(argc, argv, "d:p:t:b:s:")) != -1)
  {
    switch (c)
    {
    case 'd':
      root_dir = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case 't': // threads
      threads = atoi(optarg);
      break;
    case 'b': // buffer
      buffers = atoi(optarg);
      break;
    case 's': // scheduling
      schedalg = optarg;
      break;
    default:
      fprintf(stderr,
              "usage: wserver [-d basedir] [-p port] "
              "[-t threads] [-b buffers] [-s schedalg]\n");
      exit(1);
    }
  }

  // validate flags
  if (threads < 1 || buffers < 1 ||
      (strcasecmp(schedalg, "FIFO") && strcasecmp(schedalg, "SFF")))
  {
    fprintf(stderr,
            "usage: wserver [-d basedir] [-p port] "
            "[-t threads>0] [-b buffers>0] [-s FIFO|SFF]\n");
    exit(1);
  }

  // change to working dir(root)
  chdir_or_die(root_dir);

  // open listening socket
  listen_fd_global = open_listen_fd_or_die(port);
  int listen_fd = listen_fd_global;
  printf("[pid %d] listening on port %d, root \"%s\"\n",
         getpid(), port, root_dir);
  fflush(stdout);

  // initialize circular buffer
  queue.capacity = buffers;
  queue.head = queue.tail = queue.count = 0;
  queue.buf = malloc(queue.capacity * sizeof *queue.buf);
  if (!queue.buf)
  {
    perror("malloc");
    exit(1);
  }

  // init mutex and condition variables
  if (pthread_mutex_init(&queue.mutex, NULL) != 0)
  {
    perror("pthread_mutex_init");
    exit(1);
  }
  if (pthread_cond_init(&queue.not_empty, NULL) != 0)
  {
    perror("pthread_cond_init not_empty");
    exit(1);
  }
  if (pthread_cond_init(&queue.not_full, NULL) != 0)
  {
    perror("pthread_cond_init not_full");
    exit(1);
  }

  // signal handling for shutdown
  struct sigaction sa = {.sa_handler = handle_sigint};
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  // spawn worker threads
  pthread_t *thread_ids = malloc(threads * sizeof *thread_ids);
  if (!thread_ids)
  {
    perror("malloc");
    exit(1);
  }
  for (int i = 0; i < threads; i++)
  {
    if (pthread_create(&thread_ids[i], NULL, worker, NULL) != 0)
    {
      perror("pthread_create");
      exit(1);
    }
  }

  // accept loop(producer)
  while (!stop)
  {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // accept connection
    int conn_fd = accept(listen_fd,
                         (struct sockaddr *)&client_addr,
                         &client_len);
    if (conn_fd < 0)
    {
      if (stop && (errno == EBADF || errno == EINTR))
        break; // shutdown
      else
        continue; // retry
    }

    // sff: peek at request-line and stat file size
    off_t size = 0;
    if (strcasecmp(schedalg, "SFF") == 0)
    {
      char buf[MAXBUF + 1], *eol;
      int n = recv(conn_fd, buf, MAXBUF, MSG_PEEK);
      if (n > 0)
      {
        buf[n] = '\0';             // make it c-string
        eol = strstr(buf, "\r\n"); // find end of first line
        if (eol)
        {
          *eol = '\0';
          char method[MAXBUF], uri[MAXBUF], version[MAXBUF];
          sscanf(buf, "%s %s %s", method, uri, version);

          // reject ".." in uri, security purposes
          if (strstr(uri, ".."))
          {
            close_or_die(conn_fd);
            continue;
          }
          char filename[MAXBUF], cgiargs[MAXBUF];
          request_parse_uri(uri, filename, cgiargs);
          struct stat sbuf;
          if (stat(filename, &sbuf) == 0)
            size = sbuf.st_size;
        }
      }
    }

    // enqueue request
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == queue.capacity)
      pthread_cond_wait(&queue.not_full, &queue.mutex);
    queue.buf[queue.tail] = (struct request_entry){
        .conn_fd = conn_fd,
        .filesize = size};
    queue.tail = (queue.tail + 1) % queue.capacity;
    queue.count++;
    pthread_cond_signal(&queue.not_empty);
    pthread_mutex_unlock(&queue.mutex);
  }

  // shut down
  pthread_cond_broadcast(&queue.not_empty);
  for (int i = 0; i < threads; i++)
  {
    pthread_join(thread_ids[i], NULL);
  }
  free(thread_ids);

  // clean up
  free(queue.buf);
  if (listen_fd_global >= 0)
  {
    close_or_die(listen_fd_global);
  }
  return 0;
}

// worker thread: dequeue + handle

void *worker(void *arg)
{
  while (!stop || queue.count > 0)
  {
    pthread_mutex_lock(&queue.mutex);
    while (queue.count == 0 && !stop)
      pthread_cond_wait(&queue.not_empty, &queue.mutex);
    if (queue.count == 0 && stop)
    {
      pthread_mutex_unlock(&queue.mutex);
      break;
    }

    // choose index: fifo or sff
    int idx = queue.head;
    if (strcasecmp(schedalg, "SFF") == 0)
    {
      for (int i = 1; i < queue.count; i++)
      {
        int cand = (queue.head + i) % queue.capacity;
        if (queue.buf[cand].filesize < queue.buf[idx].filesize)
          idx = cand;
      }
    }

    // remove entry
    struct request_entry req = queue.buf[idx];
    if (idx == queue.head)
      queue.head = (queue.head + 1) % queue.capacity;
    else
    {
      for (int i = idx; i != queue.head; i = (i - 1 + queue.capacity) % queue.capacity)
        queue.buf[i] = queue.buf[(i - 1 + queue.capacity) % queue.capacity];
      queue.head = (queue.head + 1) % queue.capacity;
    }
    queue.count--;
    pthread_cond_signal(&queue.not_full);
    pthread_mutex_unlock(&queue.mutex);

    // process request
    request_handle(req.conn_fd);
    close_or_die(req.conn_fd);
  }
  return NULL;
}
