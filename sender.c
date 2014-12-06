#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "packet.h"
#include <sys/time.h>

double timestamp()
{
    return clock()/CLOCKS_PER_SEC/1000;
}

/* Use this to simulate packet loss/corruption */
int corruptedFile(double p_loss, double p_corrupt)
{
    time_t t;
  //  srand((unsigned) time(&t));
    double p = p_loss + p_corrupt;
    double thresh = p*100;
    return rand()%100 <= thresh;
}

int main(int argc, char**argv)
{
    int sockfd, n, fp;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(servaddr);
    char mesg[PACKET_SIZE];
    int size;

    double p_loss = .05;
    double p_corrupt = .05;

    if (argc != 2)
    {
        printf("usage: ./sender <PortNumber>");
        exit(1);
    }

    /* Set up Socket Stuff */

    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(atoi(argv[1]));
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

    /* Set socket to nonbinding */
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    /* these will hold file  */
    int filesize;
    char* filebuffer;

    /* Go-Back-N Protocol to transmit file */
    int base = 0;
    int nextSeqNum = 1;
    int N = 5;
    struct packet_stream ps = make_packet_stream(N);
    time_t timer = 0;
    int ftp_done = 0;
    int handshake_done = 0;
    int last_packet = -1;

    //    printf("FileSize: %d, PACKET_SIZE - HEADER_SIZE: %d\n", filesize, PACKET_SIZE-HEADER_SIZE);
    printf("(%f) Server now running...\n", timestamp());
    for (;;)
    {
        
        bzero(mesg, sizeof(mesg));
        n = recvfrom(sockfd, mesg, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n > 0)
        {
            
            struct packet ack = extract_packet(mesg);
        //    printf("ACK NUM: %d ", ack.ackNum);
            if (!isCorrupt(ack))
            {
                printf("(%f) Receiving pkt w/ACK: %d. Base: %d => %d: ", timestamp(), ack.ackNum, base, base+1);
                base = ack.ackNum + 1;
                if (base == nextSeqNum)
                    timer = 0; //stop timer
                else
                    timer = time(NULL);

                if (ack.type == 0) //syn
                {
                    //send out SYNACK
                    printf(" SYN, sending out SYNACK...\n");
                    struct packet synack = create_packet(1, 0, nextSeqNum, 0, NULL);
                    sendto(sockfd, synack.data, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                    addPacketToStream(&ps, synack);
                    nextSeqNum++;
                }
                else if (ack.type == 2) //handshake ack
                {
                    printf("Request for %s, opening file...\n", getData(ack));
                    char* filename = getData(ack);
                    filename[ack.size] = 0; 
                    fp = open(filename, O_RDONLY);
                    if (fp < 0)
                    {
                        printf("Could not find file.\n");
                        exit(1);
                    }
                    struct stat st;
                    stat(filename, &st);
                    filesize = st.st_size;
                    filebuffer = (char *)malloc(filesize * sizeof(char));
                    read(fp, filebuffer, filesize);
                    handshake_done = 1;
                } 
                else if (last_packet == ack.ackNum && ftp_done)
                {
                    printf("Done transfering file. Sending FIN...\n");
                    struct packet fin = create_packet(5, 0, 0, 0, 0);
                    sendto(sockfd, fin.data, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                    addPacketToStream(&ps, fin);
                    printf("Waiting for FIN-ACK..\n");
                    for (;;)
                    {
                        bzero(mesg, sizeof(mesg));
                        n = recvfrom(sockfd, mesg, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
                        if (n > 0)
                        {
                           struct packet finack = extract_packet(mesg);
                           if (finack.type == 6)
                           {
                               printf("(%f) FIN-ACK received. Goodbye!\n", timestamp());
                               return 0;
                           }
                        }
                    }
                }
                else
                {
                    printf("\n");
                }
            }
        }
        //lets see if we can send any packets
        if (!ftp_done && handshake_done && nextSeqNum < base + N)
        {
            printf("(%f) Sending out packet: SequenceNumber: %d...", timestamp(), nextSeqNum);
            if (filesize > PACKET_SIZE - HEADER_SIZE)
            {
                printf("\n");
                struct packet pkt = create_packet(3, PACKET_SIZE - HEADER_SIZE, nextSeqNum, 0, filebuffer);
                if (!corruptedFile(p_loss, p_corrupt))
                    sendto(sockfd, pkt.data, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                else
                    printf("Packet lost!...\n");
                
                timer = time(NULL);
                addPacketToStream(&ps, pkt);
                filesize -= (PACKET_SIZE - HEADER_SIZE);
                filebuffer += (PACKET_SIZE - HEADER_SIZE);
              //  printf("\n\n DATA: %s\n\n", getData(pkt));
            }
            else
            {
                printf("(last packet)\n");
                struct packet pkt = create_packet(3, filesize, nextSeqNum, 0, filebuffer);
                sendto(sockfd, pkt.data, PACKET_SIZE, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                addPacketToStream(&ps, pkt);
                ftp_done = 1;
                last_packet = nextSeqNum + 1;
             //   printf("\n\n DATA: %s\n\n", getData(pkt));
                
            }
            nextSeqNum++;
        }

        
        int endtimer = time(NULL);
        if (endtimer - timer >= 5 && timer != 0)
        {
            printf("(%f) Timeout: Resending all packets from %d to %d...\n", timestamp(), base, nextSeqNum -1);
            resendAll(sockfd, cliaddr, &ps, base, nextSeqNum - 1);
            timer = time(NULL);
        }

    }
    free_packet_stream(&ps);
    //wait for fin-ack
    return 0;

}

      

