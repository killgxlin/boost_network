#include "portable_socket.h"

#include <string.h>
#include <stdio.h>

int proc_arguments(int n, char* args[])
{
    if(2 == n) {
        if(0 == strcmp(args[1], "-s"))
            return 1;
        else if(0 == strcmp(args[1], "-c"))
            return 2;
    }

    return 0;
}

int start_server()
{
	struct sockaddr_in addr, r_addr;
    SOCKET    s, t;
    int       r;
    socklen_t len = sizeof(r_addr);

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(INVALID_SOCKET == s) {
        perror("Could not create socket");
        return -1;
    }

	memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(6868);

    r = bind(s, (struct sockaddr*)&addr, sizeof(addr));
    if(SOCKET_ERROR == r) {
        perror("Could not bind to local socket");
        closesocket(s);
        return -1;
    }

	r = listen(s, SOMAXCONN);
    if(SOCKET_ERROR == r) {
        perror("Could not listen to local socket");
        closesocket(s);
        return -1;
    }

    printf("Waiting for connection... ");
    t = accept(s, (struct sockaddr*)&r_addr, &len);
    if(INVALID_SOCKET == t) {
        perror("Could not accept new connection");
        closesocket(s);
        return -1;
    }
    printf("accepted.\n");

	char data[] = "Hello client!\0";
    char recieved[256];
    memset((void*)recieved, 0, sizeof(recieved));
    send(t, data, strlen(data), 0);
    recv(t, recieved, sizeof(recieved), 0);

    printf("Client returned: %s\n", recieved);

	closesocket(t);
    return 0;
}

int start_client()
{
	struct sockaddr_in addr;
    SOCKET   s;
    int      r;
    hostent* h;
    const char local_host[] = "localhost";

    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = inet_addr(local_host);
    if(INADDR_NONE == addr.sin_addr.s_addr) {
        h = gethostbyname(local_host);
        if(NULL == h) {
            perror("Could not get host by name");
            return -1;
        }
    } else {
        h = gethostbyaddr((const char*)&addr.sin_addr, sizeof(struct sockaddr_in), AF_INET);
        if(NULL == h) {
            perror("Could not get host by address");
            return -1;
        }
    }

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(INVALID_SOCKET == s) {
        perror("Could not create socket");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr   = *((in_addr*)*h->h_addr_list);
    addr.sin_port   = htons(6868);

    printf("Connecting... ");
    r = connect(s, (sockaddr*)&addr, sizeof(struct sockaddr));
    if(SOCKET_ERROR == r) {
        perror("Cannot connect to server");
        closesocket(s);
        return -1;
    }
    printf("connected.\n");

	char data[] = "Hello server!\0";
    char recieved[256];
    memset((void*)recieved, 0, sizeof(recieved));
    recv(s, recieved, sizeof(recieved), 0);
    printf("Server sent: %s\n", recieved);
    send(s, data, strlen(data), 0);

    closesocket(s);

    return 0;
}

int main2(int argc, char** argv) {

	socket_init();

	 switch(proc_arguments(argc, argv))
    {
    case 0:
        printf("Usage: socket_tutorial [option]\n");
        printf("Options:\n");
        printf("-c - start a client\n");
        printf("-s - start a server\n\n");
        break;
    case 1:
        start_server();
        break;
    case 2:
        start_client();
        break;
    }

	socket_destroy();

	return 0;
}