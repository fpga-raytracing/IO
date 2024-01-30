//
// Shared IO functions between host and HPS.
// Anything here must work on both Windows and Linux.
//

#ifndef IO_H
#define IO_H

#ifdef __cplusplus
extern "C" {
#endif

#define NET_MAX_STRING 40 // max input string, for security

// Write .bmp. Faster to write but produces larger files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_bmp(const char* filename, const void* data, int width, int height, int channels);

// Write .png. Slower to write but produces smaller files.
// Channels = 3 for RGB, 4 for RGBA.
bool write_png(const char* filename, const void* data, int width, int height, int channels);


// client: initializes data transfer, without built-in error recovery
// supports only binary data (does not consider endianness) of size in range (0B, 2GiB)
// returns send data size (-1 for failure)
int TCP_send(const unsigned char* data, unsigned total_size, const char* addr, const char* port);

// server: waits and accepts data transfer, with built-in error recovery
// ipv6 enables ipv6 support. In some OS including Windows, this disables ipv4
// returns recv data size (-1 for failure); data ptr (malloc)
int TCP_recv(unsigned char** data_ptr, const char* port, bool ipv6);


// TCP_send() example
/*
// exe ip_addr port filename
// example: client.exe ::1 50000 test.txt
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "invalid arguments!\n");
        return -1;
    }

    // read from file
    FILE* fd;
    fd = fopen(argv[3], "rb");
    if (!fd) {
        fprintf(stderr, "file not accessible!\n");
        return -1;
    }
    fseek(fd, 0, SEEK_END);
    unsigned total_size = ftell(fd);
    fseek(fd, 0, SEEK_SET);
	byte* data = (byte*)malloc(total_size);
    fread((void*) data, 1, total_size, fd);
    fclose(fd);

    printf("read filename: %s\n", argv[3]);
    printf("read size: %d\n", total_size);

    int result;
    for (int i = 0; i < 3; i++) {
        printf("\n");
        result = TCP_send((const byte*)data, total_size, argv[1], argv[2]);
        printf("\n");
        if (result != -1) break;
        fprintf(stderr, "send function failed!\n");

        // wait and retry
        #ifdef _WIN32
            Sleep(2000);
        #else
            sleep(2);
        #endif
    }

    // cleanup
    free(data);
    if (result == -1) return -1;

    printf("send size: %d\n", result);
    return 0;
}
*/


// TCP_recv() example
/*
// exe ipver port filename
// example: server.exe 6 50000 test.txt
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "invalid arguments!\n");
        exit(1);
    }
    bool ipv6;
    if (atoi(argv[1]) == 6) ipv6 = true;
    else if (atoi(argv[1]) == 4) ipv6 = false;
    else {
        fprintf(stderr, "invalid arguments!\n");
        exit(1);
    }

    byte* data;
    printf("\n");
    int size = TCP_recv(&data, argv[2], ipv6);
    printf("\n");
    if (size == -1) {
        fprintf(stderr, "receive function failed!\n");
        exit(1);
    }
    printf("receive size: %d\n", size);

    // write to file
    FILE* fd;
    fd = fopen(argv[3], "wb");
    if (!fd) {
        fprintf(stderr, "file not accessible!\n");
        free(data);
        exit(1);
    }
    fwrite((void*) data, 1, size, fd);

    // cleanup
    fclose(fd);
    free(data);
}
*/


#ifdef __cplusplus
}
#endif

#endif