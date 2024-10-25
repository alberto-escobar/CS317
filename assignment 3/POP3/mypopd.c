#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"
#include "util.h"
//x3
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024

typedef enum state {
    Undefined,
    // TODO: Add additional states as necessary
    Authorization,
    Transaction,
    Update
} State;

typedef struct serverstate {
    int fd;
    net_buffer_t nb;
    char recvbuf[MAX_LINE_LENGTH + 1];
    char *words[MAX_LINE_LENGTH];
    int nwords;
    State state;
    struct utsname my_uname;
    // TODO: Add additional fields as necessary
    char username[MAX_LINE_LENGTH]; 
    mail_list_t mail_list;
} serverstate;

static void handle_client(void *new_fd);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }
    run_server(argv[1], handle_client);
    return 0;
}

// syntax_error returns
//   -1 if the server should exit
//    1 otherwise
int syntax_error(serverstate *ss) {
    if (send_formatted(ss->fd, "-ERR %s\r\n", "Syntax error in parameters or arguments") <= 0) return -1;
    return 1;
}

// checkstate returns
//   -1 if the server should exit
//    0 if the server is in the appropriate state
//    1 if the server is not in the appropriate state
int checkstate(serverstate *ss, State s) {
    if (ss->state != s) {
        if (send_formatted(ss->fd, "-ERR %s\r\n", "Bad sequence of commands") <= 0) return -1;
        return 1;
    }
    return 0;
}

// All the functions that implement a single command return
//   -1 if the server should exit
//    0 if the command was successful
//    1 if the command was unsuccessful

int do_quit(serverstate *ss) {
    // Note: This method has been filled in intentionally!
    dlog("Executing quit\n");
    send_formatted(ss->fd, "+OK Service closing transmission channel\r\n");
    ss->state = Undefined;
    return -1;
}

int do_user(serverstate *ss) {
    dlog("Executing user\n");
    // TODO: Implement this function
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Invalid arguments\r\n");
        return 1;
    }

    char *input_email = ss->words[1];
    if (is_valid_user(input_email, NULL)){
        strncpy(ss->username, input_email, sizeof(ss->username));
        send_formatted(ss->fd, "+OK User is valid, proceed with password\r\n");
        return 0;
    };
    send_formatted(ss->fd, "-ERR No such user\r\n");
    return 1;
}

int do_pass(serverstate *ss) {
    dlog("Executing pass\n");
    // TODO: Implement this function
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Invalid arguments\r\n");
        return 1; 
    }
    char *input_password = ss->words[1];
    if (is_valid_user(ss->username, input_password)) {
        send_formatted(ss->fd, "+OK Password is valid, mail loaded\r\n");
        ss->mail_list = load_user_mail(ss->username);
        ss->state = Transaction;
        return 0;
    }
    send_formatted(ss->fd, "-ERR Invalid password\r\n");
    return 1;
}

int do_stat(serverstate *ss) {
    dlog("Executing stat\n");
    // TODO: Implement this function
    if (ss->nwords > 1) {
        send_formatted(ss->fd, "-ERR Invalid arguments\r\n");
    }
    int num_of_mails = mail_list_length(ss->mail_list, 0);
    size_t size_of_mail_list = mail_list_size(ss->mail_list);
    send_formatted(ss->fd, "+OK %d %zu\r\n", num_of_mails, size_of_mail_list);
    return 0;
}

int do_list(serverstate *ss) {
    dlog("Executing list\n");
    // TODO: Implement this function
    int num_mails = mail_list_length(ss->mail_list, 0);
    int num_total_mails = mail_list_length(ss->mail_list, 1);
    if (!ss->mail_list) {
        send_formatted(ss->fd, "+OK 0 messages\r\n.\r\n");
        return 0;
    }
    if (ss->nwords == 1) {
        send_formatted(ss->fd, "+OK %d messages\r\n", num_mails);
        for (int i = 0; i < num_total_mails; i++) {
            mail_item_t item = mail_list_retrieve(ss->mail_list, i);
            if (item) {
                send_formatted(ss->fd, "%d %zu\r\n", i + 1, mail_item_size(item));
            }
        }
        send_formatted(ss->fd, ".\r\n");
        return 0;
    } else if (ss->nwords == 2) {
        int msg_num = atoi(ss->words[1]);
        if (msg_num <= 0) {
            send_formatted(ss->fd, "-ERR Invalid message number\r\n");
            return 1;
        }
        mail_item_t item = mail_list_retrieve(ss->mail_list, msg_num - 1);
        if (item != NULL) {
            send_formatted(ss->fd, "+OK %d %zu\r\n", msg_num, mail_item_size(item));
            return 0;
        } else {
            send_formatted(ss->fd, "-ERR no such message, only %d messages in maildrop\r\n", num_mails);
            return 1;
        }
    } else {
        send_formatted(ss->fd, "-ERR Invalid arguments\r\n");
        return 1;
    }
    
}

