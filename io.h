//
// Shared IO functions between host and HPS.
// Anything here must work on both Windows and Linux.
//

#ifndef IO_H
#define IO_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TCP_STRING 64

// Write .bmp. Faster to write but produces larger files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_bmp(const char* filename, const void* data, int width, int height, int channels);

// Write .png. Slower to write but produces smaller files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_png(const char* filename, const void* data, int width, int height, int channels);


// client: initialize transfer, without built-in error recovery
// return result: 0 for success, -1 for fail
// str param len < MAX_TCP_STRING, supports only binary data (does not consider endianness)
int TCP_send(const unsigned char* data, unsigned total_size, char* name, char* addr, char* port);

// server: waits and accepts transfer, with built-in error recovery
// return: data size, -1 for failure; data_ptr (malloc); name_ptr (malloc)
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
int TCP_recv(unsigned char** data_ptr, char** name_ptr, char* port, bool ipv6);


// TCP_recv() example
// exe ipver port
/*
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
    if (size == -1) {
        printf("recv function failed!\n");
        exit(0);
    }
    printf("\nrecv size: %d\n", size);
    printf("recv name: %s\n", name);

    // write to file
    FILE* fd;
    fd = fopen(name, "wb");
    if (!fd) {
        printf("file not accessible!\n");
        exit(1);
    }
    fwrite((void*) data, 1, size, fd);

    // clean up
    fclose(fd);
    free(data);
    free(name);
}
*/


// TCP_send() example
// exe ip_addr port file_name
/*
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
    uint total_size = ftell(fd);
    fseek(fd, 0, SEEK_SET);
    byte* data = (byte*)malloc(total_size);
    fread((void*) data, 1, total_size, fd);
    fclose(fd);

    printf("send data of size %d\n", total_size);
    int result = TCP_send((const byte*)data, total_size, name, argv[1], argv[2]);
    printf("\nreturn value is %d\n", result);

    // clean up
    free(data);
}
*/

#ifdef __cplusplus
}
#endif

#endif