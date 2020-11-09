#include "common.h"

bool running = false;
pthread_t listenTh;

void ListenThread() {
  while(running) {
    printf("test\n");
    Sleep(5000); 
  }
}

void Startup() {
  running = true;
  pthread_create (&listenTh, NULL, ListenThread, "1")
  printf("XVPN Started\n");
}
void Terminate() {
  running = false;
  printf("XVPN Stopped\n");
}
