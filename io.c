//
// Shared IO functions between host and HPS.
// Anything here must work on both Windows and Linux.
//

#if defined(__MINGW32__) && !defined(_WIN32_WINNT)
// mingw bug. for ws2tcpip.h
#define _WIN32_WINNT 0x501
#endif

#define _CRT_SECURE_NO_WARNINGS 1

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // does not work in mingw, add -lws2_32 to linker manually
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include "stb_image_write.h"

#include "io.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

bool write_bmp(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_bmp(filename, width, height, channels, data);
}

bool write_png(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_png(filename, width, height, channels, data, 0);
}

#define NET_TIMEOUT 30
#define NET_MAXINIT 11 // max size of init message

#ifdef _WIN32
#define TCP_ERRNO WSAGetLastError()
#else
#define TCP_ERRNO errno
#endif

/*
    protocol:
    init: total_size
    data: data
    ack: ACK_YES
*/

void TCP_close(socket_t socket)
{
#ifdef _WIN32
    int ret = closesocket(socket);
#else
    int ret = close(socket);
#endif
    if (ret != 0) {
        // cannot retry a close, just log it
        fprintf(stderr, "ERROR: Failed to close socket with err %d\n", TCP_ERRNO);
    }
}

void TCP_wrshutdown(socket_t socket)
{
#ifdef _WIN32
    int ret = shutdown(socket, SD_SEND);
#else
    int ret = shutdown(socket, SHUT_WR);
#endif
    if (ret != 0) {
        // should never happen. not much we can do if it does
        fprintf(stderr, "ERROR: Failed to shut_wr socket with err %d\n", TCP_ERRNO);
        // close the socket, else future TCP calls may result in deadlock
        TCP_close(socket);
    }

}

// TCP send loop
// size should be in range (0B, 2GiB)
// returns total byte send, -1 for error (with socket cleanup)
int send_data(socket_t socket, const char* data, unsigned total_size, const char* log_name) {
    unsigned send_left = total_size;
    while(true) {
        int send_byte = send(socket, data+total_size-send_left, send_left, 0);
        if (send_byte == -1) {
            fprintf(stderr, "ERROR: %s message send failed with err %d\n", log_name, TCP_ERRNO);
            TCP_close(socket);
            return -1;
        }
        send_left -= send_byte;
        //printf("send %s msg of size: %d, left: %u\n", log_name, send_byte, send_left); // test
        if (!send_left) return total_size;
    }
}


// TCP recv loop
// returns total byte recv, -1 for error (with socket cleanup); data (pre allocated)
int recv_data(socket_t socket, char* data, unsigned total_size, const char* log_name) {
    unsigned recv_left = total_size;
    while(true) {
        int recv_byte = recv(socket, data+total_size-recv_left, recv_left, 0);
        if (recv_byte == -1) {
            fprintf(stderr, "ERROR: %s message receive failed with err %d\n", log_name, TCP_ERRNO);
            TCP_close(socket);
            return -1;
        }
        recv_left -= recv_byte;
        //printf("send %s msg of size: %d, left: %u\n", log_name, recv_byte, recv_left); // test
        if (!recv_left) return total_size;
    }
}

#ifdef _WIN32
// void(*)(void) as required by atexit
static void wsa_cleanup(void)
{
    WSACleanup();
}

int TCP_win32_init()
{
    WORD version = MAKEWORD(2, 2);

    WSADATA wsaData;
    int err = WSAStartup(version, &wsaData);
    if (err != 0) {
        fprintf(stderr, "ERROR: WSAStartup failed with code %d", err);
        return err;
    }
    if (wsaData.wVersion != version) {     
        fprintf(stderr, "ERROR: Winsock 2.2 not available");
        WSACleanup();
        return -1;
    }
    if (atexit(wsa_cleanup) != 0) {        
        fprintf(stderr, "ERROR: atexit registration failed");
        WSACleanup();
        return -1;
    }

    return 0;
}
#endif

socket_t TCP_connect2(const char* addr, const char* port, bool verbose)
{
    // parse
    if (!addr || !port || strlen(addr) >= NET_MAX_STRING || strlen(port) >= NET_MAX_STRING) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return INV_SOCKET;
    }

    struct addrinfo hints, * server_info, * p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // ipv4 & ipv6
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(addr, port, &hints, &server_info) != 0) {
        fprintf(stderr, "ERROR: Invalid addrinfo!\n");
        return INV_SOCKET;
    }

    // connect
    socket_t client_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        client_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (client_socket == INV_SOCKET) continue;
        if (connect(client_socket, p->ai_addr, (socklen_t)p->ai_addrlen) != -1) break;
        TCP_close(client_socket);
    }
    if (p == NULL) {
        fprintf(stderr, "ERROR: Connection failed!\n");
        freeaddrinfo(server_info);
        return INV_SOCKET;
    }

    if (verbose) {
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        int err = getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, host, NI_MAXHOST, service,
            NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
        if (err == 0)
            printf("MESSAGE: Established connection to addr: %s port: %s.\n", host, service);
        else fprintf(stderr, "WARNING: server getnameinfo failed with err %d!\n", err);    
    }

    freeaddrinfo(server_info);

    // inactivity timer
    struct timeval timer = { NET_TIMEOUT, 0 };
    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timer, sizeof(timer)) == -1) {
        fprintf(stderr, "WARNING: Connect inactivity timer failed with err %d!\n", TCP_ERRNO);
    }

    return client_socket;
}

