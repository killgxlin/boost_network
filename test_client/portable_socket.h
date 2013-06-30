#ifndef PORTABLE_SOCKET_H
#define PORTABLE_SOCKET_H

#if defined WIN32
	#pragma comment(lib, "Ws2_32.lib")
	#include <winsock.h>
#elif defined __linux__
	#include <unistd.h>
#endif

#if defined WIN32
	typedef int socklen_t;
#elif defined __linux__
	typedef int SOCKET;
	#define INVALID_SOCKET -1
	#define SOCKET_ERROR   -1
	#define closesocket(s) close(s);
#endif

extern void socket_init();
extern void socket_destroy();
extern int get_errno();

#endif