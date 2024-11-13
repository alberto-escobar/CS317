/************************************************************************
 * Adapted from a course at Boston University for use in CPSC 317 at UBC
 *
 *
 * The interfaces for the STCP sender (you get to implement them), and a
 * simple application-level routine to drive the sender.
 *
 * This routine reads the data to be transferred over the connection
 * from a file specified and invokes the STCP send functionality to
 * deliver the packets as an ordered sequence of datagrams.
 *
 * Version 2.0
 *
 *
 *************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/file.h>

#include "stcp.h"

#define STCP_SUCCESS 1
#define STCP_ERROR -1

int debug = 1;

typedef struct window_node
{
    packet *packet;
    struct window_node *next;
} window_node_t;

typedef struct
{
    /* YOUR CODE HERE */
    unsigned char state;
    unsigned int current_sequence_Number;
    unsigned int current_acknowledgement_number;
    unsigned int last_acknowledgement_number;
    unsigned char repeat_acknowledgements;
    unsigned short number_of_bytes_in_flight;
    unsigned short maximum_window_size;
    window_node_t *window_head;
    window_node_t *window_tail;
    int fd;

} stcp_send_ctrl_blk;
/* ADD ANY EXTRA FUNCTIONS HERE */

void destroy_stcp_send_ctrl_blk(stcp_send_ctrl_blk *cb)
{
    free(cb);
    return;
}

void initializePacket(packet *pkt, int len)
{
    pkt->len = len;
    pkt->hdr = (tcpheader *)pkt->data;
}
void createPacket(packet *pkt, int flags, unsigned short rwnd, unsigned int seq, unsigned int ack, unsigned char *data, int len)
{
    initializePacket(pkt, len + sizeof(tcpheader));
    tcpheader *hdr = pkt->hdr;
    hdr->srcPort = 0;
    hdr->dstPort = 0;
    hdr->seqNo = seq;
    hdr->ackNo = ack;
    hdr->windowSize = rwnd;
    hdr->flags = (5 << 12) | flags;
    hdr->checksum = 0;
    if (data != NULL)
        memcpy(sizeof(tcpheader) + pkt->data, data, len);
    htonHdr(hdr);
    hdr->checksum = ipchecksum(pkt->data, pkt->len);
}
void createReceivingPacket(packet *packet, unsigned char *data, int len)
{
    initPacket(packet, data, len);
    ntohHdr(packet->hdr);
}

void sendSYN(stcp_send_ctrl_blk *cb)
{
    // create SYN packet
    packet *syn_packet = malloc(sizeof(packet));
    createPacket(syn_packet, SYN, 0, cb->current_sequence_Number, cb->current_acknowledgement_number, NULL, 0);
    if (debug)
        logLog("init", "Created SYN packet:");

    // Send SYN packet
    send(cb->fd, syn_packet, syn_packet->len, 0);
    free(syn_packet);

    if (debug)
        logLog("init", "Sent SYN packet");
}

