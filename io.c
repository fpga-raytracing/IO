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
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

#define NET_TIMEOUT 5 // in seconds
#define NET_MAX_CTRL 64 // 11 is enough, reserved for other control commands
#define ACK_YES "yes"
#define ACK_NO "no"

#ifdef _WIN32
typedef SOCKET socket_t;
#define INV_SOCKET INVALID_SOCKET
#define CLOSE_SOCKET closesocket

#else
typedef int socket_t;
#define INV_SOCKET (-1)
#define CLOSE_SOCKET close
#endif

/*
    protocol:
    init: total_size
    data: data
    ack: ACK_YES
*/


// TCP send loop
// size should be in range (0B, 2GiB)
// returns total byte send, -1 for error (with socket cleanup)
int send_data(socket_t socket, const char* data, unsigned total_size, const char* log_name) {
    unsigned send_left = total_size;
    while(true) {
        int send_byte = send(socket, data+total_size-send_left, send_left, 0);
        if (send_byte == -1) {
            fprintf(stderr, "ERROR: %s message send failed!\n", log_name);
            CLOSE_SOCKET(socket);
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
        if (recv_byte < 1) {
            fprintf(stderr, "ERROR: %s message receive failed!\n", log_name);
            CLOSE_SOCKET(socket);
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


// client: initializes data transfer, without built-in error recovery
// supports only binary data (does not consider endianness) of size in range (0B, 2GiB)
// returns send data size (-1 for failure)
int TCP_send(const char* data, unsigned total_size, const char* addr, const char* port) {
    // parse
    if (!total_size || (total_size & 0x80000000) || !data || !addr || !port || 
        strlen(addr) >= NET_MAX_STRING || strlen(port) >= NET_MAX_STRING) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return -1;
    }

    struct addrinfo hints, *server_info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // ipv4 & ipv6
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(addr, port, &hints, &server_info) != 0) {
        fprintf(stderr, "ERROR: Invalid addrinfo!\n");
        return -1;
    }

    // connect
    socket_t client_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        client_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (client_socket == INV_SOCKET) continue;
        if (connect(client_socket, p->ai_addr, (socklen_t)p->ai_addrlen) != -1) break;
        CLOSE_SOCKET(client_socket);
    }
    if (p == NULL) {
        fprintf(stderr, "ERROR: Connection failed!\n");
        freeaddrinfo(server_info);
        return -1;
    }
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    if (getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, host, NI_MAXHOST, service,
        NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        printf("MESSAGE: Outgoing connection to addr: %s port: %s.\n", host, service);
    else fprintf(stderr, "WARNING: server getnameinfo failed!\n");
    printf("MESSAGE: Send start.\n");
    freeaddrinfo(server_info);

    // inactivity timer
    struct timeval timer = {NET_TIMEOUT, 0};
    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (char* ) &timer, sizeof(timer)) == -1) {
        fprintf(stderr, "WARNING: Inactivity timer failed!\n");
    }

    // init msg
    char buffer[NET_MAX_CTRL];
    memset(buffer, 0, NET_MAX_CTRL);
    sprintf(buffer, "%u", total_size);
    if (send_data(client_socket, buffer, NET_MAX_CTRL, "Initial") == -1) {
        return -1;
    }
    if (recv_data(client_socket, buffer, NET_MAX_CTRL, "Initial") == -1) {
        return -1;
    }
    if (strncmp(buffer, ACK_YES, NET_MAX_CTRL)) {
        fprintf(stderr, "ERROR: Initial message acknowledge failed!\n");
        CLOSE_SOCKET(client_socket);
        return -1;
    }
    
    // data msg
    if (send_data(client_socket, data, total_size, "Data") == -1) {
        return -1;
    }
    if (recv_data(client_socket, buffer, NET_MAX_CTRL, "Data") == -1) {
        return -1;
    }
    if (strncmp(buffer, ACK_YES, NET_MAX_CTRL)) {
        fprintf(stderr, "ERROR: Data message acknowledge failed!\n");
        CLOSE_SOCKET(client_socket);
        return -1;
    }

    // close
    printf("MESSAGE: Send succeeded.\n");
    CLOSE_SOCKET(client_socket);
    printf("MESSAGE: Connection closed.\n");

    return total_size;
}


