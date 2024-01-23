//
// Shared IO functions between host and HPS.
// Anything here must work on both Windows and Linux.
//

// def here to avoid messing with cpp files' WINNT.
#ifdef _WIN32
    // for ws2tcpip.h
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x501
    #endif
#endif

#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_SECURE_NO_DEPRECATE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // does not work in gcc, insert to the end of gcc command: -lws2_32
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netdb.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include "stb_image_write.h"

#include "io.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define NET_TIMEOUT 5 // in seconds
#define NET_MAX_CTRL NET_MAX_STRING+11
#define ACK_YES "yes"
#define ACK_NO "no"

typedef unsigned short ushort;
typedef unsigned char byte;


bool write_bmp(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_bmp(filename, width, height, channels, data);
}

bool write_png(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_png(filename, width, height, channels, data, 0);
}


/*
    protocol:
    init: name, total_size
    data: data
    ack: ACK_YES
*/


// TCP send loop
// returns total byte send, -1 for error (with socket cleanup)
int send_data(int socket, const byte* data, int total_size, char* log_name) {
    unsigned send_left = total_size;
    while(true) {
        int send_byte = send(socket, data+total_size-send_left, send_left, 0);
        if (send_byte == -1) {
            printf("ERROR: %s message send failed!\n", log_name);
            #ifdef _WIN32
                closesocket(socket);
            #else
                close(socket);
            #endif
            return -1;
        }
        send_left -= send_byte;
        //printf("send %s msg of size: %d, left: %d\n", log_name, send_byte, send_left); // test
        if (!send_left) return total_size;
    }
}


// TCP recv loop
// returns total byte recv, -1 for error (with socket cleanup); data (pre allocated)
int recv_data(int socket, byte* data, int total_size, char* log_name) {
    unsigned recv_left = total_size;
    while(true) {
        int recv_byte = recv(socket, data+total_size-recv_left, recv_left, 0);
        if (recv_byte < 1) {
            printf("ERROR: %s message receive failed!\n", log_name);
            #ifdef _WIN32
                closesocket(socket);
            #else
                close(socket);
            #endif
            return -1;
        }
        recv_left -= recv_byte;
        //printf("send %s msg of size: %d, left: %d\n", log_name, recv_byte, recv_left); // test
        if (!recv_left) return total_size;
    }
}


// client: initializes data transfer, without built-in error recovery
// str param len < NET_MAX_STRING
// supports only binary data (does not consider endianness)
// returns send data size (-1 for failure)
int TCP_send(const byte* data, unsigned total_size, const char* name, const char* addr, const char* port) {
    // parse
    if (!total_size || !data || !name || !addr || !port || 
        strlen(name) >= NET_MAX_STRING || strlen(addr) >= NET_MAX_STRING || strlen(port) >= NET_MAX_STRING) {
        printf("ERROR: Invalid input!\n");
        return -1;
    }

    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("ERROR: WSAStartup failed!\n");
            return -1;
        }
    #endif

    struct addrinfo hints, *server_info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // ipv4 & ipv6
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(addr, port, &hints, &server_info) != 0) {
        printf("ERROR: Invalid addrinfo!\n");
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }

    // connect
    int client_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        client_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        #ifdef _WIN32
            if (client_socket == INVALID_SOCKET) continue;
        #else
            if (client_socket == -1) continue;
        #endif
        if (connect(client_socket, p->ai_addr, p->ai_addrlen) != -1) break;
        #ifdef _WIN32
            closesocket(client_socket);
        #else
            close(client_socket);
        #endif
        break;
    }
    if (p == NULL) {
        printf("ERROR: Connection failed!\n");
        freeaddrinfo(server_info);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    if (getnameinfo(p->ai_addr, p->ai_addrlen, host, NI_MAXHOST, service,
        NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        printf("MESSAGE: Outgoing connection to addr: %s port: %s.\n", host, service);
    else printf("WARNING: server getnameinfo failed!\n");
    printf("MESSAGE: Send start.\n");
    freeaddrinfo(server_info);

    // inactivity timer
    struct timeval timer = {NET_TIMEOUT, 0};
    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (char* ) &timer, sizeof(timer)) == -1) {
        printf("WARNING: Inactivity timer failed!\n");
    }

    // init msg
    char buffer[NET_MAX_CTRL];
    memset(buffer, 0, NET_MAX_CTRL);
    sprintf(strlen(strcpy(buffer, name))+1+buffer, "%u", total_size);
    if (send_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Initialize") == -1) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    if (recv_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Initialize") == -1) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    if (strcmp(buffer, ACK_YES)) {
        printf("ERROR: Initialize message acknowledge failed!\n");
        #ifdef _WIN32
            closesocket(client_socket);
            WSACleanup();
        #else
            close(client_socket);
        #endif
        return -1;
    }
    
    // data msg
    if (send_data(client_socket, data, total_size, "Data") == -1) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    if (recv_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Data") == -1) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    if (strncmp(buffer, ACK_YES, NET_MAX_CTRL)) {
        printf("ERROR: Data message acknowledge failed!\n");
        #ifdef _WIN32
            closesocket(client_socket);
            WSACleanup();
        #else
            close(client_socket);
        #endif
        return -1;
    }

    // close
    printf("MESSAGE: Send succeeded.\n");
    #ifdef _WIN32
        closesocket(client_socket);
        WSACleanup();
    #else
        close(client_socket);
    #endif
    printf("MESSAGE: Connection closed.\n");

    return total_size;
}


