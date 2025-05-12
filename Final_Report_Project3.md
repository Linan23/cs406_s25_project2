# Final Report: Multi-threaded Web Server with FIFO/SFF Scheduling

## 1. Description of implemented software

Basically extended the provided single-threaded HTTP server(wserver) to support a fixed size pool of worker threads, a bounded circular request queue, and two scheduling policies(FIFO & SFF)

**Which is the following:**

- Master Thread: Accepts incoming TCP connections, could also peeka at the request line to stat() the requested file (for SFF), then enqueue the connection descriptor into a fixed-size circular buffer

- Worker Thread: Each repeatedly dequeues a request (FIFO or SFF), calls request_handle(), and closes the connection

- Signal Handling: Utilizing Sigint/term to initiate clean shutdown,closing the listen socket, waking any blocked threads, joining threads, and freeing resources

- Security: Rejects any uri containing “..” to prevent directory traversal outside the base directory, as per the prompt asked for

## 2. Algorithms and Their Implementation

**Circular Queue(Data Structure):**

What this is: Fixed size array that holds pending requests


struct request_entry { int conn_fd; off_t filesize; };
struct request_queue {
  request_entry *buf;   // array of length capacity
  int head, tail, count;
  int capacity;
  pthread_mutex_t mutex;
  pthread_cond_t  not_empty, not_full;
} queue;

Why? When you reach the end of the array you wrap back to the front, so you never run out of room or have to shuffle the whole thing on every enqueue/dequeue

**Master Thread(Producer)(Data Structure)**

What this is: Only accept new connections and stick them into the queue(no heavy lifting)

New connections:

conn_fd = accept(listen_fd, …);

(SFF) Peek & Stat:

off_t size = 0;
if (schedalg == SFF) {
  recv(conn_fd, buf, MAXBUF, MSG_PEEK);
  parse URI from buf;
  if (!strstr(uri, "..")) {
    stat(filename, &sbuf);
    size = sbuf.st_size;
  }
}

Enqueue(block only if full)

lock(queue.mutex)
while queue.count == queue.capacity:
  cond_wait(queue.not_full, queue.mutex)
queue.buf[queue.tail] = { conn_fd, size }
queue.tail = (queue.tail + 1) % queue.capacity
queue.count += 1
cond_signal(queue.not_empty)
unlock(queue.mutex)

Why? This way will keep the accept path pretty simple, push actual file reads or cgi spawns into the worker threads, and never loses connections(just blocks)

**Worker Threads(Consumer)(Data Structure)**

What this is: Wait until there’s work, pick one request, and fully service it

***Waiting for work:***

Description: Lock the mutex, if count == 0 and not shutting down, sleep on not_empty and if woke up and there is no work , unlock and exit the thread cleanly

lock(queue.mutex)
while queue.count == 0 and not shutting_down:
  cond_wait(queue.not_empty, queue.mutex)
if queue.count == 0 and shutting_down:
  unlock(queue.mutex)
  exit thread

***Select between FIFO/SFF***

idx = queue.head //FIFO, just grab the one at head

//SFF, scan all count entries starting at head to find smallest filesize

idx = queue.head 
for i in 1 .. queue.count-1:
  cand = (queue.head + i) % queue.capacity
  if queue.buf[cand].filesize < queue.buf[idx].filesize:
    idx = cand

***Remove Entries***

Description: Pull the chosen entry out of the circular buffer, shift things after removing, decrement count, signal not_full, unlock the mutex

req = queue.buf[idx]
if idx == queue.head:
  queue.head = (queue.head + 1) % queue.capacity
else:
  // shift entries between head and idx forward by one
  for j = idx; j != queue.head; j = (j-1+capacity)%capacity:
    queue.buf[j] = queue.buf[(j-1+capacity)%capacity]
  queue.head = (queue.head + 1) % queue.capacity
queue.count -= 1
cond_signal(queue.not_full)
unlock(queue.mutex)

***Service request***

Description: Call request_handle() on requested connection, reads the HTTP request, opens or fork cgi, writes the response, then close the socket

request_handle(req.conn_fd);
close_or_die(req.conn_fd);

**Scheduling Policies:**

FIFO: Workers always take the entry at queue.head. O(1)

SFF: Master will Peek & Stat to record the requests file size before enqueue, workers scan the buffer for the smallest file size entry, remove it, and shift the remaining entries to fill the hole


## 3. Individual Contributions

Anthony: Implemented the synchronization with mutexes and condition variables, features for the Master/Worker threads, Scheduling Policies(FIFO & SFF), and handling of security details and handling for graceful shutdowns.

Eric: Added command line parsing for -t,-b, and -s flags. Implemented circular buffer, as well as Unit testing & error handling the code ensuring that everything is robust, and cleaning up/documenting the code to make it more organized and detailed.

## 4. Usage Documentation

**Building/Setting Up**

In the Terminal type: Make clean && Make all

Throught doing this, it will reset the environment and create the necessary exe.: wserver, wclient, spin.cgi, and sql.cgi

**Command Line Flags**

./wserver [-d basedir] [-p port] [-t threads] [-b buffers] [-s schedalg]

-d basedir : Root directory for file lookups, directory must exist prior to running, any uri with .. is rejected (Default .)

-p port : TCP port on which the server listens (Default 10000)

-t threads : 	Number of worker threads in the pool (must be >0) (Default 1)

-b buffers : Size of the circular request queue; master blocks if full (must be >0)(Default 1)

-s schedalg : Scheduling policy: FIFO or SFF (Default FIFO)

**Testing**

You may go into the terminal and input: Make Test

This will run the bash script "test_server.sh", inside contains a series of test from muti threading features to scheduling policies and shutdowns. You may also run manual checks through following similar inputs as shown in the bash script.
