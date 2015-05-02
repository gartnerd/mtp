//
//  main.c
//  mtp
//
//  Created by David Gartner on 2015-05-01.
//
//  main.c
//  mtping2
//
//  Created by David Gartner on 2015-05-01.
//  Copyright (c) 2015 David Gartner. All rights reserved.
//
// prototype code for trying a multi-threaded approach to
// send out multiple ICMP echo requests
//

#ifdef HPUX
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#endif

#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#ifdef HPUX
#include <sys/socket.h>
#endif

#include <pthread.h>

#define BUFSIZE 1500

//global vars
/**********************************/
int     nsent = 1;
pid_t   pid;
/**********************************/


//global struct
/************************************************/
typedef struct {
    struct  sockaddr    *sasend;
    struct  sockaddr    *sarecv;
    socklen_t   salen;
    int         icmpproto;
} icmp_t;
/************************************************/

/***************************************/
typedef struct {
    char    *hostIp;
    int     id;
} pin_t;
/***************************************/

//function prototypes
uint16_t in_cksum(uint16_t*, int);
void    proc_v4(char*, ssize_t, struct msghdr*, struct timeval*, icmp_t*, int);
void    send_v4(int, void*, icmp_t*, int);
void*   p_init(void*);

//sruct initialization below is a C99 thing
//struct proto *pr = &(struct proto){proc_v4, send_v4, NULL, NULL, 0, 0};

int main(int argc, char *argv[]) {
    pthread_t *threadA = (pthread_t*)malloc(sizeof(pthread_t));
    pthread_t *threadB = (pthread_t*)malloc(sizeof(pthread_t));
    
    pin_t *fa1 = (pin_t*)malloc(sizeof(pin_t));
    pin_t *fa2 = (pin_t*)malloc(sizeof(pin_t));
    
    fa1->hostIp = "156.151.59.19";
    fa1->id = 10;
    
    fa2->hostIp = "142.241.240.164";
    fa2->id = 20;
    
    pthread_create(threadA, NULL, p_init, (void*)fa1);
    pthread_create(threadB, NULL, p_init, (void*)fa2);
    
    pthread_join(*threadA, NULL);
    pthread_join(*threadB, NULL);
    
    // need to add some free() calls here
    return(EXIT_SUCCESS);
}

void *p_init(void *fa){
    
    int     sockfd;
    int     size = 60 * 1024; //eventually used to create a 60kb buffer
    int     counter = 10;
    char    sendbuf[BUFSIZE];
    char    recvbuf[BUFSIZE];
    char    controlbuf[BUFSIZE];
    ssize_t n;
    struct  msghdr  msg;
    struct  iovec   iov;
    struct  timeval tval;
    struct  sockaddr_in  sai;
    struct  protoent     *proto;
    
    pin_t  *ft = (pin_t*)fa;
    int     id = ft->id;
    
    //    char *h = "156.151.59.19";
    icmp_t *pr = (icmp_t*)malloc(sizeof(icmp_t));
    
    proto = getprotobyname("icmp");
    
    sai.sin_family  = PF_INET;
    
    if (inet_pton(AF_INET, ft->hostIp, &sai.sin_addr) < 1)
        perror("inet_pton error");
    
    pid = getpid() & 0xffff;
    
    printf("pid value is: %d\n", pid);
    
    //pr = &proto_v4;
    pr->sasend  = (struct sockaddr*)&sai;
    pr->sarecv = calloc(1,sizeof(sai));
    pr->salen   = sizeof(sai);
    pr->icmpproto = proto->p_proto;
    
    //   readloop(); would go here in non-threaded program
    
#ifdef DARWIN
    sockfd = socket(pr->sasend->sa_family, SOCK_DGRAM, pr->icmpproto);
#else
    sockfd = socket(pr->sasend->sa_family, SOCK_RAW, pr->icmpproto);
#endif
    //    setuid(getuid());
    
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    
    send_v4(sockfd, sendbuf, pr, id);
    
    iov.iov_base = recvbuf;
    iov.iov_len  = sizeof(recvbuf);
    
    msg.msg_name = pr->sarecv;
    msg.msg_iov  = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = controlbuf;
    
    while(counter > 0) {
        
        //these msg values could change after a call to recvmsg
        //so re-initialize them each time through the loop
        msg.msg_namelen = pr->salen;
        msg.msg_controllen = sizeof(controlbuf);
        
        n = recvmsg(sockfd, &msg, 0);
        
        gettimeofday(&tval, NULL);
        
        proc_v4(recvbuf, n, &msg, &tval, pr, id);
        
        sleep(1); //so we don't flood the network with pings
        
        send_v4(sockfd, sendbuf, pr, id);
        printf("loop:%d\n", counter);
        counter--;
        
    }
    
    printf("run completed\n");
    return(NULL);
}

