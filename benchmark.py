#!/usr/bin/env python3

import socket
import time
import sys
import threading
from multiprocessing import Value
from ctypes import c_long

# Function to send requests in a loop in each thread
def send_requests(server_host, server_port, request_interval, total_requests):
    request = "GET / HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n".format(server_host).encode()

    while True:
        # Create a new socket for each request
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            try:
                # Connect to server
                s.connect((server_host, server_port))
                # Send the request
                s.sendall(request)
                # Receive response
                response = s.recv(1024)
                
                # Increment request count atomically if response is received
                if response:
                    total_requests.value += 1

            except socket.error as e:
                print(f"Socket error: {e}")

        # Optional sleep interval between requests to control load
        time.sleep(request_interval)

# Function to print stats periodically
def print_stats(total_requests, print_interval):
    start_time = time.time()
    while True:
        time.sleep(print_interval)
        elapsed_time = time.time() - start_time
        # Atomic read of the request count
        total_count = total_requests.value
        avg_rps = total_count / elapsed_time
        print(f"Total Requests: {total_count}, Time Elapsed: {elapsed_time:.2f}s, Avg Requests/sec: {avg_rps:.2f}")

def benchmark(server_host, server_port, num_threads=4, request_interval=0.01, print_interval=2):
    # Atomic counter for total requests
    total_requests = Value(c_long, 0)

    # Start the stat printing thread
    threading.Thread(target=print_stats, args=(total_requests, print_interval), daemon=True).start()

    # Start multiple threads to send requests
    threads = []
    for _ in range(num_threads):
        t = threading.Thread(target=send_requests, args=(server_host, server_port, request_interval, total_requests))
        t.daemon = True  # Allows program to exit even if threads are running
        t.start()
        threads.append(t)

    # Join all threads to keep main program alive
    for t in threads:
        t.join()

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <server_host> <server_port> <num_threads>")
        sys.exit(1)

    server_host = sys.argv[1]
    server_port = int(sys.argv[2])
    num_threads = int(sys.argv[3])

    benchmark(server_host, server_port, num_threads)
