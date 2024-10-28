#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define REQUEST_INTERVAL 0.001  // Set to 1 millisecond for higher request frequency

// Shared data structure for request statistics
typedef struct {
    atomic_long total_requests;
    long last_total_requests;  // Track last total for RPS calculation
    struct timespec last_time; // Track last time for RPS calculation
    pthread_mutex_t print_mutex; // Mutex for printing stats
} benchmark_stats;

// Function to send HTTP requests
void *send_requests(void *arg) {
    char *server_host = ((char **)arg)[0];
    int server_port = atoi(((char **)arg)[1]);
    benchmark_stats *stats = (benchmark_stats *)((char **)arg)[2];
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), 
             "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", server_host);

    while (1) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            usleep((useconds_t)(REQUEST_INTERVAL * 1e6)); // Sleep before retry
            continue;
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_host, &server_addr.sin_addr);

        // Connect to the server
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            usleep((useconds_t)(REQUEST_INTERVAL * 1e6)); // Sleep before retry
            continue; // Retry sending request
        }

        // Send the request
        send(sockfd, request, strlen(request), 0);
        
        // Increment total requests atomically
        atomic_fetch_add(&stats->total_requests, 1);

        // Receive the response (optional)
        char buffer[BUFFER_SIZE];
        recv(sockfd, buffer, sizeof(buffer) - 1, 0);

        close(sockfd);

        // Sleep for request interval (simulated pacing)
        usleep((useconds_t)(REQUEST_INTERVAL * 1e6));
    }
}

// Function to print RPS periodically
void *print_stats(void *arg) {
    benchmark_stats *stats = (benchmark_stats *)arg;

    // Initialize last time
    clock_gettime(CLOCK_MONOTONIC, &stats->last_time);
    stats->last_total_requests = 0;

    while (1) {
        usleep(2000000); // Print every 2 seconds
        
        long current_total_requests = atomic_load(&stats->total_requests);
        
        // Calculate time elapsed
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_time = (now.tv_sec - stats->last_time.tv_sec) + 
                              (now.tv_nsec - stats->last_time.tv_nsec) / 1e9;

        // Calculate requests since last print
        long requests_since_last_print = current_total_requests - stats->last_total_requests;

        // Only calculate RPS if elapsed time is greater than 0
        double rps = (elapsed_time > 0) ? (requests_since_last_print / elapsed_time) : 0;

        // Lock the mutex for printing
        pthread_mutex_lock(&stats->print_mutex);
        // Print stats
        printf("Total Requests: %ld, Requests Since Last Print: %ld, RPS: %.2f\n", 
               current_total_requests, requests_since_last_print, rps);
        pthread_mutex_unlock(&stats->print_mutex);

        // Update last total and time
        stats->last_total_requests = current_total_requests;
        stats->last_time = now;
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_host> <server_port> <num_threads>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_host = argv[1];
    int server_port = atoi(argv[2]);
    int num_threads = atoi(argv[3]);

    benchmark_stats stats;
    atomic_init(&stats.total_requests, 0);
    stats.last_total_requests = 0;
    pthread_mutex_init(&stats.print_mutex, NULL); // Initialize the mutex

    // Create threads for sending requests
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        char *args[3] = {server_host, argv[2], (char *)&stats};
        pthread_create(&threads[i], NULL, send_requests, args);
    }

    // Create a thread for printing stats
    pthread_t stats_thread;
    pthread_create(&stats_thread, NULL, print_stats, &stats);

    // Join all threads (they run indefinitely in this example)
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(stats_thread, NULL);

    // Clean up
    pthread_mutex_destroy(&stats.print_mutex); // Destroy the mutex
    return 0;
}