void moveWindow(stcp_send_ctrl_blk *cb, int recieved_acknowledgement_number)
{
    while (cb->window_head != NULL)
    {
        window_node_t *temp_node = cb->window_head->next;
        ntohHdr(cb->window_head->packet->hdr);
        if (recieved_acknowledgement_number == cb->window_head->packet->hdr->seqNo)
        {
            htonHdr(cb->window_head->packet->hdr);
            break;
        }

        cb->number_of_bytes_in_flight -= cb->window_head->packet->len;
        free(cb->window_head->packet);
        free(cb->window_head);
        cb->window_head = temp_node;
    }
}
void retransmitWindow(stcp_send_ctrl_blk *cb)
{
    int number_of_bytes_read = 0;
    unsigned char buffer[STCP_MTU];

    // Process the buffer until it is empty
    while (number_of_bytes_read > 0)
    {
        number_of_bytes_read = readWithTimeout(cb->fd, buffer, 0);

        if (number_of_bytes_read <= 0)
        {
            break;
        }

        packet ack_data_packet;
        createReceivingPacket(&ack_data_packet, buffer, number_of_bytes_read);

        if (ipchecksum(buffer, number_of_bytes_read) != 0)
        {
            logLog("init", "Checksum did not return 0. Ignoring ACK packet.");
            number_of_bytes_read = 0;
            continue;
        }

        logLog("init", "Received %d bytes from receiver.", number_of_bytes_read);

        ntohHdr(ack_data_packet.hdr);
        unsigned int recieved_acknowledgement_number = ack_data_packet.hdr->ackNo;
        logLog("init", "Received a ACK packet: %d", recieved_acknowledgement_number);

        cb->maximum_window_size = min(ack_data_packet.hdr->windowSize, STCP_MAXWIN);

        moveWindow(cb, recieved_acknowledgement_number);
        return;
    }

    // Find the data packet with the specified sequence no and resend it
    packet *packet_to_resend = NULL;
    window_node_t *temp_node = cb->window_head;
    while (temp_node != NULL)
    {
        ntohHdr(temp_node->packet->hdr);
        if (cb->last_acknowledgement_number == temp_node->packet->hdr->seqNo)
        {
            htonHdr(temp_node->packet->hdr);
            packet_to_resend = temp_node->packet;
            break;
        }
        temp_node = temp_node->next;
    }
    if (packet_to_resend == NULL)
        return;

    send(cb->fd, packet_to_resend, packet_to_resend->len, MSG_NOSIGNAL);

    number_of_bytes_read = 0;
    int timeout = STCP_INITIAL_TIMEOUT * 2;

    // Wait for ACK for the resent data
    while (number_of_bytes_read <= 0)
    {
        number_of_bytes_read = readWithTimeout(cb->fd, buffer, timeout);

        if (number_of_bytes_read == STCP_READ_TIMED_OUT)
        {
            send(cb->fd, cb->window_head->packet->data, cb->window_head->packet->len, MSG_NOSIGNAL);
            timeout = min(timeout * 2, STCP_MAX_TIMEOUT);
        }

        else if (number_of_bytes_read == STCP_READ_PERMANENT_FAILURE)
            return;

        if (ipchecksum(buffer, number_of_bytes_read) != 0)
        {
            logLog("init", "Checksum did not return 0. Ignoring ACK packet.");
            number_of_bytes_read = 0;
            continue;
        }

        logLog("init", "Received %d bytes from receiver.", number_of_bytes_read);

        packet ack_data_packet;
        createReceivingPacket(&ack_data_packet, buffer, number_of_bytes_read);
        unsigned int recieved_acknowledgement_number = ack_data_packet.hdr->ackNo;
        logLog("init", "Received an ACK packet: %d", recieved_acknowledgement_number);

        cb->maximum_window_size = min(ack_data_packet.hdr->windowSize, STCP_MAXWIN);
        moveWindow(cb, recieved_acknowledgement_number);

        if (recieved_acknowledgement_number == cb->last_acknowledgement_number)
            return;
        else
        {
            cb->last_acknowledgement_number = recieved_acknowledgement_number;
            cb->repeat_acknowledgements = 1;
            return;
        }
    }
    return;
}
void appendPacketToWindow(stcp_send_ctrl_blk *cb, packet *packet)
{
    if (cb->window_head == NULL)
    {
        cb->window_head = malloc(sizeof(window_node_t));
        cb->window_head->packet = packet;
        cb->window_head->next = NULL;
        cb->window_tail = cb->window_head;
    }
    else
    {
        cb->window_tail->next = malloc(sizeof(window_node_t));
        cb->window_tail = cb->window_tail->next;
        cb->window_tail->packet = packet;
        cb->window_tail->next = NULL;
    }
}

