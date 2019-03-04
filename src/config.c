#include "config.h"
#include "compressor.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

struct service_def *parse_service(const char *service) {
    char buffer[128];
    strcpy(buffer, service);
    struct service_def *ret;

    char *port = strtok(buffer, "/");
    char *proto = strtok(NULL, "/");
    if (port == NULL || proto == NULL) {
        fprintf(stderr, "Error parsing service definition: %s\n", service);
        return NULL;
    }

    uint16_t iport = atoi(port);
    if (iport == 0) {
        fprintf(stderr, "Invalid port defined for service %s\n", service);
        return NULL;
    }

    if (strcmp(proto, "tcp") == 0) {
        struct service_def *def = calloc(1, sizeof(struct service_def));
        def->port = iport;
        def->proto = PROTO_TCP;
        
        return def;
    } else if (strcmp(proto, "udp") == 0) {
        struct service_def *def = calloc(1, sizeof(struct service_def));
        def->port = iport;
        def->proto = PROTO_UDP;

        return def;
    } else {
        fprintf(stderr, "Invalid protocol defined for service %s\n", service);
        return NULL;
    }
}

struct forwarding_rule *parse_forwarding_rule(config_setting_t *cfg_rule) {
    const char *bindstr;
    const char *deststr;

    int get_bindaddr = config_setting_lookup_string(cfg_rule, "bind", &bindstr);
    int get_destaddr = config_setting_lookup_string(cfg_rule, "dest", &deststr);
    if (!get_bindaddr || !get_destaddr) {
        fprintf(stderr, "Error parsing forwarding rule\n");
        return NULL;
    }

    char bindbuffer[32];
    strcpy(bindbuffer, bindstr);
    char *bindaddr = strtok(bindbuffer, ":");
    char *bindport = strtok(NULL, ":");
    if (!bindaddr || !bindport) {
        fprintf(stderr, "Error parsing bind address %s\n", bindstr);
        return NULL;
    }

    char destbuffer[32];
    strcpy(destbuffer, deststr);
    char *destaddr = strtok(destbuffer, ":");
    char *destport = strtok(NULL, ":");
    if (!destaddr || !destport) {
        fprintf(stderr, "Error parsing bind address %s\n", deststr);
        return NULL;
    }

    struct in_addr bind_inet;
    if (!inet_aton(bindaddr, &bind_inet)) {
        fprintf(stderr, "Error parsing ip address %s\n", bindaddr);
        return NULL;
    }
    uint16_t bind_port = atoi(bindport);
    if (!bind_port) {
        fprintf(stderr, "Error parsing port %s\n", bindport);
        return NULL;
    }

    struct in_addr dest_inet;
    if (!inet_aton(destaddr, &dest_inet)) {
        fprintf(stderr, "Error parsing ip address %s\n", destaddr);
        return NULL;
    }
    uint16_t dest_port = atoi(destport);
    if (!dest_port) {
        fprintf(stderr, "Error parsing port %s\n", destport);
        return NULL;
    }

    struct forwarding_rule *rule = malloc(sizeof(struct forwarding_rule));
    rule->bind_addr = bind_inet.s_addr;
    rule->bind_port = bind_port;
    rule->source_addr = bind_inet.s_addr;
    rule->source_port = bind_port;
    rule->to_addr = dest_inet.s_addr;
    rule->to_port = dest_port;

    return rule;
}
