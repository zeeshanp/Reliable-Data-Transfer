#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "packet.h"

int main(int argc, char** argv)
{
    int sockfd,n;
    struct sockaddr_in servaddr,cliaddr;
    char* mesg = malloc(PACKET_SIZE * sizeof(char));
    socklen_t len;

    if (argc != 4)
    {
        printf("usage: ./receiver <Sender HostName> <Sender PortNumber> <Filename>\n");
        exit(1);
    }

    /* Set up Socket Stuff */

    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(argv[1]);
    servaddr.sin_port=htons(atoi(argv[2]));

    /* Set socket to nonblocking */
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    /* initialize packet stream */
    struct packet_stream ps = make_packet_stream(10);

    /* Send SYN to server */
    printf("Initializing handshake, sending SYN...\n");
    struct packet syn = create_packet(0, 0, 0, 0, 0);
    addPacketToStream(&ps, syn);
    sendto(sockfd, syn.data, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    int fp;
    if ((fp = open(argv[3], O_WRONLY|O_CREAT|O_EXCL)) < 0)
    {
        fprintf(stderr, "Error opening: %d", errno);
    }

    /*  Receive file using go-back-n */
    int expectedSeqNum = 1;
    for (;;)
    {
        bzero(mesg, sizeof(mesg));
        n = recvfrom(sockfd, mesg, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, &len);
        if (n > 0)
        {
            printf("Received packet! ");
            struct packet pkt = extract_packet(mesg);
            
            if (isCorrupt(pkt))
                continue;

            if (pkt.type == 1 && expectedSeqNum == pkt.seqNum) //synack
            {
                printf("SynACK. Sending request for %s...\n", argv[3]);
                struct packet hsACK = create_packet(2, strlen(argv[3]), 0, expectedSeqNum++, argv[3]);
                sendto(sockfd, hsACK.data, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                addPacketToStream(&ps, hsACK);               
            }
            else if (pkt.type == 3) //file data
            {
                if (pkt.seqNum == expectedSeqNum)
                {
                    printf("Packet matches expectedSeqNum: %d, saving to disk...\n", expectedSeqNum);
//                    printf("\n\n DATA: %s\n\n", getData(pkt));
                    if (write(fp, getData(pkt), pkt.size) < 0)
                        printf("write failed: %d\n", errno);
                    printf("Sending ACK: %d...\n", ++expectedSeqNum);
                    struct packet ACK = create_packet(4, 0, 0, expectedSeqNum, NULL);
                    sendto(sockfd, ACK.data, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                }
                else
                {
                    printf("Wrong packet received, resending ack %d...\n", expectedSeqNum);
                    struct packet ack = create_packet(4, 0, 0, expectedSeqNum, 0);
                    sendto(sockfd, ack.data, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                
                }
            }
            else if (pkt.type == 5) //fin
            {
                //send finack
                printf("fin. Done downloading, sending FIN-ACK. goodbye\n");
                struct packet finack = create_packet(6, 0, 0, 0, 0);
                sendto(sockfd, finack.data, PACKET_SIZE, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                close(fp);
                return 0;
            }
           
        }
    }

    //should never get here
    return 0;



    /* 
    sendto(sockfd, argv[3], strlen(argv[3]), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    socklen_t fromlen = si`zeof(servaddr);
    for (;;)
    {
        n = recvfrom(sockfd, recvline, 4, 0, (struct sockaddr *)&servaddr, &fromlen);
        printf("%d\n", n);
        recvline[n] = 0;
        if (!strcmp("404", recvline) && n != 0)
        {
            printf("File not found on server.\n");
            exit(1);
        }
        else if (n > 0)
        {
            break;
        }
    } */

/*
     //for now receive file normally
   int fp;
    fp = open(argv[3], O_WRONLY|O_CREAT|O_EXCL);

    if (fp < 0) //file exists;
    {
        char* filename = malloc((strlen(argv[3]) + 3)*sizeof(char));
        strcpy(filename, argv[3]);
        strcat(filename, "(1)");
        fp = open(filename, O_WRONLY);
    }
   for(;;)
    {
       n = recvfrom(sockfd, recvline, PACKET_SIZE, 0, NULL, NULL);
        recvline[n] = 0;
        if (!strcmp("7777", recvline))
        {
            printf("Done downloading file.\n");
            return 0;
        }
    write(fp, recvline, n);

    } 
  */  
    return 0;
}
