#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>

#define PACKET_SIZE 1024
#define HEADER_SIZE (5*sizeof(int))

//packet structure:
//|type|size|seqNum|ackNum|checksum|------data|
//  4    4     4       4       4     1024-5*4

struct packet {
    int type; //0 - SYN, 1 - SYNACK, 2 - handshakeACK, 3 - DATA, 4 - ACK, 5 - FIN, 6 - FINACK
    int size; //size of data
    int seqNum; 
    int ackNum;
    int checkSum;
    char data[PACKET_SIZE]; //header info needs to be copied into data before sending
};


//packet_stream:
//holds all packets in the window.
struct packet_stream {
    int N; //window size;
    int numPackets;
    struct packet* packets;
    
};

void printPacket(struct packet pkt)
{
    printf("Packet Summary:\n Type: %d, size: %d, seqNum: %d, ackNum: %d, checkSum: %d\n Data: %s\n", pkt.type, pkt.size, pkt.seqNum, pkt.ackNum, pkt.checkSum, pkt.data+HEADER_SIZE);
}

struct packet_stream make_packet_stream(int N)
{
    struct packet_stream ps;
    ps.N = N;
    ps.numPackets = 0;
    ps.packets = NULL;
    return ps;
}

void resendAll(int sockfd, struct sockaddr_in addr, struct packet_stream* ps, int base, int end)
{
    for (int i = 0; i < ps->numPackets; i++)
    {
        sendto(sockfd, ps->packets[i].data, PACKET_SIZE, 0, (struct sock_addr *)&addr, sizeof(addr));
    }
}

void addPacketToStream(struct packet_stream* ps, struct packet pkt)
{
    ps->packets = (struct packet*)realloc(ps->packets, (ps->numPackets+1) * sizeof(struct packet));
    ps->packets[ps->numPackets++] = pkt;
}

void free_packet_stream(struct packet_stream* ps)
{
    free(ps->packets);
}

int computeCheckSum(char *data, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++)
    {
        sum += data[i];
    }
    return sum;
}

//data contains actual info to be sent
struct packet create_packet(int type, int size, int seqNum, int ackNum, char* data)
{
    struct packet ret;
    ret.type = type;
    ret.size = size;
    ret.seqNum = seqNum;
    ret.ackNum = ackNum;
    ret.checkSum = computeCheckSum(data, size);
    
    //put header into char array
    char* ptr = ret.data;
    strncpy(ptr, &type, sizeof(int));
    ptr += sizeof(int);
    strncpy(ptr, &size, sizeof(int));
    ptr += sizeof(int);
    strncpy(ptr, &seqNum, sizeof(int));
    ptr += sizeof(int);
    strncpy(ptr, &ackNum, sizeof(int));
    ptr += sizeof(int);
    strncpy(ptr, &(ret.checkSum), sizeof(int));
    ptr += sizeof(int);
    //copy array of real data in
    strncpy(ptr, data, size);
    return ret;
}

//get packet out of buffer from udp packet
struct packet extract_packet(char* ptr)
{
    char* mesg = ptr;
    int type = (*(int *)mesg);
    mesg += sizeof(int); 
    int size = (*(int *)mesg);
    mesg += sizeof(int);
    int seqNum = (*(int *)mesg);
    mesg += sizeof(int);
    int ackNum = (*(int *)mesg);
    mesg += sizeof(int);
    int checkSum = (*(int *)mesg);
    mesg += sizeof(int);
  
    return create_packet(type, size, seqNum, ackNum, mesg);

}

char* getData(struct packet pkt)
{
    return pkt.data + HEADER_SIZE;
}

int isCorrupt(struct packet pkt)
{
    int a = computeCheckSum(pkt.data + HEADER_SIZE, pkt.size);
    int b = pkt.checkSum;
    return a != b;    
}

