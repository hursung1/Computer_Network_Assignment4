# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <strings.h>
# include <netdb.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <netinet/in.h>
# include <time.h>

# define BUF_LEN 1392

/* Structure */
typedef struct packet{
    char file[BUF_LEN];
	int len;
    int seqno;
}PACKET;

typedef struct queue{
	struct queue *next;
	char pckt[BUF_LEN];
	int pckt_len;
	int pcktnum;
}QUEUE;

/* Function */
void set_buf(int connfd);

int main(int argc, char **argv){
	struct sockaddr_in saddr, caddr;
    int connfd, //socket 
		caddr_len = sizeof(caddr), 
		fd, //file
		seqno, //received packet's sequence number
		i, //for index in loop
		lastseq = -1, //Sequence number which receiver successfully received
		preack = -1, //Sequence number which is sent to sender recently
		read_len;
	float lossprob, tmp;
	double time_int, 
			start, 
			checkpoint;
	char buf[BUF_LEN] = {0}, filename[BUF_LEN] = {0}, logfile[BUF_LEN] = {0};
	FILE *lfd; //log file
	QUEUE *front = NULL, *end = NULL, *node = NULL; 
	PACKET *pckt = (PACKET*) calloc (1, sizeof(PACKET));

	srand(time(NULL));
	
	while(1){
		printf("Packet Loss Probability: ");
		scanf("%f", &lossprob);
		if(lossprob < 0 || lossprob > 1){
			printf("\nPacket Loss SHOULD BE 0 ~ 1.\n");
		}
		else break;
	}
	if((connfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ //UDP Connection
		puts("Socket Error");
		exit(1);
	}
	set_buf(connfd);
	puts("receiver program starts... ");
	//Initiallize server address
	bzero((char*)&saddr, sizeof(saddr)); //initiallize
	saddr.sin_family = AF_INET; //Internet
	saddr.sin_addr.s_addr = htonl(INADDR_ANY); //Allow connection with any IP
	saddr.sin_port = htons(10080); //Port number

	if(bind(connfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0){
		puts("bind Error");
		exit(1);
	}
	
	recvfrom(connfd, filename, BUF_LEN, 0, (struct sockaddr*)&caddr, (socklen_t*)&caddr_len); //Get file name from sender
	strcpy(logfile, filename); 
	strcat(logfile, "_receiving_log.txt");

	if((fd = open(filename, O_WRONLY|O_CREAT, 0644)) < 0){ //Open file
		puts("File Open Error");
		exit(0);
	}

    lfd = fopen(logfile, "w"); //Open log file
	if(!lfd){
		puts("Log File Open Error");
		exit(0);
	}

	start = ((double)clock())/CLOCKS_PER_SEC;
	while(1){
		memset(pckt, 0, sizeof(*pckt));
		memset(buf, 0, BUF_LEN);

		// receive Packet from sender
		recvfrom(connfd, pckt, sizeof(*pckt), 0, (struct sockaddr*)&caddr, (socklen_t*)&caddr_len);
		checkpoint = (((double)clock())/CLOCKS_PER_SEC)-start;
		read_len = pckt->len;
		seqno = pckt->seqno;
		memcpy(buf, pckt->file, read_len);
		
		if(!strncmp(buf, "fin", 3)){ // File transfer Finished
			fprintf(lfd, "File Transfer Finished.\nThroughput: %.2lf pkts/sec\n", preack/checkpoint);
			//printf("File Transfer Finished.\nThroughput: %.2lf pkts/sec\n", (preack-1)/time_int);
			break;
		}
		fprintf(lfd, "%.3lf pkt: %d | received\n", checkpoint, seqno);
		//printf("%.3lf pkt: %d | received\n", (double)(checkpoint-start)/CLOCKS_PER_SEC, seqno);
		tmp = (double)(rand()%1000)/1000;

		if(tmp < lossprob){ //Packet Loss Occurred. Write info on filename_receiving_log.txt
			fprintf(lfd, "%.3lf pkt: %d | dropped\n", checkpoint, seqno);
			//printf("%.3lf pkt: %d | dropped\n", (double)(checkpoint-start)/CLOCKS_PER_SEC, seqno);
		}
		else{ //Packet Loss NOT Occurred
			if(seqno == preack+1){ // Seqno is contiguous: Store pckt into file
				write(fd, buf, read_len);
				if(seqno == lastseq + 1){ //(1) Pckt sequentially received
					preack += 1;
				}
				else{ //(2) Dropped pckt received
					if(front && (front->pcktnum == (seqno+1))){ // Buffer not empty: Buffer also should be stored into file.
						node = front;
						while(node && node->next && (node->pcktnum == ((node->next->pcktnum) - 1))){
							//printf("store in-buffer PCKT to FILE: %d\n", node->pcktnum);
							write(fd, node->pckt, node->pckt_len);
							node = node->next;
							front->next = node;
						}
						//printf("store in-buffer PCKT to FILE: %d\n", node->pcktnum);
						write(fd, node->pckt, node->pckt_len);
						preack = node->pcktnum;
						if(node->next) front = node->next;
						else{
							front = NULL; end = NULL;
						}
					}
					else{ // Buffer is empty
						preack += 1;
						lastseq -= 1;
					}
				}
			}
			else{ // Seqno is NOT contiguous: Loss Occurred. Store pckt into Temporary Buffer
				/* Store pckt into temporary buffer... */
				node = (QUEUE*) calloc(1, sizeof(QUEUE));
				memcpy(node->pckt, buf, read_len);
				node->pckt_len = read_len;
				node->pcktnum = seqno;
				node->next = NULL;
				if(!front){ //Empty Queue
					front = node;
					end = node;
				}
				else{ // Not Empty Queue
					end->next = node;
					end = node;
				}
				//printf("ACK Not Contiguous: %dth pckt stored in QUEUE.\n", node->pcktnum);
			}
			sendto(connfd, &preack, sizeof(preack), 0, (struct sockaddr*)&caddr, sizeof(caddr));
			//printf("%.3lf ACK: %d | sent\n", (double)(clock()-start)/CLOCKS_PER_SEC, preack);
			fprintf(lfd, "%.3lf ACK: %d | sent\n", checkpoint, preack);
			lastseq += 1;
		}
	}

	/* 뒷정리 */
	close(fd);
	close(connfd);
	fclose(lfd);
	free(pckt);
	
    return 0;
}

void set_buf(int connfd){
	int bsize = 0, rb = sizeof(int);
	getsockopt(connfd, SOL_SOCKET, SO_RCVBUF, (char*)&bsize, &rb);
	printf("Socket receive Buffer Size: %d\n", bsize);
	if(bsize < 10000000){
		bsize = 10000000;
		setsockopt(connfd, SOL_SOCKET, SO_RCVBUF, (char*)&bsize, rb);
		getsockopt(connfd, SOL_SOCKET, SO_RCVBUF, (char*)&bsize, &rb);
		printf("Socket receive Buffer Size UPDATED: %d\n", bsize);
	}
}