// server: waits and accepts data transfer, with built-in error recovery
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
// returns recv data size (-1 for failure); data ptr (malloc); name ptr (malloc)
int TCP_recv(byte** data_ptr, char** name_ptr, const char* port, bool ipv6) {
    // parse
    if (!data_ptr || !name_ptr || strlen(port) >= NET_MAX_STRING) {
        printf("ERROR: Invalid input!\n");
        return -1;
    }
    *data_ptr = NULL;
    *name_ptr = NULL;

    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("ERROR: WSAStartup failed!\n");
            return -1;
        }
    #endif

    struct addrinfo hints, *server_info, *p;
    memset(&hints, 0, sizeof(hints));
    if (ipv6) hints.ai_family = AF_INET6;
    else hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &server_info) != 0) {
        printf("ERROR: Invalid addrinfo!\n");
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }

    // bind & listen
    int server_socket;
    for (p = server_info; p != NULL; p = p->ai_next) {
        server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        #ifdef _WIN32
            if (server_socket == INVALID_SOCKET) continue;
        #else
            if (server_socket == -1) continue;
        #endif
        if (bind(server_socket, p->ai_addr, p->ai_addrlen) != -1) break;
        #ifdef _WIN32
            closesocket(server_socket);
        #else
            close(server_socket);
        #endif
        break;
    }
    if (p == NULL) {
        printf("ERROR: Bind failed!\n");
        freeaddrinfo(server_info);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    if (listen(server_socket, 4) == -1) {
        printf("ERROR: Listen failed!\n");
        #ifdef _WIN32
            closesocket(server_socket);
        #else
            close(server_socket);
        #endif
        freeaddrinfo(server_info);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return -1;
    }
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];
    if (getnameinfo(p->ai_addr, p->ai_addrlen, host, NI_MAXHOST, service,
        NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        printf("MESSAGE: Start to listen on addr: %s port: %s.\n", host, service);
    else printf("WARNING: Server getnameinfo failed!\n");
    freeaddrinfo(server_info);

    // TCP connection
    char* name = NULL;
    byte* data = NULL;
    struct timeval timer = {NET_TIMEOUT, 0};
    while(true) {
        if (name) free(name);
        if (data) free(data);
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*) &client_addr, &client_len);

        #ifdef _WIN32
            if (client_socket == INVALID_SOCKET)
        #else
            if (client_socket == -1)
        #endif
        {
            printf("ERROR: Accept failed!\n");
            continue;
        }
        if (getnameinfo((struct sockaddr*)&client_addr, sizeof(struct sockaddr_storage), host, 
            NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            printf("MESSAGE: Incoming connection from addr: %s port: %s.\n", host, service);
        else printf("WARNING: Client getnameinfo failed!\n");
        printf("MESSAGE: Receive start.\n");

        // inactivity timer
        if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char* ) &timer, sizeof(timer)) == -1) {
            printf("WARNING: Inactivity timer failed!\n");
        }

        // init msg
        char buffer[NET_MAX_CTRL];
        memset(buffer, 0, NET_MAX_CTRL);
        if (recv_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Initialize") == -1) continue;
        int name_len = strlen(buffer)+1;
        name = (char*)malloc(name_len);
        strcpy(name, buffer);
        
        unsigned total_size = atoi(buffer+name_len);
        data = (byte*)malloc(total_size);

        strcpy(buffer, ACK_YES);
        if (send_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Initialize") == -1) continue;

        // data msg
        if (recv_data(client_socket, data, total_size, "Data") == -1) continue;
        if (send_data(client_socket, (byte*)buffer, NET_MAX_CTRL, "Data") == -1) continue;

        // close
        byte temp[1];
        if (recv(client_socket, temp, 1, 0) == 0) printf("MESSAGE: Receive succeeded.\n");
        else printf("ERROR: Unknown!\n");

        #ifdef _WIN32
            closesocket(server_socket);
            WSACleanup();
        #else
            close(server_socket);
        #endif
        printf("MESSAGE: Connection closed.\n");

        *data_ptr = data;
        *name_ptr = name;
        return total_size;
    }
}


