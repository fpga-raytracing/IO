//
// Shared IO functions between host and HPS.
// Anything here must work on both Windows and Linux.
//

#ifndef IO_H
#define IO_H

#ifdef __cplusplus
extern "C" {
#endif

#define NET_MAX_STRING 64

// Write .bmp. Faster to write but produces larger files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_bmp(const char* filename, const void* data, int width, int height, int channels);

// Write .png. Slower to write but produces smaller files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_png(const char* filename, const void* data, int width, int height, int channels);


// client: initializes data transfer, without built-in error recovery
// str param len < NET_MAX_STRING
// supports only binary data (does not consider endianness)
// returns send data size (-1 for failure)
int TCP_send(const unsigned char* data, unsigned total_size, const char* name, const char* addr, const char* port);

// server: waits and accepts data transfer, with built-in error recovery
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
// returns recv data size (-1 for failure); data ptr (malloc); name ptr (malloc)
int TCP_recv(unsigned char** data_ptr, char** name_ptr, const char* port, bool ipv6);


// TCP_send() example
/*
// exe ip_addr port file_name
// example: client.exe ::1 50000 test.c
int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("invalid arguments!\n");
        exit(1);
    }

    // read from file
    char* name = argv[3];
    FILE* fd;
    fd = fopen(name, "rb");
    if (!fd) {
        printf("file not accessible!\n");
        exit(1);
    }
    fseek(fd, 0, SEEK_END);
    unsigned total_size = ftell(fd);
    fseek(fd, 0, SEEK_SET);
	byte* data = (byte*)malloc(total_size);
    fread((void*) data, 1, total_size, fd);
    fclose(fd);

    printf("read size: %d\n", total_size);
    printf("read name: %s\n\n", name);
    int result = TCP_send((const byte*)data, total_size, name, argv[1], argv[2]);
    printf("\n");
    if (result == -1) {
        printf("send function failed!\n");
        free(data);
        exit(1);
    }
    printf("send size: %d\n", result);

    // cleanup
    free(data);
}
*/


// TCP_recv() example
/*
// exe ipver port
// example: server.exe 6 50000
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("invalid arguments!\n");
        exit(1);
    }
    bool ipv6;
    if (atoi(argv[1]) == 6) ipv6 = true;
    else if (atoi(argv[1]) == 4) ipv6 = false;
    else {
        printf("invalid arguments!\n");
        exit(1);
    }

    byte* data;
    char* name;
    int size = TCP_recv(&data, &name, argv[2], ipv6);
    printf("\n");
    if (size == -1) {
        printf("receive function failed!\n");
        exit(1);
    }
    printf("receive size: %d\n", size);
    printf("receive name: %s\n", name);

    // write to file
    FILE* fd;
    fd = fopen(name, "wb");
    if (!fd) {
        printf("file not accessible!\n");
        free(data);
        free(name);
        exit(1);
    }
    fwrite((void*) data, 1, size, fd);

    // cleanup
    fclose(fd);
    free(data);
    free(name);
}
*/


#ifdef __cplusplus
}
#endif

#endif