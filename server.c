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

	short header;
	ssize_t len = recv(peer_socket, &header, sizeof(header), 0);
	if(len<0)
		error("handshake", 1);

	char file_name[256];
	int file_size, remain_data;
	
	switch(header){
	case 0x1ef0:
		recv(peer_socket, file_name, 256, 0);
		fprintf(stdout, "file name: %s\n", file_name);

		char buffer[256];
		recv(peer_socket, buffer, 256, 0);
		file_size = atoi(buffer);
		fprintf(stdout, "file size: %d bytes\n", file_size);

		FILE *received_file;
		received_file = fopen(file_name, "w");
		if(received_file == NULL)
			error("file", 1);

		remain_data = file_size;
		while((remain_data>0) && ((len = recv(peer_socket, buffer, 256, 0))>0)){
			fwrite(buffer, sizeof(char), len, received_file);
			remain_data -= len;
			fprintf(stdout, "received: %d bytes, %d bytes remaining\n", len, remain_data);
		}
		fclose(received_file);
		break;
	case 0x1ef1:
		recv(peer_socket, file_name, 256, 0);
		printf("file name: %s\n", file_name);

		int fd = open(file_name, O_RDONLY);
		if(fd<0)
			error("opening file", 1);
		struct stat file_stat;
		if(fstat(fd, &file_stat)<0)
			error("fstat", 1);

		char file_size_buffer[32];
		sprintf(file_size_buffer, "%d", file_stat.st_size);
		printf("file size: %s bytes\n", file_size_buffer);
		len = send(peer_socket, file_size_buffer, 32, 0);
		if(len<0)
			error("file size", 1);

		long offset = 0;
		int remain_data = file_stat.st_size;
		int sent_bytes = 0;
		while(((sent_bytes = sendfile(peer_socket, fd, &offset, 256))>0) && (remain_data>0)){
			remain_data -= sent_bytes;
			fprintf(stdout, "sent %d bytes, %d bytes remaining\n", sent_bytes, remain_data);
		}
		break;
	case 0x1ef2:
		
		break;
	case 0x1ef3:
		
		break;
	default:

		break;
	}
	close(peer_socket);
	close(server_socket);

	return 0;
}