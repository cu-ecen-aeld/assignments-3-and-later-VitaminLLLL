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
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#define BUFFER_SIZE 1024

struct thread_data {
	pthread_mutex_t mutex;
	int sockfd;
	bool is_thread_alive;
	char ipstr[INET_ADDRSTRLEN];
};

struct thread_info {
	pthread_t id;
	struct thread_data data;
	SLIST_ENTRY(thread_info) entries;
};

// Signal handling
bool caught_sig = false;
static void signal_handler(int signal_number) {
    int errno_saved = errno;
    if ( signal_number == SIGINT || signal_number == SIGTERM ) {
        caught_sig = true;
    }
    errno = errno_saved;
}

// Handler thread
static void *server_thread(void* arg) {
	struct thread_data *data = (struct thread_data *) arg;
	ssize_t rec_len = BUFFER_SIZE - 1;
	char buf[BUFFER_SIZE];

	// 1. Open the file
	FILE *fp = fopen("/var/tmp/aesdsocketdata", "a+");
	if (!fp) {
		perror("Failed to open");
		pthread_exit(NULL);
	}

	bool is_locked = false;
	// loop for keep receiving
	while (1) {
		// 2. receive the data and lock the file to write the data
		// lock the mutex for FILE operation
		// loop for receiving all the content.
		while (1) {
			rec_len = recv(data->sockfd, buf, BUFFER_SIZE - 1, 0);
			if (rec_len < 0 ) {
				if (caught_sig) {
					if (is_locked) {
						pthread_mutex_unlock(&data->mutex);
					}
					close(data->sockfd);
					syslog(LOG_DEBUG, "Closed connection from %s", data->ipstr);
					fclose(fp);
					syslog(LOG_DEBUG, "Caught signal, thread exiting");
					data->is_thread_alive = false;
					pthread_exit(NULL);
				}
				perror("Failed to receive");
				pthread_exit(NULL);
			} else if (rec_len == 0){
				close(data->sockfd);
				syslog(LOG_DEBUG, "Closed connection from %s", data->ipstr);
				fclose(fp);
				data->is_thread_alive = false;
				pthread_exit(NULL);
			} else {
				if (!is_locked) {
					pthread_mutex_lock(&data->mutex);
					is_locked = true;
				}
				buf[rec_len] = '\0';
				if (fprintf(fp, "%s", buf) < 0){
					perror("Failed to write");
					pthread_mutex_unlock(&data->mutex);
					data->is_thread_alive = false;
					pthread_exit(NULL);
				}
				if (buf[rec_len - 1] == '\n') {
					memset(buf, 0, BUFFER_SIZE);
					break;
				}
			}
			memset(buf, 0, BUFFER_SIZE);
		}
		// 3. read the whole file
		// allocate the buffer for send.
		long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* buffer = malloc(file_size + 1);
		if ( buffer == NULL ){
			perror("malloc");
			pthread_mutex_unlock(&data->mutex);
			data->is_thread_alive = false;
			pthread_exit(NULL);
		}
		size_t bytes_read = fread(buffer, 1, file_size, fp);
		if (bytes_read != file_size) {
			perror("fread");
			pthread_mutex_unlock(&data->mutex);
			data->is_thread_alive = false;
			pthread_exit(NULL);
		}
		// 4. unlock the mutex and send the buffer.
		pthread_mutex_unlock(&data->mutex);
		is_locked = false;
		buffer[file_size] = '\0';
		int bytes_sent = send(data->sockfd, buffer, strlen(buffer), 0);
		if (bytes_sent < 0) {
		    perror("Failed to send");
		    free(buffer);
		    data->is_thread_alive = false;
		    pthread_exit(NULL);
		}
		free(buffer);
	}
}

void timer_handler(union sigval sigval) {
    pthread_mutex_t *mutex = (pthread_mutex_t *) sigval.sival_ptr;
    time_t current_time;
    struct tm *timeinfo;
    char buffer[80];

	pthread_mutex_lock(mutex);
	FILE *fp = fopen("/var/tmp/aesdsocketdata", "a+");
	if (!fp) {
		perror("Failed to open");
		return;
	}
    time(&current_time);
    timeinfo = localtime(&current_time);
    strftime(buffer, sizeof(buffer), "%a, %d %b %y %T %z", timeinfo);
	if (fprintf(fp, "timestamp: %s\n", buffer) < 0){
		perror("Failed to write");
		return;
	}
	fclose(fp);
    pthread_mutex_unlock(mutex);
}

