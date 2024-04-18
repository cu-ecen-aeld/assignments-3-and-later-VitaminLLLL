#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

bool caught_sig = false;
static void signal_handler(int signal_number) {
    int errno_saved = errno;
    if ( signal_number == SIGINT || signal_number == SIGTERM ) {
        caught_sig = true;
    }
    errno = errno_saved;
}

int main(int argc, char** argv)
{
	bool daemon = false;
	if (argc == 2 && strncmp(argv[1], "-d", 2) == 0) {
		daemon = true;
	}
	struct addrinfo hints;
	struct addrinfo *addr;
	struct sockaddr accept_sockaddr;
	socklen_t len = sizeof(struct sockaddr);
	char ipstr[INET_ADDRSTRLEN];
	int backlog =  5;
	char buf[BUFFER_SIZE];
	ssize_t rec_len = BUFFER_SIZE - 1;

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("Failed to create socket");
		return -1;
	}
	
	memset(&hints, 0, sizeof(struct addrinfo)); 
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int res = getaddrinfo(NULL, "9000", &hints, &addr);
	if (res != 0) {
		perror("Failed to getaddrinfo");
		return -1;
	}

	// Avoid port is not release.
	int reuse = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	res = bind(sockfd, addr->ai_addr, addr->ai_addrlen);
	if (res != 0) {
		perror("Failed to bind");
		freeaddrinfo(addr);
		return -1;
	}
	freeaddrinfo(addr);
	
	if (daemon) {
		int pid = fork();
		if ( pid < 0) {
			perror("fork");
			return -1;
		} else if (pid != 0) {
			exit(0);
		}
	} 

	res = listen(sockfd, backlog);
	if (res != 0) {
		perror("Failed to listen.\n");
		return -1;
	}

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
		return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
		return -1;
    }

	openlog(NULL, 0 , LOG_USER);
	// forever loop to keep accept the connection
	memset(buf, 0, BUFFER_SIZE);
	while (1) {
		rec_len = BUFFER_SIZE - 1;
		FILE *fp = fopen("/var/tmp/aesdsocketdata", "a+");
		if (!fp) {
			perror("Failed to open");
			closelog();
			return -1;
		}

		int acceptsockfd = accept(sockfd, &accept_sockaddr, &len);
		if (acceptsockfd < 0) {
			if (caught_sig)	{
				syslog(LOG_DEBUG, "Caught signal, exiting");
				close(sockfd);
				fclose(fp);
				remove("/var/tmp/aesdsocketdata");
				break;
			}
			perror("Failed to accept");
			return -1;
		}
		memset(ipstr, 0, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, &accept_sockaddr, ipstr, INET_ADDRSTRLEN);
		syslog(LOG_DEBUG, "Accepted connection from %s", ipstr);
		
		// Receive data
		while (rec_len == (BUFFER_SIZE - 1)) {
			rec_len = recv(acceptsockfd, buf, BUFFER_SIZE - 1, 0);
			if (rec_len < 0 ) {
				perror("Failed to receive");
				return -1;
			} else if (rec_len != 0){
				buf[BUFFER_SIZE - 1] = '\0';
				if (fprintf(fp, "%s", buf) < 0){
					perror("Failed to write");
					return -1;
				}
			}
			memset(buf, 0, BUFFER_SIZE);
		}

		// Send the whole file
	    long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* buffer = malloc(file_size + 1);
		if ( buffer == NULL ){
			perror("malloc");
			return -1;
		}
		size_t bytes_read = fread(buffer, 1, file_size, fp);
		if (bytes_read != file_size) {
			perror("fread");
			return -1;
		}
		buffer[file_size] = '\0';
   		int bytes_sent = send(acceptsockfd, buffer, strlen(buffer), 0);
	    if (bytes_sent < 0) {
	        perror("Failed to send");
	        return -1;
	    }
	    free(buffer);
	    memset(buf, 0, BUFFER_SIZE);
		close(acceptsockfd);
		fclose(fp);
		syslog(LOG_DEBUG, "Closed connection from %s", ipstr);
	}
	
	return 0;
}