// server: waits and accepts data transfer, with built-in error recovery
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
// returns recv data size (-1 for failure); data ptr (malloc)
int TCP_recv(char** data_ptr, const char* port, bool ipv6) {
    // parse
    if (!data_ptr || strlen(port) >= NET_MAX_STRING) {
        fprintf(stderr, "ERROR: Invalid input!\n");
        return -1;
    }
    *data_ptr = NULL;

    struct addrinfo hints, *server_info, *p;
    memset(&hints, 0, sizeof(hints));
    if (ipv6) hints.ai_family = AF_INET6;
    else hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &server_info) != 0) {
        fprintf(stderr, "ERROR: Invalid addrinfo!\n");
        return -1;
    }

    // bind & listen
    socket_t server_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_socket == INV_SOCKET) continue;
        if (bind(server_socket, p->ai_addr, (socklen_t)p->ai_addrlen) != -1) break;
        CLOSE_SOCKET(server_socket);
    }
    if (p == NULL) {
        fprintf(stderr, "ERROR: Bind failed!\n");
        freeaddrinfo(server_info);
        return -1;
    }
    if (listen(server_socket, 4) == -1) {
        fprintf(stderr, "ERROR: Listen failed!\n");
        CLOSE_SOCKET(server_socket);
        freeaddrinfo(server_info);
        return -1;
    }
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    if (getnameinfo(p->ai_addr, (socklen_t)p->ai_addrlen, host, NI_MAXHOST, service,
        NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        printf("MESSAGE: Start to listen on addr: %s port: %s.\n", host, service);
    else fprintf(stderr, "WARNING: Server getnameinfo failed!\n");
    freeaddrinfo(server_info);

    // TCP connection
    char* data = NULL;
    struct timeval timer = {NET_TIMEOUT, 0};
    while(true) {
        if (data) free(data);
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        socket_t client_socket = accept(server_socket, (struct sockaddr*) &client_addr, &client_len);

        if (client_socket == INV_SOCKET)
        {
            fprintf(stderr, "ERROR: Accept failed!\n");
            continue;
        }
        if (getnameinfo((struct sockaddr*)&client_addr, sizeof(struct sockaddr_storage), host, 
            NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            printf("MESSAGE: Incoming connection from addr: %s port: %s.\n", host, service);
        else fprintf(stderr, "WARNING: Client getnameinfo failed!\n");
        printf("MESSAGE: Receive start.\n");

        // inactivity timer
        if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char* ) &timer, sizeof(timer)) == -1) {
            fprintf(stderr, "WARNING: Inactivity timer failed!\n");
        }

        // init msg
        char buffer[NET_MAX_CTRL];
        memset(buffer, 0, NET_MAX_CTRL);
        if (recv_data(client_socket, buffer, NET_MAX_CTRL, "Initial") == -1) continue;   
        unsigned total_size = atoi(buffer);
        if (!total_size) {
            fprintf(stderr, "ERROR: Initial message parse failed!\n");
            CLOSE_SOCKET(client_socket);
            continue;
        }
        data = (char*)malloc(total_size);

        strcpy(buffer, ACK_YES);
        if (send_data(client_socket, buffer, NET_MAX_CTRL, "Initial") == -1) continue;

        // data msg
        if (recv_data(client_socket, data, total_size, "Data") == -1) continue;
        if (send_data(client_socket, buffer, NET_MAX_CTRL, "Data") == -1) continue;

        // close
        char temp[1];
        if (recv(client_socket, temp, 1, 0) == 0) printf("MESSAGE: Receive succeeded.\n");
        else fprintf(stderr, "ERROR: Unknown!\n");

        CLOSE_SOCKET(server_socket);
        printf("MESSAGE: Connection closed.\n");

        *data_ptr = data;
        return total_size;
    }
}