int main(int argc, char** argv)
{
	bool daemon = false;
	if (argc == 2 && strncmp(argv[1], "-d", 2) == 0) {
		daemon = true;
	}
	struct addrinfo hints;
	struct addrinfo *addr;
	int backlog =  5;
	struct sockaddr accept_sockaddr;
	socklen_t len = sizeof(struct sockaddr);
	pthread_mutex_t mutex;
	SLIST_HEAD(thread_info_head, thread_info) thread_info_head;
	timer_t timerid;
	struct sigevent sev;
	struct itimerspec its;

	// 1. Create the socket.
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("Failed to create socket");
		return -1;
	}

	// 2. Get socket address to bind.
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

	// 3. Bind the socket.
	res = bind(sockfd, addr->ai_addr, addr->ai_addrlen);
	if (res != 0) {
		perror("Failed to bind");
		freeaddrinfo(addr);
		return -1;
	}
	freeaddrinfo(addr);

	// 4 Init mutex
	res = pthread_mutex_init(&mutex, NULL);
	if (res != 0) {
		perror("pthread_init_mutex");
		return -1;
	}

	// 5.1. Check the daemon mode
	if (daemon) {
		int pid = fork();
		if ( pid < 0) {
			perror("fork");
			return -1;
		} else if (pid != 0) {
			exit(0);
		}
	}
	// 5.1.1 Create timer
	memset(&sev,0,sizeof(struct sigevent));
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_notify_function = timer_handler;
	sev.sigev_value.sival_ptr = &mutex;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
		perror("timer_create");
		return -1;
	}
	its.it_value.tv_sec = 10;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 10;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(timerid, 0, &its, NULL) == -1) {
		perror("timer_settime");
		return -1;
	}

	// 5.2 Init signal handling.
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
    // 5.3 Init syslog
	openlog(NULL, 0 , LOG_USER);
	// 5.4 Init thread list
	SLIST_INIT(&thread_info_head);

	// 6. Listen to the socket
	res = listen(sockfd, backlog);
	if (res != 0) {
		perror("Failed to listen.\n");
		return -1;
	}

	// 7. Accept loop
	//    When receive a new connection, create a thread for the handling.
	while (1) {
		int acceptsockfd = accept(sockfd, &accept_sockaddr, &len);
		if (acceptsockfd < 0) {
			if(caught_sig) {
				syslog(LOG_DEBUG, "Caught signal, main thread exiting");
				break;
			}
			perror("Failed to accept");
			return -1;
		}
		// Allocate memory and insert to the list
		struct thread_info *new_element = (struct thread_info *) calloc(1, sizeof(struct thread_info));
		new_element->data.mutex = mutex;
		new_element->data.sockfd = acceptsockfd;
		new_element->data.is_thread_alive = true;
		memset(new_element->data.ipstr, 0, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, &accept_sockaddr, new_element->data.ipstr, INET_ADDRSTRLEN);
		syslog(LOG_DEBUG, "Accepted connection from %s", new_element->data.ipstr);

		// Create thread
		res = pthread_create(&new_element->id, NULL, server_thread, (void *)&new_element->data);
		if(res != 0) {
			perror("pthread_create");
			return -1;
		}

		// Remove the joined thread
		struct thread_info *element = SLIST_FIRST(&thread_info_head);
		while (element) {
			struct thread_info *tmp = element;
			element = SLIST_NEXT(element, entries);
			if (!tmp->data.is_thread_alive) {
				pthread_join(tmp->id, NULL);
				SLIST_REMOVE(&thread_info_head, tmp, thread_info, entries);
				free(tmp);
			}
		}
		SLIST_INSERT_HEAD(&thread_info_head, new_element, entries);
	}

    // Clear all the resourse
    while (!SLIST_EMPTY(&thread_info_head)) {
        struct thread_info *element = SLIST_FIRST(&thread_info_head);
		// Wait for thread to join
		pthread_join(element->id, NULL);
        SLIST_REMOVE_HEAD(&thread_info_head, entries);
        free(element);
    }
	close(sockfd);
	remove("/var/tmp/aesdsocketdata");
	return 0;
}
