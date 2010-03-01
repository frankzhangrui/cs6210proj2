#include "ringbuffer.h"
#include <math.h>
#include <sys/mman.h>
#include <stdio.h>

void read_request(struct ring_buffer* rbuff) {
  printf("read request\n");
  pthread_mutex_lock(&rbuff->request_mutex);
  int size = rbuff->request_writes - rbuff->request_reads;

  if(size < 1)
    return;

  struct request* next = &rbuff->request_buffer[rbuff->request_reads % MAXSIZE];
  int result = pow(2.0, next->x);
  result = result % next->p;

  rbuff->request_reads++;
  msync(rbuff, sizeof(struct ring_buffer), MS_SYNC | MS_INVALIDATE);
  pthread_mutex_unlock(&rbuff->request_mutex);
  printf("request read\n");

  printf("write response\n");
  pthread_mutex_lock(&rbuff->response_mutex);
  rbuff->response_buffer[rbuff->response_writes % MAXSIZE] = result;
  rbuff->response_writes++;  
  pthread_cond_signal(&next->response_ready);
  msync(rbuff, sizeof(struct ring_buffer), MS_SYNC | MS_INVALIDATE);
  pthread_mutex_unlock(&rbuff->response_mutex);
  printf("response written\n");
}

void process_requests(struct ring_buffer* rbuff) {
  while((rbuff->request_writes - rbuff->request_reads) > 0) 
    read_request(rbuff);
}

void write_request(struct ring_buffer* rbuff, int x, int p, 
		   pthread_cond_t* response_ready) {
  struct request* next;

  pthread_mutex_lock(&rbuff->request_mutex);
  next = &rbuff->request_buffer[rbuff->request_writes % MAXSIZE];
  
  next->x = x;
  next->p = p;
  response_ready =  &next->response_ready;

  rbuff->request_writes++;

  msync(rbuff, sizeof(struct ring_buffer), MS_SYNC | MS_INVALIDATE);
  pthread_cond_signal(&rbuff->nonempty);
  pthread_mutex_unlock(&rbuff->request_mutex);
}

void init_ring_buffer(struct ring_buffer* rbuff) {
  rbuff->request_writes = 0;
  rbuff->request_reads = 0;

  rbuff->response_writes = 0;
  rbuff->response_reads = 0;

  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);  
  pthread_mutex_init(&rbuff->request_mutex, &mattr);
  pthread_mutex_init(&rbuff->response_mutex, &mattr);

  pthread_condattr_t  cattr;
  pthread_condattr_init(&cattr);
  pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(&rbuff->nonempty, &cattr);

  int i;
  for(i = 0; i < MAXSIZE; i++) {
    pthread_cond_init(&rbuff->request_buffer[i].response_ready, &cattr);
  }
}