void send_v4(int fd, void* buffer, icmp_t* spr, int id)
{
    int len;
    int datalen = 56;
    int nsent = 1;
    struct icmp *icmp; //see ip_icmp.h for icmp struct def
    
    icmp = (struct icmp*)buffer;
    icmp->icmp_type = ICMP_ECHO;
    icmp->icmp_code = 0;
    
    //printf("setting id to %d\n", pid);
    
    icmp->icmp_id = id; // should be a unique value e.g. pid
    icmp->icmp_seq = nsent++;
    memset(icmp->icmp_data, 0xa5, datalen); //0xa5 is just an arbitrary value that results in an alternating bit pattern
    //    Gettimeofday((struct timeval*)icmp->icmp_data, NULL);
    
    //ICMP header is a fixed 8 bytes plus 0 or more data bytes
    len = 8 + datalen;
    icmp->icmp_cksum = 0;
    icmp->icmp_cksum = in_cksum((u_short*)icmp, len);
    
    //printf("about to send packet off\n");
    
    if (sendto(fd, buffer, len, 0, spr->sasend, spr->salen) != (ssize_t)len)
        perror("sendto error");
}

void proc_v4(char *ptr, ssize_t len, struct msghdr *msg, struct timeval *tvrecv, icmp_t* spr, int id)
{
    int     hlen1, icmplen;
    double  rtt;
    char    *dst;
    char    str[128];
    struct  ip       *ip;
    struct  icmp     *icmp;
    struct  timeval  *tvsend;
    struct  sockaddr_in *sin = (struct sockaddr_in*)spr->sarecv;
    
    ip = (struct ip*)ptr;   //start of IP header
    hlen1 = ip->ip_hl << 2; //ip_hl gives length in 32bit words. Divide by 4 to get header length in bytes
    
    if (ip->ip_p != IPPROTO_ICMP) {
        printf("packet is not an ICMP echo request\n");
        return; //only interested in ICMP packets
    }
    
    icmp = (struct icmp*)(ptr + hlen1); //move to the location in the datagram where the ICMP packet begins
    
    if ((icmplen = len - hlen1) < 8) {
        printf("size is less than minimum\n");
        return; //minimum size for ICMP packet is 8 bytes i.e. header is min 8 bytes
    }
    
    if (icmp->icmp_type == ICMP_ECHOREPLY) {
        if(icmp->icmp_id != id) {
            //printf("id %d didn't match pid: %d\n", icmp->icmp_id, id);
            return; //not a packet sent by this process
        }
        if(icmplen < 16) {
            printf("incomplete timeval struct receceived\n");
            return; //incomplete timeval struct received
        }
        
        tvsend = (struct timeval*)icmp->icmp_data;
        //tv_sub(tvrecv, tvsend);
        
        tvrecv->tv_sec -= tvsend->tv_sec;
        
        rtt = tvrecv->tv_sec * 1000.0 + tvrecv->tv_usec / 1000.0;
        
        if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
            perror("inet_ntop error");
        
        //dst = sock_ntop_host(spr->sarecv, spr->salen);
        
        //if (dst == NULL)
        //    perror("sock_ntop_host error");
        
        printf("%d bytes from %s: icmp_seq=%u, ttl=%d, rtt=%.3f ms\n",
               icmplen, str, icmp->icmp_seq, ip->ip_ttl, rtt);
    }
    
}

uint16_t in_cksum(uint16_t *addr, int len)
{
    int         nleft = len;
    uint32_t    sum = 0;
    uint16_t    *w = addr;
    uint16_t    answer = 0;
    
    //checksum algorithm from UNP
    while (nleft > 1) {
        sum = sum + *w++;
        nleft = nleft - 2;
    }
    
    if (nleft == 1) {
        *(unsigned char*)(&answer) = *(unsigned char*)w ;
        sum = sum + answer;
    }
    
    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    
    answer = ~sum;
    return(answer);
}

