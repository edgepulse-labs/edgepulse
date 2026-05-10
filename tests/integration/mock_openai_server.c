#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	static const char body[] =
		"{\"choices\":[{\"message\":{\"content\":\"local model response\"}}]}";
	char response[512];
	char request[2048];
	struct sockaddr_in addr;
	int one = 1;
	int port;
	int server_fd;
	int client_fd;

	if (argc != 2) {
		fprintf(stderr, "usage: mock_openai_server <port>\n");
		return 2;
	}

	port = atoi(argv[1]);
	if (port <= 0 || port > 65535) {
		fprintf(stderr, "invalid port: %s\n", argv[1]);
		return 2;
	}

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket");
		return 1;
	}

	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons((unsigned short)port);

	if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "bind: %s\n", strerror(errno));
		close(server_fd);
		return 1;
	}
	if (listen(server_fd, 1) != 0) {
		perror("listen");
		close(server_fd);
		return 1;
	}

	client_fd = accept(server_fd, NULL, NULL);
	if (client_fd < 0) {
		perror("accept");
		close(server_fd);
		return 1;
	}

	if (read(client_fd, request, sizeof(request)) < 0) {
		perror("read");
		close(client_fd);
		close(server_fd);
		return 1;
	}
	snprintf(response, sizeof(response),
		 "HTTP/1.1 200 OK\r\n"
		 "Content-Type: application/json\r\n"
		 "Content-Length: %zu\r\n"
		 "Connection: close\r\n\r\n%s",
		 strlen(body), body);
	if (write(client_fd, response, strlen(response)) < 0) {
		perror("write");
		close(client_fd);
		close(server_fd);
		return 1;
	}
	close(client_fd);
	close(server_fd);
	return 0;
}
