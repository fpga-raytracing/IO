
// better here than in io.h to avoid 
// messing with cpp files' WINNT.
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

typedef unsigned int uint;
typedef unsigned char byte;


bool write_bmp(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_bmp(filename, width, height, channels, data);
}

bool write_png(const char* filename, const void* data, int width, int height, int channels) {
    return stbi_write_png(filename, width, height, channels, data, 0);
}


// TO DO:
// add dual-stack support
// add timer
// use stream instead of packet
// add reply message

/*
    pkt format:
    init: name, total_size
    data: name, this_size, data
    ASSUME: all pkt are valid
*/


#define MAX_DATA 1200

// client: initialize transfer, without built-in error recovery
// return result: 0 for success, -1 for fail
// str param len < MAX_TCP_STRING, supports only binary data (does not consider endianness)
int TCP_send(const byte* data, unsigned total_size, char* name, char* addr, char* port) {
    // parse
    if (!data || !name || !addr || !port || 
        strlen(name) >= MAX_TCP_STRING || strlen(addr) >= MAX_TCP_STRING || strlen(port) >= MAX_TCP_STRING) {
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

    byte buffer[MAX_DATA];
    byte* buffer_ptr;
    int init = 1; // 0: wait for data transfer, 1: wait for init message
    uint send_size = 0; // size of all send data

    // TCP packet
    //int cnt = 0; // test
    while(true){
        buffer_ptr = buffer;
        if (init) {
            // send name, send total_size
            buffer_ptr += strlen(strcpy(buffer_ptr, name))+1;
            sprintf(buffer_ptr, "%u", total_size);
            init = 0;
            //printf("\ndata packed of size: 0 name: %s\n", buffer); // test
        } else {
            // send name, send this_size, copy data, set send_size
            buffer_ptr += strlen(strcpy(buffer_ptr, name))+1;

            // send_size string is 11B max
            uint max_length = buffer + MAX_DATA - buffer_ptr - 11;
            uint left_length = total_size - send_size;
            uint this_size = MIN(max_length, left_length);
            buffer_ptr += sprintf(buffer_ptr, "%u", this_size)+1;
            memcpy(buffer_ptr, data+send_size, this_size);
            send_size += this_size;
            //printf("\ndata packed of size: %d name: %s\n", this_size, buffer); // test
        }

        // send
        //cnt++; // test
        //printf("send no. %d\n", cnt); // test
        int send_left = MAX_DATA;
        while(true) {
            int send_byte = send(client_socket, buffer+MAX_DATA-send_left, send_left, 0);
            //printf("send() of size: %d\n", send_byte); // test
            if (send_byte == -1) {
                printf("ERROR: Send failed!\n");

                #ifdef _WIN32
                closesocket(client_socket);
                WSACleanup();
                #else
                close(client_socket);
                #endif

                return -1;
            }
            if (send_size == total_size) {
                //printf("\n"); // test
                printf("MESSAGE: Connection closed.\n");
                printf("MESSAGE: Send succeeded.\n");

                #ifdef _WIN32
                closesocket(client_socket);
                WSACleanup();
                #else
                close(client_socket);
                #endif

                return 0;
            }
            send_left -= send_byte;
            if (!send_left) break;
        }
    }
}


// server: waits and accepts transfer, with built-in error recovery
// return: data size, -1 for failure; data_ptr (malloc); name_ptr (malloc)
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
int TCP_recv(unsigned char** data_ptr, char** name_ptr, char* port, bool ipv6) {
    // parse
    if (!data_ptr || !name_ptr || strlen(port) >= MAX_TCP_STRING) {
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
    while(true) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*) &client_addr, &client_len);

        #ifdef _WIN32
        if (client_socket == INVALID_SOCKET) {
        #else
        if (client_socket == -1) {
        #endif

            printf("ERROR: Accept failed!\n");
            continue;
        }
        if (getnameinfo((struct sockaddr*)&client_addr, sizeof(struct sockaddr_storage), host, NI_MAXHOST, service,
            NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            printf("MESSAGE: Incoming connection from addr: %s port: %s.\n", host, service);
        else printf("WARNING: Client getnameinfo failed!\n");
        printf("MESSAGE: Receive start.\n");

        int init = 1; // 0: wait for data transfer, 1: wait for init message
        uint total_size; // size of all required data
        uint recv_size = 0; // size of all received data
        char* name = NULL;
        byte* data = NULL;

        // TCP packet
        //int cnt = 0; // test
        while(true) {
            byte buffer[MAX_DATA];
            byte* buffer_ptr;
            buffer_ptr = buffer;

            // receive
            //cnt++; // test
            //printf("\nrecv no. %d\n", cnt); // test
            int recv_left = MAX_DATA;
            bool break_out = false;
            while(true) {
                int recv_byte = recv(client_socket, buffer+MAX_DATA-recv_left, recv_left, 0);
                //printf("recv() of size: %d\n", recv_byte); // test
                if (recv_byte < 1) {
                    if(recv_byte == 0) printf("MESSAGE: Connection closed.\n");
                    else printf("ERROR: Connection lost!\n");
                    break_out = true;
                    break;
                }
                recv_left -= recv_byte;
                if (!recv_left) break;
            }
            if(break_out) break;

            if (init) {
                // set name, set total_size, construct data
                int name_len = strlen(buffer_ptr)+1;
                name = (char*)malloc(name_len);
                strcpy(name, buffer_ptr);
                buffer_ptr += name_len;

                total_size = atoi(buffer_ptr);
                data = (byte*)malloc(total_size);
                init = 0;
                //printf("data unpacked of size: 0 name: %s\n", buffer); // test
            } else {
                // check name, set recv_size, copy data
                if (strcmp(name, buffer_ptr)) {
                    printf("ERROR: Name mismatch!\n");
                    break;
                }
                buffer_ptr += strlen(name)+1;
                uint this_size = atoi(buffer_ptr);
                buffer_ptr += strlen(buffer_ptr)+1;
                memcpy(data+recv_size, buffer_ptr, this_size);
                recv_size += this_size;
                //printf("data unpacked of size: %d name: %s\n", this_size, buffer); // test
            }
        }
        // clean up

        #ifdef _WIN32
        closesocket(client_socket);
        #else
        close(client_socket);
        #endif

        if (recv_size != total_size) {
            printf("ERROR: Receive failed!\n");
            if (data) free(data);
            if (name) free(name);
        } else {
            printf("MESSAGE: Receive succeeded.\n");

            #ifdef _WIN32
            closesocket(server_socket);
            WSACleanup();
            #else
            close(server_socket);
            #endif

            *data_ptr = data;
            *name_ptr = name;
            return total_size;
        }
    }
}


/*
typedef struct BITMAPFILE { // in little-endian
    // bmp file header
    ushort  bfType;          // type: "BM" (0-1)
    uint    bfSize;          // bmp file size (2-5)
    ushort  bfReserved1;     // reserved, 0 (6-7)
    ushort  bfReserved2;     // reserved, 0 (8-9)
    uint    bfOffBits;       // pixel bits offset, usually 54 (10-13)

    // bmp info header
    uint    biSize;          // bmp info header size, 40 (14-17)
    uint    biWidth;         // image width  (18-21)
    uint    biHeight;        // image height  (22-25)
    ushort  biPlanes;        // target color plane, usually 1 (26-27)
    ushort  biBitCount;      // color depth of all channels (28-29)
    uint    biCompression;   // compression, usually 0 (30-33)
    uint    biSizeImage;     // image size including padding, uncompressed could use 0 (34-37)
    uint    biXPelsPerMeter; // horizontal pixel density, usually 0 (38-41)
    uint    biYPelsPerMeter; // vertical pixel density, usually 0 (42-45)
    uint    biClrUsed;       // color used, usually 0 (46-49)
    uint    biClrImportant;  // important color, usually 0 (50-53)

    // color palette (unused if biBitCount > 8, which is 256 color)
    //byte rgbBlue;
    //byte rgbGreen;
    //byte rgbRed;
    //byte rgbReserved;
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
