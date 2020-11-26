#include "common.h"

int running = 0;
int listen_socket = 0;

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

void echo(const char* msg) {
    char cmd[256];
    sprintf(cmd, "echo \"%s\"", msg);
    system(cmd);
}

struct Client {
  int socket;
  struct sockaddr_in endpoint;
};

void endptostr(struct sockaddr_in* addr, char* out) {
  char ip[32];
  inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
  sprintf(out, "%X::%s:%i", addr->sin_family, ip, htons(addr->sin_port));
}

int tconnect(int sockno, struct sockaddr* addr, size_t addrlen, struct timeval* timeout) {
	int res;

  int opt = fcntl(sockno, F_GETFL, NULL);
	if (opt < 0)
		return -1;
  else if (fcntl(sockno, F_SETFL, opt | O_NONBLOCK) < 0) // set non-blocking
		return -1;

  res = connect(sockno, addr, addrlen);
  
	if (res < 0) {
		if (errno == EINPROGRESS) {
			fd_set wait_set;

			// make file descriptor set with socket
			FD_ZERO (&wait_set);
			FD_SET (sockno, &wait_set);

			// wait for socket to be writable; return after given timeout
			res = select (sockno + 1, NULL, &wait_set, NULL, timeout);
		}
	}
	else {
		res = 1;
	}

	// reset socket flags
	if (fcntl (sockno, F_SETFL, opt) < 0) {
		return -1;
	}

	if (res < 0)
		return -1;
	else if (res == 0) {
		errno = ETIMEDOUT;
		return 1;
	}
	else {
		socklen_t len = sizeof(opt);

		if (getsockopt (sockno, SOL_SOCKET, SO_ERROR, &opt, &len) < 0)
			return -1;

		if (opt) {
			errno = opt;
			return -1;
		}
	}

	return 0;
}

int IsSocketConnected(int socket) {
    return recv(socket, NULL, 1, MSG_PEEK | MSG_DONTWAIT) != 0;
}

void* ClientThread(void* arg) {
  struct Client* c = (struct Client*)arg;
  int client_socket = (int)c->socket; // connection to vpn client
  struct sockaddr_in client_endpoint = c->endpoint; // endpoint to client
  int dest_socket = 0; // connection to destination
  struct sockaddr_in dest_endpoint; // endpoint to destination
  
  free(c);
  
  if (recv(client_socket, &dest_endpoint, sizeof(dest_endpoint), NULL) != sizeof(dest_endpoint)){
    char err[256]; char endp[64];
    endptostr(&client_endpoint, endp);
    sprintf(err, "%s failed to send destination details, connection has been closed", endp);
    echo(err);
    close(client_socket);
    return 0;
  }
  
  char err[256]; char endp1[64]; char endp2[64];
  endptostr(&client_endpoint, endp1);
  endptostr(&dest_endpoint, endp2);
  sprintf(err, "%s forwarding to %s", endp1, endp2);
  echo(err);
  
  const char* okMsg = "XVPN_OK";
  const char* errMsg = "XVPN_ER";
  
  dest_socket = socket(dest_endpoint.sin_family, SOCK_STREAM, 0);
  if (dest_socket < 0) {
    char err[256]; char endp[64];
    endptostr(&client_endpoint, endp);
    sprintf(err, "%s failed to create destination socket (%s), connection has been closed", endp, strerror(errno));
    echo(err);
    send(client_socket, errMsg, strlen(errMsg), NULL);
    return;
  }
  
  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  if (tconnect(dest_socket, (const struct sockaddr*)&dest_endpoint, sizeof(dest_endpoint), &tv) != 0) {
  //if (connect(dest_socket, (const struct sockaddr*)&dest_endpoint, sizeof(dest_endpoint)) != 0) {
    char err[256]; char endp[64];
    endptostr(&client_endpoint, endp);
    sprintf(err, "%s failed to connect to destination (%s), connection has been closed", endp, strerror(errno));
    echo(err);
    send(client_socket, errMsg, strlen(errMsg), NULL);
    return;
  }
  
  send(client_socket, okMsg, strlen(okMsg), NULL);
  
  sprintf(err, "%s binded with %s successfully", endp1, endp2);
  echo(err);
  
  int lastComm = clock();
  char data[0x1000];
  while (running == 1 && (IsSocketConnected(client_socket) || (clock() - lastComm) < 120000)) {    
    int sent = 0, recvd = 0;
    
    recvd = recv(client_socket, data, sizeof(data), MSG_DONTWAIT | MSG_NOSIGNAL);    
    if (recvd > 0) {
      char n[256];
      sent = send(dest_socket, data, recvd, MSG_DONTWAIT | MSG_NOSIGNAL);
      sprintf(n, "%s: c->s: 0x%X bytes, sent 0x%X", endp1, recvd, sent);
      echo(n);
      lastComm = clock();
    }
    
    recvd = recv(dest_socket, data, sizeof(data), MSG_DONTWAIT | MSG_NOSIGNAL);    
    if (recvd > 0){
      char n[256];
      sent = send(client_socket, data, recvd, MSG_DONTWAIT | MSG_NOSIGNAL);
      sprintf(n, "%s: s->c: 0x%X bytes, sent 0x%X", endp1, recvd, sent);
      echo(n);
      lastComm = clock();
    }
    
    msleep(10);
  }
  
  char err4[256]; char endp[64];
  endptostr(&client_endpoint, endp);
  sprintf(err4, "Session with %s terminated", endp);
  echo(err4);
    
  close(client_socket);
  close(dest_socket);
  return 0;
}

