#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>

void error(const char *msg, int end){
	perror(msg);
	if(end)
		exit(-1);
}

#define PORT_NO		20001
#define SERVER_IP	"127.0.0.1"

int main(int argc, char *argv[]){
	int server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(server_socket<0)
		error("socket", 1);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NO);
	inet_pton(AF_INET, SERVER_IP, &(server_addr.sin_addr));

	if(bind(server_socket, (struct sockaddr *) &server_addr, sizeof(struct sockaddr))<0)
		error("bind", 1);

	if(listen(server_socket, 5)<0)
		error("listen", 1);

	struct sockaddr_in peer_addr;
	socklen_t sock_len = sizeof(struct sockaddr_in);
	int peer_socket = accept(server_socket, (struct sockaddr *) &peer_addr, &sock_len);
	if(peer_socket<0)
		error("accept", 1);
	fprintf(stdout, "peer: %s\n", inet_ntoa(peer_addr.sin_addr));

	char file_name[256];
	recv(peer_socket, file_name, 256, 0);
	fprintf(stdout, "file name: %s\n", file_name);

	char buffer[256];
	recv(peer_socket, buffer, 256, 0);
	int file_size = atoi(buffer);
	fprintf(stdout, "file size: %d bytes\n", file_size);

	FILE *received_file;
	received_file = fopen(file_name, "w");
	if(received_file == NULL)
		error("file", 1);

	int remain_data = file_size;
	ssize_t len;
	while((remain_data>0) && ((len = recv(peer_socket, buffer, 256, 0))>0)){
		fwrite(buffer, sizeof(char), len, received_file);
		remain_data -= len;
		fprintf(stdout, "received: %d bytes, %d bytes remaining\n", len, remain_data);
	}
	fclose(received_file);
	close(peer_socket);
	close(server_socket);

	return 0;
}