# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <strings.h>
# include <netdb.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/time.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <arpa/inet.h>
# include <netinet/in.h>
# include <time.h>
# include <signal.h>

# define BUF_LEN 1392


/* Structure */
typedef struct packet{
    char file[BUF_LEN];
    int len;
    int seqno;
}PACKET;

/* Main Function */
int main(int argc, char **argv){
    struct sockaddr_in raddr;
    int fd, 
        winsize, 
        cnt = 0, // count for 3 duplicte ACKs
        i, 
        port = 10080,
        rec, //Receive function error check
        connfd,
        raddr_len,
        sseqno = 0, // Sequence number that recently sent to receiver
        seqack = 0, // Window base pckt sequence number in Window  
        preack = -1, // Previous ACK sequence number
        recvack, // Received ACK seqence number
        read_len, // Recently time-outted ACK sequence number
        last_seq = -10;
    double timeout, // For timer
            timer_start, //Start timer
            checkpoint, //Time that ACK received
            add_time = 0, // for timeout error
            send_time, //Time that pckt sent to receiver
            *resp_time = NULL; //Set of send_time
    struct timeval tv;
    PACKET *pckt = (PACKET*) calloc (1, sizeof(PACKET));
	char buf[BUF_LEN], logfile[BUF_LEN] = {0}, filename[BUF_LEN] = {0};
    FILE *lfd;
	
    printf("Receiver IP Address: ");
    scanf("%s", buf);
    printf("Window Size: ");
    scanf("%d", &winsize);
    printf("Timeout(sec): ");
    scanf("%lf", &timeout);
    tv.tv_sec = (int)timeout; 
    tv.tv_usec = (timeout-tv.tv_sec) * 1000000;
    resp_time = (double*) calloc(winsize, sizeof(double));

	if((connfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ //UDP Connection
		puts("Socket Error");
		exit(1);
	}
    if((i = setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval))) < 0){
        puts("Socket Setting Error");
        exit(0);
    } 

	//Initiallize server address
	bzero((char*)&raddr, sizeof(raddr)); //initiallize 
    raddr.sin_family = AF_INET; // Internet
	raddr.sin_addr.s_addr = inet_addr(buf);
	raddr.sin_port = htons(port); //Port number
    raddr_len = sizeof(raddr);
    memset(buf, 0, BUF_LEN);
    
    printf("Input File Name to send: ");
    scanf("%s", filename); 
	strcpy(logfile, filename); 
    strcat(logfile, "_sending_log.txt");
	
    sendto(connfd, filename, strlen(filename)+1, 0, (struct sockaddr*)&raddr, sizeof(raddr)); //send filename to receiver
    
    if((fd = open(filename, O_RDONLY)) < 0){ //open file
		puts("File Open Error");
        exit(0);
	}

    lfd = fopen(logfile, "w"); //Open log file
	if(!lfd){
		puts("Log File Open Error");
		exit(0);
	}
    timer_start = (double)clock();
    timer_start = timer_start/CLOCKS_PER_SEC;

    while(1){
        for(;sseqno < seqack+winsize && (read_len = read(fd, buf, BUF_LEN)) > 0; sseqno += 1){ // send packet to receiver until sequence number get same with first next-window packet number.
            memset(pckt, 0, sizeof(*pckt));

            memcpy(pckt->file, buf, read_len);
            pckt->len = read_len;
            pckt->seqno = sseqno;

            sendto(connfd, pckt, sizeof(*pckt), 0, (struct sockaddr*)&raddr, sizeof(raddr));//Send packet

            send_time = (((double)clock())/CLOCKS_PER_SEC) + add_time - timer_start;
            resp_time[sseqno%winsize] = send_time;

            fprintf(lfd, "%.3lf pkt: %d | sent\n", send_time, sseqno);
            //printf("%.3lf pkt: %d | sent\n", (double)(clock() - timer_start)/CLOCKS_PER_SEC, sseqno);

            memset(buf, 0, BUF_LEN);
        }
        if(read_len == 0){ // File reading finished
            last_seq = sseqno - 1;
            //printf("Last Seqno: %d\n", last_seq);
        }
        rec = recvfrom(connfd, &recvack, sizeof(recvack), 0, (struct sockaddr*)&raddr, (socklen_t*)&raddr_len); //receive ACK from receiver
        checkpoint = (((double)clock()) / CLOCKS_PER_SEC) + add_time - timer_start;
        if(rec != -1){ // No timeout
            fprintf(lfd, "%.3lf ACK: %d | received\n", checkpoint, recvack);
            //printf("%.3lf ACK: %d | received\n", (double)(checkpoint - timer_start)/CLOCKS_PER_SEC, recvack);
            if(last_seq == recvack){
                puts("File Transfer Finished.");
                memset(pckt, 0, sizeof(*pckt));
                memcpy(pckt->file, "fin", 3);
                pckt->len = 3;
                pckt->seqno = 0;
                sendto(connfd, pckt, sizeof(*pckt), 0, (struct sockaddr*)&raddr, sizeof(raddr));//Send final packet
                printf("File Transfer Finished.\nThroughput: %.2lf pkts/sec\n", last_seq/checkpoint);
			    fprintf(lfd, "File Transfer Finished.\nThroughput: %.2lf pkts/sec\n", last_seq/checkpoint);
                break;
            }
            if(recvack == preack){
                cnt += 1;
                if(cnt == 3){ // 3 Dup ACKs
                    fprintf(lfd, "%.3lf pkt: %d | 3 Duplicated ACKs\n", checkpoint, recvack);
                    //printf("%.3lf pkt: %d | 3 Duplicated ACKs\n", (double)(checkpoint - timer_start)/CLOCKS_PER_SEC, recvack);
                    
                    lseek(fd, BUF_LEN * (recvack+1), SEEK_SET);
                    i = read(fd, buf, BUF_LEN);
                    memset(pckt, 0, sizeof(pckt));
                    memcpy(pckt->file, buf, i);
                    pckt->len = i;
                    pckt->seqno = recvack+1;

                    sendto(connfd, pckt, sizeof(*pckt), 0, (struct sockaddr*)&raddr, sizeof(raddr));

                    resp_time[(recvack+1)%winsize] = checkpoint;
                    lseek(fd, BUF_LEN * sseqno, SEEK_SET);
                    fprintf(lfd, "%.3lf pkt: %d | sent\n", checkpoint, pckt->seqno);
                    //printf("%.3lf pkt: %d | sent\n", (double)(checkpoint - timer_start)/CLOCKS_PER_SEC, pckt->seqno);
                }
                else if(cnt > 3){
                    cnt = 3;
                }
            }
            else{
                seqack = recvack + 1;
                preack = recvack;
                if(cnt == 3){
                    cnt = 0;
                }
            }
        }
        else{ //Timeout Occurred
            //puts("Timeout!");
            add_time += timeout;
            checkpoint += timeout;
            fprintf(lfd, "%.3lf pkt: %d | timeout since %.3lf\n", checkpoint, preack+1, resp_time[(preack+1)%winsize]);
            //printf("%.3lf pkt: %d | timeout since %.3lf\n", (double)(checkpoint - timer_start)/CLOCKS_PER_SEC, preack+1, resp_time[preack%winsize]);
            
            lseek(fd, BUF_LEN * (preack+1), SEEK_SET);
            if((i = read(fd, buf, BUF_LEN)) <= 0){
                puts("Read Error!");
                exit(0);
            }

            memset(pckt, 0, sizeof(*pckt));
            memcpy(pckt->file, buf, i);

            pckt->len = i;
            pckt->seqno = preack+1;

            sendto(connfd, pckt, sizeof(*pckt), 0, (struct sockaddr*)&raddr, sizeof(raddr));

            resp_time[(preack+1)%winsize] = checkpoint;
            lseek(fd, BUF_LEN * sseqno, SEEK_SET);
            fprintf(lfd, "%.3lf pkt: %d | sent\n", checkpoint, pckt->seqno);
            //printf("%.3lf pkt: %d | sent\n", ((double)(checkpoint - timer_start))/CLOCKS_PER_SEC, pckt->seqno);
        }
    }
    close(connfd);
    free(resp_time);
    free(pckt);
    return 0;
}