int TCP_send2(socket_t socket, const char* data, unsigned total_size, bool verbose) {
    
    if (!total_size || (total_size & 0x80000000) || !data) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return -1;
    }

    if (verbose) 
        printf("MESSAGE: Send start.\n");

    // size of data
    char buffer[NET_MAXINIT];
    memset(buffer, 0, NET_MAXINIT);
    sprintf(buffer, "%u", total_size);
    if (send_data(socket, buffer, NET_MAXINIT, "Initial") == -1) {
        return -1;
    }  
    // data msg
    if (send_data(socket, data, total_size, "Data") == -1) {
        return -1;
    }

    if (verbose) 
        printf("MESSAGE: Sent %d bytes\n", total_size);

    return total_size;
}

socket_t TCP_listen2(const char* port, bool ipv6, bool verbose)
{
    // parse
    if (strlen(port) >= NET_MAX_STRING) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return INV_SOCKET;
    }

    struct addrinfo hints, * server_info, * p;
    memset(&hints, 0, sizeof(hints));
    if (ipv6) hints.ai_family = AF_INET6;
    else hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &server_info) != 0) {
        fprintf(stderr, "ERROR: Invalid addrinfo!\n");
        return INV_SOCKET;
    }

    // bind & listen
    socket_t server_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_socket == INV_SOCKET) continue;
        if (bind(server_socket, p->ai_addr, (socklen_t)p->ai_addrlen) != -1) break;
        TCP_close(server_socket);
    }
    if (p == NULL) {
        fprintf(stderr, "ERROR: Bind failed!\n");
        freeaddrinfo(server_info);
        return INV_SOCKET;
    }
    if (listen(server_socket, 4) == -1) {
        fprintf(stderr, "ERROR: Listen failed!\n");
        TCP_close(server_socket);
        freeaddrinfo(server_info);
        return INV_SOCKET;
    }

    if (verbose) {
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        int err = getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, host, NI_MAXHOST, service,
            NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
        if (err == 0)
            printf("MESSAGE: Start to listen on addr: %s port: %s.\n", host, service);
        else fprintf(stderr, "WARNING: Server getnameinfo failed with err %d!\n", err);
    }
    
    freeaddrinfo(server_info);

    return server_socket;
}

socket_t TCP_accept2(socket_t socket, bool verbose)
{
    struct timeval timer = { NET_TIMEOUT, 0 };

    struct sockaddr_storage client_addr;
    socklen_t client_len = sizeof(client_addr);
    socket_t client_socket = accept(socket, (struct sockaddr*)&client_addr, &client_len);

    if (client_socket == INV_SOCKET)
    {
        fprintf(stderr, "ERROR: Accept failed!\n");
        return INV_SOCKET;
    }
    if (verbose) {
        char host[NI_MAXHOST];
        char service[NI_MAXSERV];
        int err = getnameinfo((struct sockaddr*)&client_addr, sizeof(struct sockaddr_storage), host,
            NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
        if (err == 0)
            printf("MESSAGE: Accepted connection from addr: %s port: %s.\n", host, service);
        else fprintf(stderr, "WARNING: Client getnameinfo failed with err %d!\n", err);
    }

    // inactivity timer
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timer, sizeof(timer)) == -1) {
        fprintf(stderr, "WARNING: Accept inactivity timer failed with err %d!\n", TCP_ERRNO);
    }

    return client_socket;
}

int TCP_recv2(socket_t socket, char** data_ptr, bool verbose) 
{
    if (!data_ptr) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return -1;
    }
    *data_ptr = NULL;
   
    if (verbose) 
        printf("MESSAGE: Receive start.\n");

    // size of data
    char buffer[NET_MAXINIT];
    memset(buffer, 0, NET_MAXINIT);
    if (recv_data(socket, buffer, NET_MAXINIT, "Initial") == -1) return -1;
    unsigned total_size = atoi(buffer);
    if (!total_size) {
        fprintf(stderr, "ERROR: Initial message parse failed!\n");
        TCP_close(socket);
        return -1;
    }

    // data
    char* data = (char*)malloc(total_size);  
    if (recv_data(socket, data, total_size, "Data") == -1) {
        free(data);
        return -1;
    }

    if (verbose) 
        printf("MESSAGE: Received %d bytes\n", total_size);

    *data_ptr = data;
    return total_size;
}

socket_t TCP_connect(const char* addr, const char* port) {
    return TCP_connect2(addr, port, false);
}

socket_t TCP_listen(const char* port, bool ipv6) {
    return TCP_listen2(port, ipv6, false);
}

socket_t TCP_accept(socket_t socket) {
    return TCP_accept2(socket, false);
}

int TCP_send(socket_t socket, const char* data, unsigned total_size) {
    return TCP_send2(socket, data, total_size, false);
}

int TCP_recv(socket_t socket, char** data_ptr) {
    return TCP_recv2(socket, data_ptr, false);
}
