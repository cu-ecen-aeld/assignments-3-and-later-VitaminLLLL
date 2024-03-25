#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char** argv) {
	openlog(NULL, 0 , LOG_USER);
	if (argc < 3) {
		fprintf(stderr, "Invalid number of arguments!\n");
		syslog(LOG_ERR, "Invalid number of arguments: %d!", argc);
		return 1;
	}
	char* writefile = argv[1];
	char* writestr = argv[2];
	FILE *fp = fopen(argv[1], "w");
	if (!fp) {
		fprintf(stderr, "Failed to open file: %s!\n", writefile);
		syslog(LOG_ERR, "Failed to open file: %s", writefile);
		return 1;
	}
	if (fprintf(fp, "%s", writestr) < 0){
		fprintf(stderr, "Failed to write %s to %s!\n", writestr, writefile);
		syslog(LOG_ERR, "Failed to write %s to %s", writestr, writefile);
	} else {
		syslog(LOG_DEBUG, "Write %s to %s", writestr, writefile);
	}
	
	if (fclose(fp) != 0) {
		fprintf(stderr, "Failed to close file: %s!\n", writefile);
		syslog(LOG_ERR, "Failed to close file: %s", writefile);
	}
	closelog();

	return 0;
}
