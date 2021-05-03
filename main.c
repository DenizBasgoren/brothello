#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

int main(int argc, char **argv) {

	int fds[2];
	char buff[32]; // Parent reads from the read file descriptor

	pipe(fds);
	int childpid = fork();

	if (childpid == -1) {
		fprintf(stderr, "fork error!\n");
		exit(1);
	}

	// Child
	if (childpid == 0) {
		// close(fds[0]);
		char str[] = "Hello Daddy!";
		// Child writes to the write file descriptor
		fprintf(stdout, "CHILD: Waiting for 2 seconds ...\n");
		sleep(2);
		fprintf(stdout, "CHILD: Writing to daddy ...\n");
		write(fds[1], str, strlen(str) + 1);
		sleep(1);
		read(fds[0], buff, 32);
		fprintf(stdout, "CHILD: Got response from dad: %s\n", buff);
	}
	
	// Parent
	else {
		// close(fds[1]);
		fprintf(stdout, "PARENT: Reading from child ...\n");
		int num_of_read_bytes = read(fds[0], buff, 32);
		fprintf(stdout, "PARENT: Received from child: %s\n", buff);
		fprintf(stdout, "PARENT: Waitz for 2 seconds ...\n");
		sleep(2);
		fprintf(stdout, "PARENT: Replying to sweet child ...\n");
		write(fds[1], "Daughter!", 10);

	}

	return 0;
}