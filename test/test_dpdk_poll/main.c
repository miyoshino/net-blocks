#include "nb_runtime.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define CLIENT_MSG ("Hello from client")

int main(int argc, char* argv[]){
    int packet_size = 0;
    nb__dpdk_init(argc, argv);
    printf("DPDK initialized\n");

    int received = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while(received < 3){
        // check time out 
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - start.tv_sec)
            + (double)(now.tv_nsec - start.tv_nsec) / 1000000000.0;
        if (elapsed >= 5.0) {
            fprintf(stderr, "Timed out: received %d/3 packets\n", received);
            break;
        }

        const int headroom = 32;
        char* buf = nb__poll_packet(&packet_size, headroom);
        
        if (buf != NULL){
            const unsigned char* data = (const unsigned char*)(buf + headroom);
            printf("Packet %d: %d bytes\n", received + 1, packet_size);
            for (int i = 0; i < packet_size; i++){
                printf("%02X ",(unsigned int)data[i]);
            }
            printf("\n");
            free(buf);
            received++;
        }
    }
    
    nb__dpdk_deinit();
    return 0;
}