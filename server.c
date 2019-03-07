/*
 * Compile using flags detailed in:
 * https://computing.llnl.gov/tutorials/pthreads/#Compiling
*/

// Misc.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "pthread_pool.h"

// Networking
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Constants
#define LISTEN_PORT "80"	// Change as needed
#define BACKLOG 10			// Change as needed
#define BUFF_LEN 10240 		// Should be ample for any HTTP request
#define MAX_THREADS 10 		// Change according to available stack space
#define STDIN 0				// File descriptor for standard input

/* Close socket
 * sock: Socket file descriptor to close
 * returns: 0 (success) or 1 (fail)
*/
int closeSocket(int sock) {
	if (close(sock) < 0) {
		fprintf(stderr, "Failed to close socket: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

/*
 * Handle client request
 * arg: The socket file descriptor of the connection
*/
void *handleRequest(void *arg) {
	int cfd = *(int *) arg;

	// Recieve data (0 recieved means client has disconnected)
	void *buf = calloc(BUFF_LEN, sizeof(char));
	if (recv(cfd, buf, BUFF_LEN, 0) <= 0) {
		fprintf(stderr, "Failed to recv: %s\n", strerror(errno));
		closeSocket(cfd);
		free(buf);
		return NULL;
	}

	// Get request method
	char *p, *method;
	int length;
	p = strchr(buf, ' ');
	length = (void*)p - buf;
	method = (char*)malloc((length + 1) * sizeof(char));
	strncpy(method, buf, length);
	method[length] = '\0';

	// Only accept implemented methods
	if (!strcmp(method, "GET")) {
		// Get requested URL
		char *b, *e, *url;
		b = strchr(buf, '/');
		e = strchr(b, ' ');
		length = e - b + 1;
		url = (char*)malloc((length + 1) * sizeof(char));
		strncpy(url + 1, b, length);
		url[0] = '.';
		url[length] = '\0';

		// Get requested file
		FILE* fp;
		if (!strcmp(url, "./")) { // Index requested
			fp = fopen("./index.html", "r");
		} else {
			fp = fopen(url, "r");
		}

		// Send data to client
		if (fp == NULL) { // 404 Not Found
			const char *message = "HTTP/1.1 404 Not Found";
			send(cfd, message, strlen(message), 0);
		} else {
			// Get file size
			fseek(fp, 0, SEEK_END);
			long size = ftell(fp);
			rewind(fp);

			// Read file into memory
			void *reply = malloc(size * sizeof(char));
			if (reply != NULL) {
				fread(reply, sizeof(char), size, fp);

				// Send data until we're done
				int total = 0;
				long bytes_left = size;
				int n;
				while(total < size) {
					if ((n = send(cfd, reply + total, bytes_left, 0)) < 0) {
						break;
					}
					total += n;
					bytes_left -= n;
				}
				free(reply);
			}
			fclose(fp);
		}
		
		free(url);
	} else { // 501 Not Implemented
		const char *message = "HTTP/1.1 501 Not Implemented";
		send(cfd, message, strlen(message), 0);
	}

	closeSocket(cfd);
	free(buf);
	free(method);
	return NULL;
}

int main(int argc, char** argv) {
	// Starting variables
	struct addrinfo hints, *res;
	int status, sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // Either AF_INET (IPv4) or AF_INET6 (IPv6)
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE; // Fill in host IP for us

	// Load up address structs
	if ((status = getaddrinfo(NULL, LISTEN_PORT, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return 1;
	}

	// Some addresses returned by getaddrinfo() may not work 
	// Loop through until a working one is found
	struct addrinfo* it;
	for(it = res; it != NULL; it = it->ai_next) {
		// Make a socket
		if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
			fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
			continue;
		}

		// Bind it to the port we passed into getaddrinfo()
		if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
			fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
			continue;
		}

		break; // If we get here, means a working address was found
	}

	// Looped off the end of the list with no connection
	if (it == NULL) {
		fprintf(stderr, "Could not bind socket. Stopping...\n");
		return 1;
	}
	freeaddrinfo(res);

	// Listen
	if (listen(sockfd, BACKLOG) < 0) {
		fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
		return 1;
	}

	// Set up file descriptor sets
	fd_set master, temp;
	FD_ZERO(&master);
	FD_ZERO(&temp);
	FD_SET(STDIN, &master);
	FD_SET(sockfd, &master);

	// Set up thread pool
	struct pool *thread_pool = (struct pool *) pool_start(handleRequest, MAX_THREADS);
	if (thread_pool == NULL) {
		fprintf(stderr, "Failed to start thread pool.\n");
		return 1;
	}

	printf("Started listening on port %s\nPress return to stop...\n", LISTEN_PORT);
	while(1) {
		// Poll for readable data
		temp = master;
		if (select(sockfd+1, &temp, NULL, NULL, NULL) < 0) {
			fprintf(stderr, "Select failed. ");
			break;
		}

		if (FD_ISSET(STDIN, &temp)) { // Read from standard input
			char ch;	
			while((ch = getchar()) != '\n' && ch != EOF); // Clear STDIN
			break;
		} else if (FD_ISSET(sockfd, &temp)) { // New connection
			struct sockaddr_storage client_addr;
			socklen_t addr_size = sizeof(client_addr);
			int cfd;

			// Accept incoming connection
			if ((cfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size)) < 0) {
				fprintf(stderr, "Failed to accept: %s\n", strerror(errno));
				continue;
			}

			// Enqueue job to thread pool
			int *fd = (int *) malloc(sizeof(int));
			if (fd == NULL) {
				continue;
			} 
			*fd = cfd;
			pool_enqueue(thread_pool, (void *)fd, 1);
		}
	}

	// Stop listening & join all threads
	printf("Stopping...\n");
	closeSocket(sockfd);
	pool_end(thread_pool);
	pthread_exit(NULL);
}