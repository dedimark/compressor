#include <arpa/inet.h>
#include <pthread.h>
#include <memory.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <bpf.h>
#include <unistd.h>
#include <hiredis/hiredis.h>

#include "compressor_cache_seed.h"
#include "compressor_cache_user.h"
#include "xassert.h"

struct seed_arg {
    struct forwarding_rule *rule;
    int cache_map_fd;
    uint32_t redis_addr;
    uint16_t redis_port;
};

void *seed_cache(void *arg) {
    struct seed_arg *params = (struct seed_arg *)arg;
    
    struct in_addr addr;
    addr.s_addr = params->redis_addr;
    redisContext *redis = redisConnect(inet_ntoa(addr), params->redis_port);
    if (!redis || redis->err) {
        if (redis) {
            fprintf(stderr, "Error connecting to redis: %s\n", redis->errstr);
        } else {
            fprintf(stderr, "Can't allocate redis context\n");
        }

        exit(1);
    }

    for (;;) {
        sleep(4);

        struct a2s_info_cache_entry entry = { 0 };
        bpf_map_lookup_elem(params->cache_map_fd, &params->rule->bind_addr, &entry);
        if (entry.age) {
            struct timespec tspec;
            clock_gettime(CLOCK_MONOTONIC, &tspec);
            uint64_t kernel_nsec = (tspec.tv_sec * 1e9) + tspec.tv_nsec;

            // If we have a recent response, update redis
            if (kernel_nsec - entry.age < 4e9) {
                uint64_t now = time(NULL);
                uint64_t expires = (params->rule->cache_time - (kernel_nsec - entry.age)) / 1e9;
                uint64_t age = now - ((kernel_nsec - entry.age) / 1e9);

                uint8_t *buffer = calloc(entry.len + sizeof(uint64_t), sizeof(uint8_t));
                memcpy(buffer, &age, sizeof(uint64_t));
                memcpy(buffer + sizeof(uint64_t), entry.udp_data, entry.len);

                redisReply *reply = redisCommand(redis, "SETEX %b %d %b", &params->rule->bind_addr, sizeof(uint32_t), expires, buffer, entry.len + sizeof(uint64_t));
                if (reply->type == REDIS_REPLY_ERROR) {
                    char err_buff[255];
                    strncpy(err_buff, reply->str, (reply->len > 254) ? 254 : reply->len);
                    fprintf(stderr, "Error during redis store: %s\n", err_buff);
                }
                free(buffer);
                freeReplyObject(reply);
            } else {
                redisReply *reply = redisCommand(redis, "GET %b", &params->rule->bind_addr, sizeof(uint32_t));
                if (reply->type == REDIS_REPLY_STRING) {
                    if (reply->len < 8) {
                        freeReplyObject(reply);
                        continue;
                    }

                    uint64_t cache_seconds = params->rule->cache_time / 1e9;

                    void *data = (void *)reply->str;
                    uint64_t *timestamp = (uint64_t *)data;
                    time_t now = time(NULL);

                    uint64_t cache_age = now - *timestamp;

                    if (cache_age > cache_seconds) {
                        freeReplyObject(reply);
                        continue;
                    }

                    struct a2s_info_cache_entry entry = { 0 };
                    bpf_map_lookup_elem(params->cache_map_fd, &params->rule->bind_addr, &entry);
                    if (entry.udp_data) {
                        free(entry.udp_data);
                    }

                    struct timespec tspec;
                    clock_gettime(CLOCK_MONOTONIC, &tspec);
                    uint64_t kernel_nsec = (tspec.tv_sec * 1e9) + tspec.tv_nsec;

                    uint64_t data_len = reply->len - sizeof(uint64_t);
                    entry.udp_data = malloc(data_len);
                    memcpy(entry.udp_data, data + sizeof(uint64_t), data_len);
                    entry.len = data_len;
                    entry.misses = 0;
                    entry.age = kernel_nsec - (cache_age * 1e9);

                    bpf_map_update_elem(params->cache_map_fd, &params->rule->bind_addr, &entry, BPF_ANY);
                } else if (reply->type == REDIS_REPLY_ERROR) {
                    char err_buff[255];
                    strncpy(err_buff, reply->str, (reply->len > 254) ? 254 : reply->len);
                    fprintf(stderr, "Error during redis read: %s\n", err_buff);
                }

                freeReplyObject(reply);
            }
        }
    }
}

void start_seed_thread(struct forwarding_rule *rule, int cache_map_fd, uint32_t redis_addr, uint16_t redis_port) {
    struct forwarding_rule *rule_copy = malloc(sizeof(struct forwarding_rule));
    memcpy(rule_copy, rule, sizeof(struct forwarding_rule));
    struct seed_arg* params = malloc(sizeof(struct seed_arg));
    params->rule = rule_copy;
    params->cache_map_fd = cache_map_fd;
    params->redis_addr = redis_addr;
    params->redis_port = redis_port;

    pthread_t new_thread;
    xassert(pthread_create(&new_thread, NULL, seed_cache, (void *)params) == 0);
}

void start_cache_seeding(int cache_map_fd, struct forwarding_rule **rules, uint32_t redis_addr, uint16_t redis_port) {
    struct forwarding_rule *rule;
    int idx = 0;
    while ((rule = rules[idx++]) != NULL) {
        if (rule->a2s_info_cache) {
            start_seed_thread(rule, cache_map_fd, redis_addr, redis_port);
        }
    }
}