/*
typedef struct BITMAPFILE { // in little-endian
    // bmp file header
    ushort      bfType;          // type: "BM" (0-1)
    unsigned    bfSize;          // bmp file size (2-5)
    ushort      bfReserved1;     // reserved, 0 (6-7)
    ushort      bfReserved2;     // reserved, 0 (8-9)
    unsigned    bfOffBits;       // pixel bits offset, usually 54 (10-13)

    // bmp info header
    unsigned    biSize;          // bmp info header size, 40 (14-17)
    unsigned    biWidth;         // image width  (18-21)
    unsigned    biHeight;        // image height  (22-25)
    ushort      biPlanes;        // target color plane, usually 1 (26-27)
    ushort      biBitCount;      // color depth of all channels (28-29)
    unsigned    biCompression;   // compression, usually 0 (30-33)
    unsigned    biSizeImage;     // image size including padding, uncompressed could use 0 (34-37)
    unsigned    biXPelsPerMeter; // horizontal pixel density, usually 0 (38-41)
    unsigned    biYPelsPerMeter; // vertical pixel density, usually 0 (42-45)
    unsigned    biClrUsed;       // color used, usually 0 (46-49)
    unsigned    biClrImportant;  // important color, usually 0 (50-53)

    // color palette (unused if biBitCount > 8, which is 256 color)
    //byte        rgbBlue;
    //byte        rgbGreen;
    //byte        rgbRed;
    //byte        rgbReserved;
} bmp_header;
*/
// if board does not support stbi, we can try to use this
bool write_bmp_old(const byte* pixelBuf, int width, int height, const char* filename) {
    // assume pixelBuf is top left and uses true color (24b)

    int rowSize = (width * 3 + 3) & ~3;
    int bfSize = 54 + rowSize * height;

    byte header[54] = {
        // bmp file header
        'B', 'M',    // bfType
        bfSize, bfSize >> 8, bfSize >> 16, bfSize >> 24, // bfSize
        0, 0,        // bfReserved1
        0, 0,        // bfReserved2
        54, 0, 0, 0, // bfOffBits

        // bmp info header
        40, 0, 0, 0, // biSize
        width, width >> 8, width >> 16, width >> 24, // biWidth
        height, height >> 8, height >> 16, height >> 24, // biHeight
        1, 0,        // biPlanes
        24, 0,       // biBitCount
        0, 0, 0, 0,  // biCompression
        0, 0, 0, 0,  // biSizeImage
        0, 0, 0, 0,  // biXPelsPerMeter
        0, 0, 0, 0,  // biYPelsPerMeter
        0, 0, 0, 0,  // biClrUsed
        0, 0, 0, 0   // biClrImportant
    };

    FILE* fd;
    fd = fopen(filename, "wb");
    if (!fd) return false;

    fwrite(header, 1, 54, fd);
    for (int h = height - 1; h >= 0; h--) {
        fwrite(pixelBuf + h * width * 3, 1, width * 3, fd);
        fwrite((char[]) { 0, 0, 0 }, 1, rowSize - width * 3, fd); // Padding
    }

    fclose(fd);
    return true;
}