int do_retr(serverstate *ss) {
    dlog("Executing retr\n");
    // TODO: Implement this function
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Invalid commands\r\n");
        return 1;
    }

    int msg_num = atoi(ss->words[1]);
    if (msg_num <= 0) {
        send_formatted(ss->fd, "-ERR Invalid message number\r\n");
        return 1;
    }
    mail_item_t item = mail_list_retrieve(ss->mail_list, msg_num - 1);
    if (!item) {
        send_formatted(ss->fd, "-ERR no such message\r\n");
        return 1;
    }

    FILE *file = mail_item_contents(item);
    send_formatted(ss->fd, "+OK message follows\r\n");
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '.') {
            send_formatted(ss->fd, ".%s", line);
        } else {
            send_formatted(ss->fd, "%s", line);
        }
    }

    fclose(file);

    send_formatted(ss->fd, ".\r\n");

    return 0;
}

int do_rset(serverstate *ss) {
    dlog("Executing rset\n");
    // TODO: Implement this function
    int recovered_messages = mail_list_undelete(ss->mail_list);
    send_formatted(ss->fd, "+OK %d message(s) restored\r\n", recovered_messages);  
    return 0;
}

int do_noop(serverstate *ss) {
    dlog("Executing noop\n");
    // TODO: Implement this function
    send_formatted(ss->fd, "+OK\r\n"); 
    return 0;
}

int do_dele(serverstate *ss) {
    dlog("Executing dele\n");
    // TODO: Implement this function
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Invalid arguments\r\n");
        return 1; 
    }
    int msg_num = atoi(ss->words[1]);
    if (msg_num <= 0) {
        send_formatted(ss->fd, "-ERR Invalid message number\r\n");
        return 1;
    }
    mail_item_t item = mail_list_retrieve(ss->mail_list, msg_num - 1);
    if (!item) {
        send_formatted(ss->fd, "-ERR no such message\r\n");
        return 1;
    }

    mail_item_delete(item);
    send_formatted(ss->fd, "+OK Message deleted\r\n");
    
    return 0;
}

void handle_client(void *new_fd) {
    int fd = *(int *)(new_fd);

    size_t len;
    serverstate mstate, *ss = &mstate;

    ss->fd = fd;
    ss->nb = nb_create(fd, MAX_LINE_LENGTH);
    ss->state = Undefined;
    uname(&ss->my_uname);
    // TODO: Initialize additional fields in `serverstate`, if any
    if (send_formatted(fd, "+OK POP3 Server on %s ready\r\n", ss->my_uname.nodename) <= 0) return;
    ss->state = Authorization;
    while ((len = nb_read_line(ss->nb, ss->recvbuf)) >= 0) {
        if (ss->recvbuf[len - 1] != '\n') {
        send_formatted(ss->fd, "-ERR Syntax error, command too long\r\n");
        break;
        }

        while (isspace(ss->recvbuf[len - 1])) ss->recvbuf[--len] = 0;
        dlog("%x: Command is %s\n", fd, ss->recvbuf);

        ss->nwords = split(ss->recvbuf, ss->words);
        char *command = ss->words[0];

        /* TODO: Handle the different values of `command` and dispatch it to the correct implementation
         *  TOP, UIDL, APOP commands do not need to be implemented and therefore may return an error response */
        if (ss->state == Authorization) {
            if (strcasecmp(command, "QUIT") == 0) {
                if (do_quit(ss) == -1) break;
            } else if (strcasecmp(command, "USER") == 0) {
                if (do_user(ss) == -1) break;
            } else if (strcasecmp(command, "PASS") == 0) {
                if (do_pass(ss) == -1) break;
            } else {
                send_formatted(fd, "-ERR Bad sequence of commands\r\n");
            }
        } else if (ss->state == Transaction) {
            // Handle other POP3 commands 
            if (strcasecmp(command, "STAT") == 0) {
                if (do_stat(ss) == -1) break;
            } else if (strcasecmp(command, "LIST") == 0) {
                if (do_list(ss) == -1) break;
            } else if (strcasecmp(command, "RETR") == 0) {
                if (do_retr(ss) == -1) break;
            } else if (strcasecmp(command, "DELE") == 0) {
                if (do_dele(ss) == -1) break;
            } else if (strcasecmp(command, "RSET") == 0) {
                if (do_rset(ss) == -1) break;
            } else if (strcasecmp(command, "NOOP") == 0) {
                if (do_noop(ss) == -1) break;
            } else if (strcasecmp(command, "QUIT") == 0) {
                if (do_quit(ss) == -1){
                    ss->state = Update;
                    int deleted_number = mail_list_destroy(ss->mail_list);
                    dlog(" %d errors deleting messages", deleted_number);
                    ss->state = Undefined;
                    
                    break;
                }
            } else {
                send_formatted(fd, "-ERR Bad sequence of commands\r\n");
            }
        }
    }
    // TODO: Clean up fields in `serverstate`, if required
    nb_destroy(ss->nb);
    close(fd);
    //free(new_fd);
}

