#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <net/ethernet.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

#define BUF_SIZE 4096

struct esp_header {
    uint32_t spi;
    uint32_t seq;
};

struct config {
    char server_ip[64];
    char interface[32];
    int local_port;
    char target_ip[64];
    int target_port;
};

int is_ipv6_str(const char *ip_str);
void load_config(const char *filename, struct config *cfg, int is_server);
int sockaddr_cmp(struct sockaddr *a, struct sockaddr *b);
uint32_t gen_rand_32b();

#endif