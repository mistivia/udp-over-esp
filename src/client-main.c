#include "common.h"
#include <netinet/in.h>
#include <netinet/ip.h> // for struct ip

struct sockaddr_storage client_addr; // Store UDP client addr (v4 or v6)
socklen_t client_addr_len = 0;
int has_client = 0;
uint32_t global_seq;
uint32_t global_spi = 0;

int is_same_addr(struct sockaddr *a, struct sockaddr *b) {
    if (a->sa_family != b->sa_family) return 0;
    if (a->sa_family == AF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in*)a;
        struct sockaddr_in *b4 = (struct sockaddr_in*)b;
        return a4->sin_addr.s_addr == b4->sin_addr.s_addr;
    } else if (a->sa_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6*)a;
        struct sockaddr_in6 *b6 = (struct sockaddr_in6*)b;
        return memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(struct in6_addr)) == 0;
    }
    return 0;
}

void send_udp_to_client(int udp_sock, const char *data, int len) {
    if (!has_client) return;
    sendto(udp_sock, data, len, 0, (struct sockaddr*)&client_addr, client_addr_len);
}

void send_esp_to_server(int raw_sock, struct sockaddr *dest, socklen_t dest_len, const char *data, int len) {
    char packet[BUF_SIZE];
    struct esp_header *esp = (struct esp_header *)packet;
    
    esp->spi = htonl(global_spi);
    esp->seq = htonl(global_seq++);

    memcpy(packet + sizeof(struct esp_header), data, len);
    printf("client esp send, len: %d\n", (int)sizeof(struct esp_header) + len);
    sendto(raw_sock, packet, sizeof(struct esp_header) + len, 0, dest, dest_len);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s CONFIG\n", argv[0]);
        exit(-1);
    }
    struct config cfg = {0};
    load_config(argv[1], &cfg, 1);
    while (global_spi < 5000) {
        global_spi = gen_rand_32b();
    }
    global_seq = gen_rand_32b();

    // listen local udp
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    local_addr.sin_port = htons(cfg.local_port);
    if (bind(udp_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("UDP bind failed"); return 1;
    }

    // server addr parsing
    int raw_sock;
    struct sockaddr_storage server_addr = {0};
    socklen_t server_addr_len;
    int family;
    if (is_ipv6_str(cfg.server_ip)) {
        family = AF_INET6;
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&server_addr;
        s6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, cfg.server_ip, &s6->sin6_addr);
        server_addr_len = sizeof(struct sockaddr_in6);
    } else {
        fprintf(stderr, "server must be ipv6.\n");
        exit(-1);
    }

    // create raw socket for proto 50
    raw_sock = socket(family, SOCK_RAW, IPPROTO_ESP);
    if (raw_sock < 0) { perror("Raw socket failed"); return 1; }

    printf("Client started. Listen UDP :%d, Tunnel to [%s]\n", cfg.local_port, cfg.server_ip);

    fd_set readfds;
    char buffer[BUF_SIZE];
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(udp_sock, &readfds);
        FD_SET(raw_sock, &readfds);
        
        int max_fd = (udp_sock > raw_sock ? udp_sock : raw_sock) + 1;
        
        // multiplexing
        int act = select(max_fd, &readfds, NULL, NULL, NULL);
        if (act < 0) continue;

        if (FD_ISSET(udp_sock, &readfds)) {
            struct sockaddr_storage src;
            socklen_t slen = sizeof(src);
            int n = recvfrom(udp_sock, buffer, BUF_SIZE, 0, (struct sockaddr*)&src, &slen);
            if (n > 0) {
                printf("client udp recv, len: %d\n", n);
                memcpy(&client_addr, &src, slen);
                client_addr_len = slen;
                has_client = 1;
                send_esp_to_server(raw_sock, (struct sockaddr*)&server_addr, server_addr_len, buffer, n);
            }
        }

        if (FD_ISSET(raw_sock, &readfds)) {
            struct sockaddr_storage src_ip_addr;
            socklen_t slen = sizeof(src_ip_addr);

            int n = recvfrom(raw_sock, buffer, BUF_SIZE, 0, (struct sockaddr*)&src_ip_addr, &slen);
            printf("client esp recv, len: %d\n", n);
            if (n > 0) {
                // esp addr filter
                if (!is_same_addr((struct sockaddr*)&server_addr, (struct sockaddr*)&src_ip_addr)) {
                    continue; 
                }
                int esp_total_len = n;

                // packet length check and decode
                if (esp_total_len >= (int)sizeof(struct esp_header)) {
                    char *payload_ptr = buffer + sizeof(struct esp_header);
                    int payload_len = esp_total_len - sizeof(struct esp_header);
                    
                    if (payload_len > 0) {
                        printf("client udp send, len: %d\n", payload_len);
                        send_udp_to_client(udp_sock, payload_ptr, payload_len);
                    }
                }
            }
        }
    }
    return 0;
}