/*
 * Send STCP. This routine is to send all the data (len bytes).  If more
 * than MSS bytes are to be sent, the routine breaks the data into multiple
 * packets. It will keep sending data until the send window is full or all
 * the data has been sent. At which point it reads data from the network to,
 * hopefully, get the ACKs that open the window. You will need to be careful
 * about timing your packets and dealing with the last piece of data.
 *
 * Your sender program will spend almost all of its time in either this
 * function or in tcp_close().  All input processing (you can use the
 * function readWithTimeout() defined in stcp.c to receive segments) is done
 * as a side effect of the work of this function (and stcp_close()).
 *
 * The function returns STCP_SUCCESS on success, or STCP_ERROR on error.
 */
int stcp_send(stcp_send_ctrl_blk *stcp_CB, unsigned char *data, int length)
{
    while (stcp_CB->number_of_bytes_in_flight >= stcp_CB->maximum_window_size)
    {
        // wait for ACKs before moving window
        unsigned char buffer[STCP_MTU];
        int number_of_bytes_read = 0;
        int timeout = STCP_INITIAL_TIMEOUT;
        while (number_of_bytes_read <= 0)
        {
            number_of_bytes_read = readWithTimeout(stcp_CB->fd, buffer, timeout);
            // time out case
            if (number_of_bytes_read == STCP_READ_TIMED_OUT)
            {
                send(stcp_CB->fd, stcp_CB->window_head->packet, stcp_CB->window_head->packet->len, MSG_NOSIGNAL);
                timeout = min(timeout * 2, STCP_MAX_TIMEOUT);
            }
            // read failure case
            else if (number_of_bytes_read == STCP_READ_PERMANENT_FAILURE)
            {
                return STCP_ERROR;
            }
            // failed checksum case
            if (ipchecksum(buffer, number_of_bytes_read) != 0)
            {
                logLog("init", "Checksum did not return 0. Ignoring ACK packet.");
                number_of_bytes_read = 0;
                continue;
            }
            logLog("init", "Recieved %d bytes from the receiver.", number_of_bytes_read);
            packet ack_packet;
            createReceivingPacket(&ack_packet, buffer, number_of_bytes_read);
            unsigned int recieved_acknowledgement_number = ack_packet.hdr->ackNo;
            logLog("init", "Received an ACK packet: %d", recieved_acknowledgement_number);
            stcp_CB->maximum_window_size = min(ack_packet.hdr->windowSize, STCP_MAXWIN);

            moveWindow(stcp_CB, recieved_acknowledgement_number);

            if (stcp_CB->last_acknowledgement_number == recieved_acknowledgement_number)
                stcp_CB->repeat_acknowledgements += 1;
            else
                stcp_CB->repeat_acknowledgements = 1;
            if (stcp_CB->repeat_acknowledgements == 3)
                retransmitWindow(stcp_CB);
        }
    }

    if (stcp_CB->maximum_window_size != 0 && length > stcp_CB->maximum_window_size)
    {
        int remaining_length = length;
        int number_of_packets = 0;

        while (remaining_length > 0)
        {
            packet *data_packet = malloc(sizeof(packet));
            createPacket(data_packet, ACK, 0, stcp_CB->current_sequence_Number, stcp_CB->current_acknowledgement_number, data, length);
            appendPacketToWindow(stcp_CB, data_packet);
            stcp_CB->current_sequence_Number += payloadSize(data_packet);

            remaining_length -= min(stcp_CB->maximum_window_size, remaining_length);
            data = data + min(stcp_CB->maximum_window_size, remaining_length);
            number_of_packets++;
        }

        window_node_t *temp = stcp_CB->window_head;

        for (int i = 0; i < number_of_packets; i++)
        {
            if (stcp_CB->number_of_bytes_in_flight + temp->packet->len <= stcp_CB->maximum_window_size)
            {
                send(stcp_CB->fd, temp->packet->data, temp->packet->len, MSG_NOSIGNAL);
                stcp_CB->number_of_bytes_in_flight += payloadSize(temp->packet);
                temp = temp->next;
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        packet *data_packet = malloc(sizeof(packet));
        createPacket(data_packet, ACK, 0, stcp_CB->current_sequence_Number, stcp_CB->current_acknowledgement_number, data, length);
        stcp_CB->number_of_bytes_in_flight += payloadSize(data_packet);
        stcp_CB->current_sequence_Number += payloadSize(data_packet);
        send(stcp_CB->fd, data_packet, data_packet->len, MSG_NOSIGNAL);
        appendPacketToWindow(stcp_CB, data_packet);
    }
    return STCP_SUCCESS;
}
/*
 * Open the sender side of the STCP connection. Returns the pointer to
 * a newly allocated control block containing the basic information
 * about the connection. Returns NULL if an error happened.
 *
 * If you use udp_open() it will use connect() on the UDP socket
 * then all packets then sent and received on the given file
 * descriptor go to and are received from the specified host. Reads
 * and writes are still completed in a datagram unit size, but the
 * application does not have to do the multiplexing and
 * demultiplexing. This greatly simplifies things but restricts the
 * number of "connections" to the number of file descriptors and isn't
 * very good for a pure request response protocol like DNS where there
 * is no long term relationship between the client and server.
 */
stcp_send_ctrl_blk *stcp_open(char *destination, int sendersPort,
                              int receiversPort)
{

    logLog("init", "Sending from port %d to <%s, %d>", sendersPort, destination, receiversPort);
    // Since I am the sender, the destination and receiversPort name the other side
    int fd = udp_open(destination, receiversPort, sendersPort);
    (void)fd;

    /* YOUR CODE HERE */

    // initialize ctrl_blk
    stcp_send_ctrl_blk *cb = malloc(sizeof(stcp_send_ctrl_blk));
    cb->state = STCP_SENDER_CLOSED;
    cb->current_sequence_Number = (unsigned int)rand() % 1000;
    cb->current_acknowledgement_number = cb->current_acknowledgement_number + 1;
    cb->last_acknowledgement_number = 0;
    cb->repeat_acknowledgements = 0;
    cb->number_of_bytes_in_flight = 0;
    cb->maximum_window_size = STCP_MAXWIN;
    cb->fd = fd;
    cb->window_head = NULL;
    cb->window_tail = NULL;

    // create and send SYN packet
    sendSYN(cb);

    // wait for SYN-ACK packet
    unsigned char buffer[STCP_MTU];
    int number_of_bytes_read = 0;
    int timeout = STCP_INITIAL_TIMEOUT;
    while (number_of_bytes_read <= 0)
    {
        number_of_bytes_read = readWithTimeout(cb->fd, buffer, timeout);

        // timeout case
        if (number_of_bytes_read == STCP_READ_TIMED_OUT)
        {
            logLog("init", "Sending another SYN packet to the receiver due to timeout.");
            sendSYN(cb);
            timeout = min(timeout * 2, STCP_MAX_TIMEOUT);
        }
        // read failure case
        else if (number_of_bytes_read == STCP_READ_PERMANENT_FAILURE)
            return NULL;

        // failed checksum case
        if (ipchecksum(buffer, number_of_bytes_read) != 0)
        {
            logLog("init", "Checksum did not return 0. Ignoring SYN-ACK packet.");
            number_of_bytes_read = 0;
            continue;
        }

        packet synack_packet;
        createReceivingPacket(&synack_packet, buffer, number_of_bytes_read);
        if ((synack_packet.hdr->flags == (SYN | ACK)) && synack_packet.hdr->ackNo == cb->current_sequence_Number + 1)
        {
            cb->state = STCP_SENDER_ESTABLISHED;
            logLog("init", "Received SYN-ACK packet, will begin sending.");

            cb->current_acknowledgement_number = synack_packet.hdr->seqNo + 1;
            cb->current_sequence_Number = synack_packet.hdr->ackNo;
            cb->last_acknowledgement_number = synack_packet.hdr->ackNo;
            cb->last_acknowledgement_number = 1;
            cb->maximum_window_size = synack_packet.hdr->windowSize;
            packet ack_packet;
            createPacket(&ack_packet, ACK, 0, cb->current_sequence_Number, cb->current_acknowledgement_number, NULL, 0);
            send(cb->fd, &ack_packet, ack_packet.len, 0);
        }
        else
            logLog("init", "Did not receive the correct SYN-ACK packet, will retry.");
    }

    if (cb == NULL)
        return NULL;
    else
        return cb;
}

/*
 * Make sure all the outstanding data has been transmitted and
 * acknowledged, and then initiate closing the connection. This
 * function is also responsible for freeing and closing all necessary
 * structures that were not previously freed, including the control
 * block itself.
 *
 * Returns STCP_SUCCESS on success or STCP_ERROR on error.
 */
int stcp_close(stcp_send_ctrl_blk *cb)
{
    cb->state = STCP_SENDER_CLOSING;
    logLog("init", "There are still %d bytes in flight. Waiting for ACKs.", cb->number_of_bytes_in_flight);

    while (cb->number_of_bytes_in_flight > 0)
    {
        // wait for ACKs
        unsigned char buffer[STCP_MTU];
        int number_of_bytes_read = 0;
        int timeout = STCP_INITIAL_TIMEOUT;
        while (number_of_bytes_read <= 0)
        {
            number_of_bytes_read = readWithTimeout(cb->fd, buffer, timeout);

            if (number_of_bytes_read == STCP_READ_TIMED_OUT)
            {
                if (cb->window_head != NULL)
                {
                    send(cb->fd, cb->window_head->packet, cb->window_head->packet->len, MSG_NOSIGNAL);
                    timeout = min(timeout * 2, STCP_MAX_TIMEOUT);
                }
                else
                    cb->number_of_bytes_in_flight = 0;
            }

            else if (number_of_bytes_read == STCP_READ_PERMANENT_FAILURE)
                return STCP_ERROR;

            if (ipchecksum(buffer, number_of_bytes_read) != 0 && cb->window_head != NULL)
            {
                logLog("init", "Checksum did not return 0. Ignoring packet.");

                number_of_bytes_read = 0;
                continue;
            }
            packet ack_packet;
            createReceivingPacket(&ack_packet, buffer, number_of_bytes_read);

            unsigned int recieved_acknowledgement_number = ack_packet.hdr->ackNo;

            moveWindow(cb, recieved_acknowledgement_number);

            if (cb->window_head == NULL)
            {
                break;
            }
            logLog("init", "Received an ACK, moving window to %d", recieved_acknowledgement_number);

            if (cb->last_acknowledgement_number == recieved_acknowledgement_number)
                cb->repeat_acknowledgements = cb->repeat_acknowledgements + 1;
            else
                cb->repeat_acknowledgements = 1;
            if (cb->repeat_acknowledgements == 3)
                retransmitWindow(cb);
            cb->last_acknowledgement_number = recieved_acknowledgement_number;
        }
    }

    logLog("init", "Sending the FIN packet.");

    // close connection by sending FIN
    packet fin_packet;
    createPacket(&fin_packet, FIN, 0, cb->current_sequence_Number, cb->current_acknowledgement_number, NULL, 0);
    send(cb->fd, &fin_packet, fin_packet.len, MSG_NOSIGNAL);

    cb->state = STCP_SENDER_FIN_WAIT;

    // Send FIN
    unsigned char buffer[STCP_MTU];
    int number_of_bytes_read = 0;
    int timeout = STCP_INITIAL_TIMEOUT;

    while (number_of_bytes_read <= 0)
    {
        number_of_bytes_read = readWithTimeout(cb->fd, buffer, timeout);

        if (number_of_bytes_read == STCP_READ_TIMED_OUT)
        {
            send(cb->fd, &fin_packet, fin_packet.len, MSG_NOSIGNAL);
            timeout = min(timeout * 2, STCP_MAX_TIMEOUT);
        }

        else if (number_of_bytes_read == STCP_READ_PERMANENT_FAILURE)
            return STCP_SUCCESS;

        if (ipchecksum(buffer, number_of_bytes_read) != 0)
        {
            logLog("init", "Checksum did not return 0. Ignoring FIN-ACK packet.");
            number_of_bytes_read = 0;
            continue;
        }

        packet fin_ack_packet;
        createReceivingPacket(&fin_ack_packet, buffer, number_of_bytes_read);

        if (fin_ack_packet.hdr->flags == (FIN | ACK))
        {
            cb->current_sequence_Number = fin_ack_packet.hdr->ackNo + 1;
            cb->current_acknowledgement_number = fin_ack_packet.hdr->seqNo + 1;

            packet ack_packet;
            createPacket(&ack_packet, ACK, 0, 0, cb->current_acknowledgement_number, NULL, 0);
            logLog("init", "Received the FIN-ACK and will send an ACK");

            send(cb->fd, &ack_packet, ack_packet.len, MSG_NOSIGNAL);

            cb->state = STCP_SENDER_CLOSED;
            break;
        }
    }

    return STCP_SUCCESS;
}
/*
 * Return a port number based on the uid of the caller.  This will
 * with reasonably high probability return a port number different from
 * that chosen for other uses on the undergraduate Linux systems.
 *
 * This port is used if ports are not specified on the command line.
 */
int getDefaultPort()
{
    uid_t uid = getuid();
    int port = (uid % (32768 - 512) * 2) + 1024;
    assert(port >= 1024 && port <= 65535 - 1);
    return port;
}

/*
 * This application is to invoke the send-side functionality.
 */
int main(int argc, char **argv)
{
    stcp_send_ctrl_blk *cb;

    char *destinationHost;
    int receiversPort, sendersPort;
    char *filename = NULL;
    int file;
    /* You might want to change the size of this buffer to test how your
     * code deals with different packet sizes.
     */
    unsigned char buffer[STCP_MSS];
    int num_read_bytes;

    logConfig("sender", "init,segment,error,failure");
    /* Verify that the arguments are right */
    if (argc > 5 || argc == 1)
    {
        fprintf(stderr, "usage: sender DestinationIPAddress/Name receiveDataOnPort sendDataToPort filename\n");
        fprintf(stderr, "or   : sender filename\n");
        exit(1);
    }
    if (argc == 2)
    {
        filename = argv[1];
        argc--;
    }

    // Extract the arguments
    destinationHost = argc > 1 ? argv[1] : "localhost";
    receiversPort = argc > 2 ? atoi(argv[2]) : getDefaultPort();
    sendersPort = argc > 3 ? atoi(argv[3]) : getDefaultPort() + 1;
    if (argc > 4)
        filename = argv[4];

    /* Open file for transfer */
    file = open(filename, O_RDONLY);
    if (file < 0)
    {
        logPerror(filename);
        exit(1);
    }

    /*
     * Open connection to destination.  If stcp_open succeeds the
     * control block should be correctly initialized.
     */
    cb = stcp_open(destinationHost, sendersPort, receiversPort);
    if (cb == NULL)
    {
        /* YOUR CODE HERE */
    }

    /* Start to send data in file via STCP to remote receiver. Chop up
     * the file into pieces as large as max packet size and transmit
     * those pieces.
     */
    while (1)
    {
        num_read_bytes = read(file, buffer, sizeof(buffer));

        /* Break when EOF is reached */
        if (num_read_bytes <= 0)
            break;

        if (stcp_send(cb, buffer, num_read_bytes) == STCP_ERROR)
        {
            /* YOUR CODE HERE */
        }
    }

    /* Close the connection to remote receiver */
    if (stcp_close(cb) == STCP_ERROR)
    {
        /* YOUR CODE HERE */
    }

    return 0;
}
