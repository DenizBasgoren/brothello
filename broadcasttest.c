

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

void* tcp_main( void* arg );
const int PORT = 10101;

int main(char a, char**b) {

	int server_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sd == -1) {
		puts("er1");
		exit(1);
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	int result = bind(server_sd, (struct sockaddr*)&addr, sizeof(addr));
	if ( result == -1) {
		close(server_sd);
		puts("er2");
		exit(1);
	}
	result = listen(server_sd, 3);
	if ( result == -1) {
		close(server_sd);
		puts("er3");
		exit(1);
	}

	printf("Listening on port %d\n", PORT);

	while(true) {

		struct sockaddr client_adr;
		socklen_t client_adrlen = sizeof(struct sockaddr);

		int client_sd = accept(server_sd, &client_adr, &client_adrlen);
		if (client_sd == -1) {
			close(server_sd);
			puts("er4");
			fprintf(stdout, "err:%s", strerror(errno));
			exit(1);
		}

		printf("Connection from ");
		for (int i = 0; i<1; i++) {
			printf("%d: %hu.", i, ntohs( client_adr.sa_data[i] ) );
		}

		printf("Connection len %d, sd %d\n", client_adrlen, client_sd);

		pthread_t tcp_thread;
		int* tcp_arg = malloc( sizeof(int) );
		*tcp_arg	= client_sd;
		result = pthread_create(&tcp_thread, NULL, tcp_main, tcp_arg);
		if (result) {
			close(client_sd);
			close(server_sd);
			free(tcp_arg);
			puts("er5");
			exit(1);
		}
	}

	close(server_sd);
	return 0;
}


void* tcp_main( void* arg ) {

	int client_sd = *(int*)arg;
	free(arg);

	char response[128] = {0};

	while(1) {
		int result = read(client_sd, response, 128);
		if (result==0 || result==-1) {
			break;
		}

		printf("MSG by %d: %s\n", client_sd, response);

	}




	close(client_sd);
}
