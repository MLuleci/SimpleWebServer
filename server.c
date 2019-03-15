/*
 * Compile using flags detailed in:
 * https://computing.llnl.gov/tutorials/pthreads/#Compiling
*/

// Misc.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

// Multi-threading
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

// MIME types (add more as needed)
typedef struct MimeType {
	char* ext;
	char* type;
} MimeType;

const struct MimeType mime[] = {
	{ "htm", "text/html" },
    { "html", "text/html" },
    { "xml", "text/xml" },
    { "txt", "text/plain" },
    { "css", "text/css" },
    { "png", "image/png" },
    { "gif", "image/gif" },
    { "jpg", "image/jpg" },
    { "jpeg", "image/jpeg" },
    { "zip", "application/zip"}
};

/** 
 * Close socket
 * \param sock The socket file descriptor to close
 * \return 0 (success) or 1 (fail)
*/
int closeSocket(int sock) {
	if (close(sock) < 0) {
		fprintf(stderr, "Failed to close socket: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

/**
 * Send error
 * \param sock The socket to send error response to
 * \param code The error code to send
*/
void sendError(int sock, int code) {
	char *error;

	switch(code) {
		case 501:
			error = "HTTP/1.1 501 Not Implemented";
			break;
		case 404:
			error = "HTTP/1.1 404 Not Found";
			break;
		default:
			error = "HTTP/1.1 500 Internal Server Error";
			break;
	}

	send(sock, error, strlen(error), 0);
}

/**
 * Make response header
 * 
 * It's the caller's responsibility to free the returned string.
 * 
 * \param mime_type Content-type to use
 * \param size Content-length to use
 * \return Pointer to a string containing the header, NULL on failure
*/
char *makeHeader(char *mime_type, long size) {
	char *tmp;
	int len;

	// Status line
	char status [] = "HTTP/1.1 200 OK\r\n";

	// Date
	time_t rawtime;
	struct tm *timeinfo;
	char timebuf [41] = { 'D', 'a', 't', 'e', ':', ' ' };

	time(&rawtime);
	timeinfo = gmtime(&rawtime);
	strftime(timebuf + 6, 35, "%a, %d %b %y %T %Z%z\r\n", timeinfo);

	// Connection
	char conn [] = "Connection: close\r\n";

	// Content type
	tmp = "Content-Type: ";
	char *con_type = NULL;
	if (mime_type) {
		len = strlen(tmp) + strlen(mime_type) + 3;
		con_type = (char *) malloc(len * sizeof(char));
		if (con_type) sprintf(con_type, "%s%s\r\n", tmp, mime_type);
	}

	// Content length
	tmp = "Content-Length: ";
	char *con_len = (char *) malloc((strlen(tmp) + 14) * sizeof(char));
	if (con_len) sprintf(con_len, "%s%ld\r\n", tmp, size);

	// Composition
	len = strlen(status) + strlen(timebuf) + strlen(conn) + (con_type ? strlen(con_type) : 0) + (con_len ? strlen(con_len) : 0) + 3;
	char *header = (char *) malloc(len * sizeof(char));
	if (!header) return NULL;

	if (con_type && con_len) {
		sprintf(header, "%s%s%s%s%s\r\n", status, timebuf, conn, con_type, con_len); // Both OK
	} else if (!con_type && con_len) {
		sprintf(header, "%s%s%s%s\r\n", status, timebuf, conn, con_len); // con_type fail
	} else if (con_type && !con_len) {
		sprintf(header, "%s%s%s%s\r\n", status, timebuf, conn, con_type); // con_len fail
	} else {
		sprintf(header, "%s%s%s\r\n", status, timebuf, conn); // Both fail
	}

	free(con_type);
	free(con_len);
	return header;
}

/**
 * Handle client request
 * \param arg The socket file descriptor of the connection
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
	length = (void *) p - buf;
	method = (char *) malloc((length + 1) * sizeof(char));

	if (method != NULL) {
		strncpy(method, buf, length);
		method[length] = '\0';

		// Only accept implemented methods
		if (!strcmp(method, "GET")) {

			// Get requested URL
			char *b, *e, *url;
			b = strchr(buf, '/');
			e = strchr(b, ' ');
			length = e - b + 1;
			url = (char *) malloc((length + 1) * sizeof(char));

			if (url != NULL) {
				strncpy(url + 1, b, length);
				url[0] = '.';
				url[length] = '\0';

				// Get requested file
				if (!strcmp(url, "./")) { // Index requested
					free(url);
					url = strdup("./index.html");
				}
				FILE* fp = fopen(url, "r");

				// Get file extension
				char *ext = NULL;
				b = strchr(url + 1, '.');
				if (b != NULL) {
					e = url + strlen(url);
					length = e - b;
					ext = strndup(b + 1, length);
					for(char* i = ext; *i; i++)
						*i = tolower(*i);
				}
				free(url);

				// Get MIME type
				char* mime_type = NULL;
				if (ext != NULL) {
					length = sizeof(mime) / sizeof(MimeType);
					for(int i = 0; i < length; i++) {
						const MimeType *temp = &mime[i];
						if (!strcmp(ext, temp->ext)) {
							mime_type = temp->type;
							break;
						}
					}
					free(ext);
				}

				// Send data to client
				if (fp != NULL) {
					// Get file size
					fseek(fp, 0, SEEK_END);
					long size = ftell(fp);
					rewind(fp);

					// Get header & allocate space for reply
					char *header = makeHeader(mime_type, size);
					length = (header ? strlen(header) : 0);
					void *reply = malloc((size + length) * sizeof(char));

					if (reply != NULL) {
						// Attach header
						if(header) memcpy(reply, header, length);
			
						// Read file into memory
						fread(reply + length, sizeof(char), size, fp);

						// Send data until we're done
						int total = 0;
						long bytes_left = size + length;
						int n;
						while(total < size) {
							if ((n = send(cfd, reply + total, bytes_left, 0)) < 0) {
								break;
							}
							total += n;
							bytes_left -= n;
						}

						if (header) free(header);
						free(reply);
					} else { // malloc fail: reply
						sendError(cfd, 500);
					}
					
					fclose(fp);
				} else { // 404 Not Found
					sendError(cfd, 404);
				}
			} else { // malloc fail: url
				sendError(cfd, 500);
			}
		} else { // 501 Not Implemented
			sendError(cfd, 501);
		}

		free(method);
	} else { // malloc fail: method
		sendError(cfd, 500);
	}

	closeSocket(cfd);
	free(buf);
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