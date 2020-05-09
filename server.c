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
#include <time.h>

#define PORT_NO			20001
#define SERVER_IP		"127.0.0.1"
#define FILES_DIR		"files/"

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
	char * ptr;
	ptr = strrchr(file_name, '/');
	if(ptr!=NULL)
		strcpy(file_name, ptr+1);
	ssize_t len = send(socket_id, file_name, 256, 0);
	if(len<0)
		error("file name", 1);
}

void send_file_size(short socket_id, int file_size){
	ssize_t len = send(socket_id, &file_size, sizeof(file_size), 0);
	if(len<0)
		error("file name", 1);
}

void send_file_time(short socket_id, long timestamp){
	ssize_t len = send(socket_id, &timestamp, sizeof(timestamp), 0);
	if(len<0)
		error("time stamp", 1);
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
	char * ptr;
	ptr = strrchr(file_name, '/');
	if(ptr!=NULL)
		strcpy(file_name, ptr+1);
	return file_name;
}

char * to_path(const char * file_name){
	static char temp[256];
	strcpy(temp, file_name);
	size_t len = strlen(FILES_DIR);
	memmove(temp+len, temp, strlen(temp)+1);
	memcpy(temp,FILES_DIR,len);
	return temp;
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

	// this is not working for multiple files
	// recv sucks in too much data, including more sends from client than one
	// while((remain_data>0) && (len = recv(socket_id, buffer, sizeof(buffer), 0))>0){
	// 	fwrite(buffer, sizeof(char), len, rec_file);
	// 	remain_data -= len;
	// 	printf("received: %d bytes, %d bytes remaining\n", len, remain_data);
	// }

	printf("\n");
	fclose(rec_file);
}

int timesort(const struct dirent **a, const struct dirent **b){
	struct stat a_stat, b_stat;
	char a_name[256], b_name[256];
	strcpy(a_name, to_path((*a)->d_name));
	strcpy(b_name, to_path((*b)->d_name));
	stat(a_name, &a_stat);
	stat(b_name, &b_stat);
	return (a_stat.st_mtime>b_stat.st_mtime)? 1: -1;
}

int dirfilter(const struct dirent *a){
	if(strlen(a->d_name)==1 && a->d_name[0]=='.')
		return 0;
	if(strlen(a->d_name)==2 && a->d_name[1]=='.' && a->d_name[0]=='.')
		return 0;
	return 1;
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
			strcpy(file_name, to_path(recv_file_name(peer_socket)));
			file_size = recv_file_size(peer_socket);
			recv_file(peer_socket, file_name, file_size);
		}
	}else if(header==JEF_FILE_DOWN){
		short file_amount = recv_file_amount(peer_socket);
		for(short i=0; i<file_amount; i++){
			char file_name[256];
			strcpy(file_name, to_path(recv_file_name(peer_socket)));
			
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
		struct dirent ** file_ents;

		int n = scandir(FILES_DIR, &file_ents, 0, alphasort);

		if(n<3)
			error("scandir", 1);

		for(int i=2; i<n; i++){
			struct stat file_stat;
			int len = stat(to_path(file_ents[i]->d_name), &file_stat);
			if(len!=0){
				send_header(peer_socket, END_HDR);
				error("file stat", 1);
			}

			send_header(peer_socket, F_HDR);
			send_file_name(peer_socket, file_ents[i]->d_name);
			send_file_size(peer_socket, file_stat.st_size);
			send_file_time(peer_socket, file_stat.st_mtime);

			free(file_ents[i]);
		}
		free(file_ents);

		send_header(peer_socket, END_HDR);
	}else if(header==JEF_FILE_NEWEST){
		short file_amount = recv_file_amount(peer_socket);

		struct dirent ** file_ents;

		int n = scandir(FILES_DIR, &file_ents, dirfilter, timesort);
		if(n<1)
			error("scandir", 1);

		if(file_amount>n)
			file_amount=n;

		send_file_amount(peer_socket, file_amount);

		for(int i=0; i<file_amount; i++){
			char full_name[256];
			struct stat file_stat;
			strcpy(full_name, to_path(file_ents[i]->d_name));
			int len = stat(full_name, &file_stat);
			if(len!=0)
				error("file stat", 1);

			int fd = open(full_name, O_RDONLY);
			if(fd<0)
				error("opening file", 1);

			send_file_name(peer_socket, full_name);
			send_file_size(peer_socket, file_stat.st_size);
			send_file(peer_socket, fd, file_stat.st_size);

			free(file_ents[i]);
		}
		free(file_ents);
	}
	
	close(peer_socket);
	close(server_socket);

	return 0;
}