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
#include <dirent.h>

#define PORT_NO			20001
#define SERVER_IP		"127.0.0.1"

#define JEF_FILE_UP		0
#define JEF_FILE_DOWN	1
#define JEF_FILE_NEWEST	2
#define JEF_FILE_LIST	3

#define F_HDR			0
#define END_HDR			9

void error(const char *msg, int end){
	perror(msg);
	if(end)
		exit(-1);
}

short handshake(short socket_id){
	short header;
	ssize_t len = recv(socket_id, &header, sizeof(header), 0);
	if(len<0)
		error("handshake", 1);
	header ^= 0x1ef0;
	return header;
}

void send_header(short socket_id, unsigned char h_type){
	short header = 0xf110 | (h_type & 0xf);
	ssize_t len = send(socket_id, &header, sizeof(header), 0);
	if(len<0)
		error("header", 1);
}

void send_file_amount(short socket_id, short file_amount){
	ssize_t len = send(socket_id, &file_amount, sizeof(short), 0);
	if(len<0)
		error("file amount: ",1);
}

void send_file_name(short socket_id, char * file_name){
	ssize_t len = send(socket_id, file_name, 256, 0);
	if(len<0)
		error("file name", 1);
}

void send_file_size(short socket_id, int file_size){
	ssize_t len = send(socket_id, &file_size, sizeof(file_size), 0);
	if(len<0)
		error("file name", 1);
}

void send_file(short socket_id, short fd, int file_size){
	long offset = 0;
	int remain_data = file_size;
	int sent_bytes = 0;
	while(((sent_bytes = sendfile(socket_id, fd, &offset, 256))>0) && (remain_data>0)){
		remain_data -= sent_bytes;
		printf("sent: %d\tdone: %.2f%%\n", sent_bytes, 100.0-(remain_data*1.0)/(file_size*1.0)*100.0);
	}
}

const char * recv_file_name(short socket_id){
	static char file_name[256];
	ssize_t len = recv(socket_id, file_name, 256, 0);
	if(len<0)
		error("file name", 1);
	return file_name;
}

int recv_file_size(short socket_id){
	int file_size;
	ssize_t len = recv(socket_id, &file_size, sizeof(file_size), 0);
	if(len<0)
		error("no file", 1);
	return file_size;
}

short recv_file_amount(short socket_id){
	short file_amount;
	ssize_t len = recv(socket_id, &file_amount, sizeof(file_amount), 0);
	if(len<0)
		error("no amount", 1);
	return file_amount;
}

void recv_file(short socket_id, char * file_name, int file_size){
	char buffer[256];
	ssize_t len;
	int remain_data = file_size;
	FILE *rec_file;
	rec_file = fopen(file_name, "w");
	if(rec_file == NULL)
		error("file", 1);

	printf("%s: %d bytes\n", file_name, file_size);

	int full_count = file_size/sizeof(buffer);
	for(int i=0; i<full_count; i++){
		len = recv(socket_id, buffer, sizeof(buffer), 0);
		if(len<0)
			error("len", 1);
		fwrite(buffer, sizeof(char), len, rec_file);
		remain_data -= len;
		printf("received: %d bytes, %d bytes remaining\n", len, remain_data);
	}
	if(remain_data){
		len = recv(socket_id, buffer, file_size%256, 0);
		if(len<0)
			error("len", 1);
		fwrite(buffer, sizeof(char), len, rec_file);
		remain_data -= len;
		printf("received: %d bytes, %d bytes remaining\n", len, remain_data);
	}

	// this is not working for multiple files, bad data from recv
	// while((remain_data>0) && (len = recv(socket_id, buffer, sizeof(buffer), 0))>0){
	// 	fwrite(buffer, sizeof(char), len, rec_file);
	// 	remain_data -= len;
	// 	printf("received: %d bytes, %d bytes remaining\n", len, remain_data);
	// }

	printf("\n");
	fclose(rec_file);
}

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
	printf("peer: %s\n", inet_ntoa(peer_addr.sin_addr));

	short header = handshake(peer_socket);

	if(header==JEF_FILE_UP){
		short file_count = recv_file_amount(peer_socket);
		int file_size;
		char file_name[256];
		for(int i=0; i<file_count; i++){
			strcpy(file_name, recv_file_name(peer_socket));
			file_size = recv_file_size(peer_socket);
			recv_file(peer_socket, file_name, file_size);
		}

	}else if(header==JEF_FILE_DOWN){
		short file_amount = recv_file_amount(peer_socket);
		char file_name[256];
		for(short i=0; i<file_amount; i++){
			strcpy(file_name, recv_file_name(peer_socket));

			int fd = open(file_name, O_RDONLY);
			if(fd<0)
				error("opening file", 1);
			struct stat file_stat;
			if(fstat(fd, &file_stat)<0)
				error("fstat", 1);
			
			int file_size = file_stat.st_size;
			send_file_size(peer_socket, file_size);
			send_file(peer_socket, fd, file_size);
		}
	}else if(header==JEF_FILE_LIST){
		DIR *dir = opendir("/home/xesar/prog/c/jef-linux-server");
		struct dirent *dir_ent;
		if(dir==NULL)
			error("dir", 1);

		while(dir_ent=readdir(dir)){
			if(dir_ent->d_type==8){
				send_header(peer_socket, F_HDR);
				FILE *f_ptr;
				f_ptr = fopen(dir_ent->d_name, "r");
				fseek(f_ptr, 0, SEEK_END);
				int f_len = ftell(f_ptr);
				send_file_name(peer_socket, dir_ent->d_name);
				send_file_size(peer_socket, f_len);
				fclose(f_ptr);
			}
		}
		send_header(peer_socket, END_HDR);

		closedir(dir);

	}
	close(peer_socket);
	close(server_socket);

	return 0;
}