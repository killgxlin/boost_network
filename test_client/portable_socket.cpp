#include "portable_socket.h"

void socket_init() {
#if defined WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(1,1), &wsa_data);
#endif
}

void socket_destroy() {
#if defined WIN32
    WSACleanup();
#endif
}

int get_errno() {
#if defined WIN32 
	return WSAGetLastError();
#else
	return errno;
#endif
}