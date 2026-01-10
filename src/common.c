
#include "common.h"
#include <netinet/in.h>

uint32_t gen_rand_32b() {
    unsigned int high_part = (uint32_t)rand() << 16;
    unsigned int low_part = (uint32_t)rand() & 0xFFFF;
    return high_part | low_part;
}

static void rand_init() __attribute__((constructor));
static void rand_init() {;
    srand((uint32_t)time(NULL));
}

int is_ipv6_str(const char *ip_str) {
    return strchr(ip_str, ':') != NULL;
}

void load_config(const char *filename, struct config *cfg, int is_server) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("Open config failed"); exit(1); }
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[32], val[64];
        if (sscanf(line, "%[^=]=%s", key, val) == 2) {
            if (strcmp(key, "server_ip") == 0) strcpy(cfg->server_ip, val);
            if (strcmp(key, "interface") == 0) strcpy(cfg->interface, val);
            if (strcmp(key, "local_port") == 0) cfg->local_port = atoi(val);
            if (strcmp(key, "target_ip") == 0) strcpy(cfg->target_ip, val);
            if (strcmp(key, "target_port") == 0) cfg->target_port = atoi(val);
        }
    }
    fclose(fp);
}

int sockaddr_cmp(struct sockaddr *a, struct sockaddr *b) {
    if (a->sa_family != b->sa_family) return -1;
    if (a->sa_family == AF_INET) {
        return memcmp(&((struct sockaddr_in *)a)->sin_addr, 
                      &((struct sockaddr_in *)b)->sin_addr, 4);
    } else {
        return memcmp(&((struct sockaddr_in6 *)a)->sin6_addr, 
                      &((struct sockaddr_in6 *)b)->sin6_addr, 16);
    }
}
