#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ip.h"
#include "config.h"
#include "util.h"
#include "log.h"

typedef struct
{
    uint32_t network;
    int netlength;
    int interface;
} ForwardingEntry;

ForwardingEntry forwarding_table[128];
int forwarding_table_size = 0;

uint8_t active_interfaces = 1;

int forwardTableLookup(packet *packet, int interface)
{
    int i = 0;
    int best_match_interface = -1;
    int best_netlength = -1;
    while (i < forwarding_table_size)
    {
        uint32_t mask = 0xffffffff << (32 - forwarding_table[i].netlength);
        if (forwarding_table[i].netlength == 0)
        {
            mask = 0;
        }
        // printf("interface: %d\n", forwarding_table[i].interface);
        // printf("netlen: %d\n", forwarding_table[i].netlength);
        // printf("mask  : %x\n", mask);
        // printf("entry : %x\n", (forwarding_table[i].network & mask));
        // printf("packet: %x\n", (packet->hdr->dstipaddr & mask));
        if ((forwarding_table[i].netlength > best_netlength) && ((forwarding_table[i].network & mask) == (packet->hdr->dstipaddr & mask)))
        {
            best_netlength = forwarding_table[i].netlength;
            best_match_interface = forwarding_table[i].interface;
        }
        i++;
    }
    // logLog("packet", "found best interface: %d", best_match_interface);
    if (best_match_interface == interface)
    {
        return -1;
    }
    return best_match_interface;
}

/*
 * Add a forwarding entry to your forwarding table.  The network address is
 * given by the network and netlength arguments.  Datagrams destined for
 * this network are to be sent out on the indicated interface.
 */
void addForwardEntry(uint32_t network, int netlength, int interface)
{
    // TODO: Implement this
    if (!(active_interfaces & 1 << interface))
    {
        logLog("packet", "invalid interface");
        return;
    }
    forwarding_table[forwarding_table_size].network = network;
    forwarding_table[forwarding_table_size].netlength = netlength;
    forwarding_table[forwarding_table_size].interface = interface;
    forwarding_table_size++;
    // logLog(
    //     "packet", "Added forwarding entry: network %d.%d.%d.%d, netlength %d, interface %d",
    //     (network & 0xFF000000) >> 24,
    //     (network & 0x00FF0000) >> 16,
    //     (network & 0x0000FF00) >> 8,
    //     (network & 0x000000FF),
    //     netlength,
    //     interface
    // );
}

/*
 * Add an interface to your router.
 */
void addInterface(int interface)
{
    // TODO: Implement this
    active_interfaces = active_interfaces | 1 << interface;
    logLog("packet", "added interface %d, %x", interface, active_interfaces);
}

void run()
{
    int port = getDefaultPort();
    int fd = udp_open(port);
    // TODO: Implement this
    while (1)
    {
        packet *buffer = malloc(sizeof(packet));
        int *origin_interface = malloc(sizeof(int));
        readpkt(fd, buffer, origin_interface);

        buffer->hdr = (ipheader *)&(buffer->data);
        buffer->hdr->ttl--;
        buffer->hdr->checksum++;
        int forwarding_interface = forwardTableLookup(buffer, *origin_interface);
        if (forwarding_interface == -1)
        {
            printf("dropping packet, forwarding to same interface\n");
            continue;
        }
        if (buffer->hdr->ttl < 1)
        {
            printf("dropping packet, no more hops left\n");
            continue;
        }
        if (ipchecksum(buffer, sizeof(ipheader)) != 0)
        {
            printf("dropping packet, failed checkum\n");
            continue;
        }
        printf("sending packet to %d\n", forwarding_interface);
        sendpkt(fd, forwarding_interface, buffer);
        free(buffer);
    }
}

int main(int argc, char **argv)
{
    logConfig("router", "packet,error,failure");

    char *configFileName = "router.config";
    if (argc > 1)
    {
        configFileName = argv[1];
    }
    configLoad(configFileName, addForwardEntry, addInterface);
    run();
    return 0;
}
