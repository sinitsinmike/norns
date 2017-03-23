#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include "events.h"
#include "input.h"

static pthread_t pid;

#if USE_GETLINE
static void* input_run(void* p) {
  bool quit = false;
  size_t len, dum;
  char* rxbuf = (char*)NULL;
  char* line;
  while(!quit) {
	printf("getting a line...\n");
  	getline(&rxbuf, &dum, stdin);
  	len = strlen(rxbuf);
	printf("getline: strlen = %d\n", len);
  	if(len == 2) {
  	  if(rxbuf[0] == 'q') {
		event_post(event_data_new(EVENT_QUIT));
  		fflush(stdout);
  		quit = true;
  		continue;
  	  }
  	}
  	if (len > 0) {
  	  // event handler must free this chunk!
  	  line = malloc((len+1) * sizeof(char));
  	  strncpy(line, rxbuf, len);
  	  line[len] = '\0';
	  union event_data *ev = event_data_new(EVENT_EXEC_CODE_LINE);
	  ev->exec_code_line.line = line;
	  event_post(ev);
  	}
  }
  free(rxbuf);
}

#else

#define RX_BUF_LEN 4096
static void* input_run(void* p) {
  bool quit = false;
  size_t len, dum;
  char rxbuf[RX_BUF_LEN];
  int nb;
  bool newline;
  char b;
  
  while(!quit) {
	nb = 0;
	newline = false;
	while(!newline) {
	  if(nb < RX_BUF_LEN) {
		read(STDIN_FILENO, &b, 1);
		if(b == '\0') { continue; }
		if(b == '\n') { newline = true; }
		rxbuf[nb++] = b;
	  }
	}
  	if(nb == 2) {
  	  if(rxbuf[0] == 'q') {
		event_post(event_data_new(EVENT_QUIT));
  		fflush(stdout);
  		quit = true;
  		continue;
  	  }
  	}
  	if (nb > 0) {
  	  // event handler must free this chunk!
  	  char* line = malloc((nb+1) * sizeof(char));
  	  strncpy(line, rxbuf, nb);
  	  line[nb] = '\0';
	  union event_data *ev = event_data_new(EVENT_EXEC_CODE_LINE);
	  ev->exec_code_line.line = line;
	  event_post(ev);
  	}
  }
}
#endif

void input_init(void) {
   pthread_attr_t attr;
   int s;
   s = pthread_attr_init(&attr);
   if(s != 0) { printf("input_init(): error in pthread_attr_init(): %d\n", s); }
   s = pthread_create(&pid, &attr, &input_run, NULL);
   if(s != 0) { printf("input_init(): error in pthread_create(): %d\n", s); }
   pthread_attr_destroy(&attr);
}
