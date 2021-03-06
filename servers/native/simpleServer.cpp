/*
 * simpleServer.cpp
 *
 * Copyright 2010-2012 Yahoo! Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *  Created on: Aug 11, 2010
 *      Author: sears
 */

#include "simpleServer.h"
#include "requestDispatch.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

typedef struct {
 simpleServer * obj;
 int self;
} worker_arg;

void * worker_wrap(void * arg) {
  ((worker_arg*)arg)->obj->worker(((worker_arg*)arg)->self);
  free(arg);
  return 0;
}

void * simpleServer::worker(int self) {
  int mybufsize =128*1024;
  char * bigbuffer = (char*)malloc(mybufsize);
  pthread_mutex_lock(&thread_mut[self]);
  while(true) {
    while(thread_fd[self] == -1) {
      if(!ltable->accepting_new_requests) {
		free(bigbuffer);
        pthread_mutex_unlock(&thread_mut[self]);
        return 0;
      }
      pthread_cond_wait(&thread_cond[self], &thread_mut[self]);
    }
    pthread_mutex_unlock(&thread_mut[self]);
    FILE * f = fdopen(thread_fd[self], "a+");
    setbuffer(f, bigbuffer, mybufsize);
    while(!requestDispatch<FILE*>::dispatch_request(f, ltable)) { }
    fclose(f);
    pthread_mutex_lock(&thread_mut[self]);
    thread_fd[self] = -1;
  }
}

simpleServer::simpleServer(bLSM * ltable, int max_threads, int port):
  ltable(ltable),
  port(port),
  max_threads(max_threads),
  thread_fd((int*)malloc(sizeof(*thread_fd)*max_threads)),
  thread_cond((pthread_cond_t*)malloc(sizeof(*thread_cond)*max_threads)),
  thread_mut((pthread_mutex_t*)malloc(sizeof(*thread_mut)*max_threads)),
  thread((pthread_t*)malloc(sizeof(*thread)*max_threads)) {
  for(int i = 0; i < max_threads; i++) {
    thread_fd[i] = -2;
    pthread_cond_init(&thread_cond[i], 0);
    pthread_mutex_init(&thread_mut[i], 0);
  }
}

bool simpleServer::acceptLoop() {

  int sockfd;
  struct sockaddr_in serv_addr;
  struct sockaddr_in cli_addr;
  int newsockfd;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd == -1) {
    perror("ERROR opening socket");
    return false;
  }
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // XXX security...
  serv_addr.sin_port = htons(port);

  if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
    perror("ERROR on binding");
    return false;
  }
  if(listen(sockfd,SOMAXCONN)==-1) {
    perror("ERROR on listen");
    return false;
  }
  printf("LSM Server listening....\n");

//  *(sdata->server_socket) = sockfd;

  while(ltable->accepting_new_requests) {
    socklen_t clilen = sizeof(cli_addr);

    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if(newsockfd == -1) {
      perror("ERROR on accept");
    } else {

#ifdef LOGSTORE_NODELAY
      int flag, result;

      flag = 1;
      result = setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
      if(result == -1) {
        perror("ERROR on setting socket option TCP_NODELAY");
        // ignore the error.
      }
#endif
      //      char clientip[20];
      //      inet_ntop(AF_INET, (void*) &(cli_addr.sin_addr), clientip, 20);
      //      printf("Connection from %s\n", clientip);
      int i;
      for(i = 0; i < max_threads; i++) { //TODO round robin or something? (currently, we don't care if connect() is slow...)
        pthread_mutex_lock(&thread_mut[i]);
        if(thread_fd[i] == -2) {  // lazily spawn new threads
          thread_fd[i] = -1;
          worker_arg * arg = (worker_arg*)malloc(sizeof(worker_arg));
          arg->obj = this;
          arg->self = i;
          pthread_create(&thread[i], 0, worker_wrap, (void*)arg);

        }
        if(thread_fd[i] == -1) {
          thread_fd[i] = newsockfd;
          DEBUG("connect %d\n", i);
          pthread_cond_signal(&thread_cond[i]);
          pthread_mutex_unlock(&thread_mut[i]);
          break;
        } else {
          pthread_mutex_unlock(&thread_mut[i]);
        }
      }
      if(i == max_threads) {
        printf("all threads are busy.  rejecting connection\n");
        close(newsockfd);
      }
    }
  }
  return true;
}
simpleServer::~simpleServer() {
  for(int i = 0; i < max_threads; i++) {
    pthread_cond_signal(&thread_cond[i]);
    pthread_mutex_lock(&thread_mut[i]);
    if(thread_fd[i] != -2) {
      pthread_mutex_unlock(&thread_mut[i]);
      pthread_join(thread[i],0);
    } else {
      pthread_mutex_unlock(&thread_mut[i]);
    }
    pthread_mutex_destroy(&thread_mut[i]);
    pthread_cond_destroy(&thread_cond[i]);
  }
  free(thread);
  free(thread_mut);
  free(thread_cond);
  free(thread_fd);
}