void Startup() {
  if (running != 0)
    return;
      /*
  struct sockaddr_in dest_endpoint;
	dest_endpoint.sin_family = AF_INET;
	dest_endpoint.sin_port = htons(5211);
  inet_pton(AF_INET, "142.202.188.226", &dest_endpoint.sin_addr);
  
  char endp[64];
  endptostr(&dest_endpoint, endp);
  
  int dest_socket = socket(dest_endpoint.sin_family, SOCK_STREAM, 0);
  if (dest_socket < 0) {
    char err[256];
    sprintf(err, "%s failed to create destination socket (%s), connection has been closed", endp, strerror(errno));
    echo(err);
    return;
  }
  
  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  if (tconnect(dest_socket, (const struct sockaddr*)&dest_endpoint, sizeof(dest_endpoint), &tv) != 0) {
  //if (connect(dest_socket, (const struct sockaddr*)&dest_endpoint, sizeof(dest_endpoint)) != 0) {
    char err[256];
    sprintf(err, "%s failed to connect to destination socket (%s), connection has been closed", endp, strerror(errno));
    echo(err);
    return;
  }
  
    char err[256];
    sprintf(err, "%s success (%s), connection has been closed", endp, strerror(errno));
    echo(err);
    */
  listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket < 0) {
    printf("Failed to create socket (%s)\n", strerror(errno));
    return;
  }
  
  struct sockaddr_in in;
	in.sin_family = AF_INET;
 	in.sin_addr.s_addr = INADDR_ANY;
	in.sin_port = htons(25640);
 
  if (bind(listen_socket, (const struct sockaddr*)&in, sizeof(in)) < 0) {
    printf("Failed to bind socket (%s)\n", strerror(errno));
    return;
  }
  else if (listen(listen_socket, 5) < 0) {
    printf("Failed to start socket listen (%s)\n", strerror(errno));
    return;
  }
  
  running = 1;
  printf("XVPN Started\n");
  
  while (running == 1) {
    struct sockaddr_in client_endpoint;
    int client_endpoint_size = sizeof(client_endpoint);
    int client_socket = accept(listen_socket, (struct sockaddr*)&client_endpoint, (socklen_t*)&client_endpoint_size);
    
    if (client_socket < 0) {
      printf("Failed to accept incoming client (%s)\n", strerror(errno));
      return;
    }
    
    char notify[256]; char ip[32];
    inet_ntop(AF_INET, &client_endpoint.sin_addr, ip, INET_ADDRSTRLEN);
    sprintf(notify, "New connection from %s:%i", ip, client_endpoint.sin_port);
    echo(notify);
    
    //printf("New connection from");
    
    struct Client* c = (struct Client*)malloc(sizeof(struct Client));
    c->socket = client_socket;
    c->endpoint = client_endpoint;

    pthread_t cth;
    pthread_create(&cth, NULL, ClientThread, (void*)c);
      
    /*char ip[32];
    //inet_ntop(AF_INET, &client_endpoint.sin_addr, ip, INET_ADDRSTRLEN);

    printf("New connection from %s:%i", ip, client_endpoint.sin_port);*/
    
    msleep(50);
  }
}
void Terminate() {
  if (running == 0)
    return;

  close(listen_socket);

  running = 0;
  printf("XVPN Stopped\n");
}
