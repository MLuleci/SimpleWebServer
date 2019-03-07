// Misc.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

// Networking
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Constants
#define LISTEN_PORT "80"
#define BACKLOG 10
#define BUFF_LEN 10240

// Interrupt handling
int run = 1;

void interrupt(int i) {
	run = 0;
}

// Close socket: returns 0 (success) or 1 (fail) 
int closeSocket(int sock) {
	if (close(sock) < 0) {
		fprintf(stderr, "Failed to close socket: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

int main(int argc, char** argv) {
	// Bind SIGINT to handler
	struct sigaction act;
	act.sa_handler = interrupt;
	act.sa_flags = SA_RESTART;
	sigaction(SIGINT, &act, NULL);

	// Starting variables
	struct addrinfo hints, *res;
	int status, sockfd;

	memset(&hints, 0, sizeof hints);
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
		fprintf(stderr, "Failed to bind socket\n");
		return 1;
	}

	// Listen
	if (listen(sockfd, BACKLOG) < 0) {
		fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
		return 1;
	}

	printf("Started listening on port %s\nPress ^C to stop...\n", LISTEN_PORT);
	while(run) {
		struct sockaddr_storage client_addr;
		socklen_t addr_size;
		int cfd;
		clock_t start, end;

		// Accept incoming connection
		start = clock();
		addr_size = sizeof client_addr;
		if ((cfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size)) < 0) {
			fprintf(stderr, "Failed to accept: %s\n", strerror(errno));
			continue;
		}

		printf("Accepted connection from ");
		if (client_addr.ss_family == AF_INET) {
			char ip[INET_ADDRSTRLEN];
			struct sockaddr_in *ipv4 = (struct sockaddr_in *)&client_addr;
			inet_ntop(AF_INET, &(ipv4->sin_addr), ip, INET_ADDRSTRLEN);
			printf("%s", ip);
		} else {
			char ip[INET6_ADDRSTRLEN];
			struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&client_addr;
			inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip, INET6_ADDRSTRLEN);
			printf("%s", ip);
		}
		printf(" on port %s	<<<\n", LISTEN_PORT);

		// Recieve data (0 recieved means client has disconnected)
		void* buf = calloc(BUFF_LEN, sizeof(char));
		if ((status = recv(cfd, buf, BUFF_LEN, 0)) <= 0) {
			fprintf(stderr, "Failed to recv: %s\n", strerror(errno));
			closeSocket(cfd);
			free(buf);
			continue;
		}
		printf("Recieved %d bytes\n", status);

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
			printf("Requested: %s\n", url);

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
				fread(reply, sizeof(char), size, fp);

				// Send data until we're done
				int bytes_sent;
				long total_sent = 0;
				void *ptr = reply;
				do {
					bytes_sent = send(cfd, ptr, size - total_sent, 0);
					total_sent += bytes_sent;
					ptr += bytes_sent;
				} while(total_sent != size);

				free(reply);
				fclose(fp);
			}
			
			free(url);
		} else { // 501 Not Implemented
			const char *message = "HTTP/1.1 501 Not Implemented";
			send(cfd, message, strlen(message), 0);
		}

		end = clock();
		double elapsed = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
		printf("Reply sent (%f ms)	>>>\n", elapsed);
		closeSocket(cfd);
		free(buf);
		free(method);
	}

	// Stop listening & free resources
	printf("Stopping...\n");
	closeSocket(sockfd);
	freeaddrinfo(res);
	return 0;
}
