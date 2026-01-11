#include "common.h"
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <sys/types.h>
#include <algds/vec.h>

typedef struct session {
    struct sockaddr_storage client_addr; 
    int udp_sock;
    time_t last_active;
} Session;

void Session_show(Session self, FILE *fp) {}

uint32_t global_seq = 1;
uint32_t global_spi = 0;

VECTOR_DEF_AS(Session, VSession);
VECTOR_IMPL_AS(Session, VSession);

VSession sessions;

Session* get_session(struct sockaddr_storage *addr, struct config *cfg) {
    for (int i = 0; i < sessions.size; i++) {
        Session *curr = &sessions.buffer[i];
        if (sockaddr_cmp((struct sockaddr*)&curr->client_addr, (struct sockaddr*)addr) == 0) {
            curr->last_active = time(NULL);
            return curr;
        }
    }

    Session new_node;
    memset(&new_node, 0, sizeof(Session));
    memcpy(&new_node.client_addr, addr, sizeof(struct sockaddr_storage));
    new_node.last_active = time(NULL);

    struct sockaddr_storage target = {0};
    socklen_t tlen;
    
    if (is_ipv6_str(cfg->target_ip)) {
        struct sockaddr_in6 *t6 = (struct sockaddr_in6 *)&target;
        t6->sin6_family = AF_INET6;
        inet_pton(AF_INET6, cfg->target_ip, &t6->sin6_addr);
        t6->sin6_port = htons(cfg->target_port);
        tlen = sizeof(struct sockaddr_in6);
        new_node.udp_sock = socket(AF_INET6, SOCK_DGRAM, 0);
    } else {
        struct sockaddr_in *t4 = (struct sockaddr_in *)&target;
        t4->sin_family = AF_INET;
        t4->sin_addr.s_addr = inet_addr(cfg->target_ip);
        t4->sin_port = htons(cfg->target_port);
        tlen = sizeof(struct sockaddr_in);
        new_node.udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if (new_node.udp_sock < 0) {
        perror("Create UDP socket failed");
        return NULL;
    }

    if (connect(new_node.udp_sock, (struct sockaddr*)&target, tlen) < 0) {
        perror("Connect to target failed");
        close(new_node.udp_sock);
        return NULL;
    }

    VSession_push_back(&sessions, new_node);
    
    printf("New session created. UDP Socket: %d\n", new_node.udp_sock);
    return &sessions.buffer[sessions.size - 1];
}

void cleanup_sessions() {
    time_t now = time(NULL);
    for (int i = 0; i < sessions.size; i++) {
        Session *curr = &sessions.buffer[i];
        if (now - curr->last_active > 60) {
            close(curr->udp_sock);
            VSession_remove(&sessions, i);
            i--;
            printf("Session closed due to timeout.\n");
        }
    }
}

void send_esp_to_client(int raw_sock, 
                       struct sockaddr_storage *dest, const char *data, int len) {
    char packet[BUF_SIZE];
    struct esp_header *esp = (struct esp_header *)packet;
    
    esp->spi = htonl(global_spi);
    esp->seq = htonl(global_seq++);
    memcpy(packet + sizeof(struct esp_header), data, len);
    
    int sock_to_use = -1;
    socklen_t slen;
    sock_to_use = raw_sock;
    slen = sizeof(struct sockaddr_in6);

    if (sock_to_use > 0) {
        int sent = sendto(sock_to_use, packet, sizeof(struct esp_header) + len, 0, 
               (struct sockaddr*)dest, slen);
        if (sent < 0) perror("Send ESP failed");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s CONFIG\n", argv[0]);
        exit(-1);
    }
    struct config cfg = {0};
    load_config(argv[1], &cfg, 1);
    VSession_init(&sessions);

    while(global_spi < 5000) global_spi = gen_rand_32b();
    global_seq = gen_rand_32b();

    int raw_sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ESP);
    if (raw_sock < 0) { 
        perror("Socket v6 failed (warning)"); 
    }

    if (raw_sock > 0 && is_ipv6_str(cfg.server_ip)) {
        struct sockaddr_in6 bind_addr = {0};
        bind_addr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, cfg.server_ip, &bind_addr.sin6_addr);
        if (bind(raw_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            perror("Bind v6 failed");
            return 1;
        }
    } else {
        fprintf(stderr, "failed to create ipv6 raw socket to bind.\n");
        exit(-1);
    }

    printf("Server Ready (Raw Socket Mode). Listening ESP on [%s] -> %s:%d\n", 
           cfg.server_ip, cfg.target_ip, cfg.target_port);

    fd_set readfds;
    char buffer[BUF_SIZE];
    struct timeval tv;
    struct sockaddr_storage src_addr;
    socklen_t addr_len;

    while (1) {
        FD_ZERO(&readfds);
        int max_fd = -1;

        if (raw_sock > 0) {
            FD_SET(raw_sock, &readfds);
            if (raw_sock > max_fd) max_fd = raw_sock;
        }
        for (int i = 0; i < sessions.size; i++) {
            Session *curr = &sessions.buffer[i];
            FD_SET(curr->udp_sock, &readfds);
            if (curr->udp_sock > max_fd) max_fd = curr->udp_sock;
        }

        tv.tv_sec = 10; tv.tv_usec = 0;
        int ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (ret == 0) { cleanup_sessions(); continue; }
        if (ret < 0) { perror("select error"); break; }

        if (raw_sock > 0 && FD_ISSET(raw_sock, &readfds)) {
            addr_len = sizeof(src_addr);
            int n = recvfrom(raw_sock, buffer, BUF_SIZE, 0, (struct sockaddr*)&src_addr, &addr_len);
            if (n > 0) {
                if (n > sizeof(struct esp_header)) {
                    char *payload_ptr = buffer + sizeof(struct esp_header);
                    int payload_len = n - sizeof(struct esp_header);

                    Session *sess = get_session(&src_addr, &cfg);
                    if (sess) {
                        send(sess->udp_sock, payload_ptr, payload_len, 0);
                    }
                }
            }
        }

        for (int i = 0; i < sessions.size; i++) {
            Session *curr = &sessions.buffer[i];
            if (FD_ISSET(curr->udp_sock, &readfds)) {
                int n = recv(curr->udp_sock, buffer, BUF_SIZE, 0);
                if (n > 0) {
                    curr->last_active = time(NULL);
                    // printf("server udp recv, len: %d\n", n);
                    send_esp_to_client(raw_sock, &curr->client_addr, buffer, n);
                }
            }
        }
        
        static int loop_count = 0;
        if (++loop_count % 6 == 0) cleanup_sessions();
    }
    
    if (raw_sock > 0) close(raw_sock);
    return 0;
}