#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>

void signal_handler(int sig)
{
	printf("child [%d] received signal %d\n", getpid(), sig);
	return;
}

int main()
{
	char * argls[] = {"/bin/ls", "-R", NULL};
	pid_t pid;
	pid_t pid_array[10];
	char cwd[PATH_MAX];
	int status;
	int w;
	int i;

	chdir("/ptreefs/");

	printf("============================================\n");
	printf("Before creating processes.\n");
	printf("============================================\n");
	fflush(stdout);

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "error: %s\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		execv(argls[0], argls);
		/* Should not reach here */
		fprintf(stderr, "error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	w = waitpid(pid, &status, 0);
	if (w < 0)
		return EXIT_FAILURE;

	printf("\n============================================\n");
	printf("Creating children processes.\n");
	printf("============================================\n");
	fflush(stdout);

	for (i = 0; i < 10; i++) 
		pid_array[i] = 0;

	for (i = 0; i < 10; i++) {
		pid = fork();

		if (pid < 0) {
			fprintf(stderr, "error: %s\n", strerror(errno));
			return EXIT_FAILURE;
		}

		if (pid == 0) {
			signal(SIGUSR1, signal_handler);
			pause();
			exit(0);
		}

		pid_array[i] = pid;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "error: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		execv(argls[0], argls);
		/* Should not reach here */
		fprintf(stderr, "error: %s\n", strerror(errno));
		exit(1);
	}

	w = waitpid(pid, &status, 0);
	if (w < 0)
		return EXIT_FAILURE;

	for (i = 0; i < 10; i++)
		kill(pid_array[i], SIGUSR1);

	for (i = 0; i < 10; i++)
		wait(NULL);

	printf("==========================================\n");
	printf("Terminate processes.\n");
	printf("==========================================\n");

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "error: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (pid == 0) {
		execv(argls[0], argls);
		/* Should not reach here */
		fprintf(stderr, "error: %s\n", strerror(errno));
		exit(1);
	}

	w = waitpid(pid, &status, 0);
	if (w < 0)
		return EXIT_FAILURE;

	return 0;
}