#include "ipc.h"
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <scenario.h>
#include <sys/msg.h>
int main(int argc, char **argv) {

    msg_hello_t hello_msg;
    hello_msg.mtype = MSGTYPE_HELLO;
    hello_msg.has_strategy = TR_TRUE;
    hello_msg.strategy = STRATEGY_PROFIT;
    hello_msg.pid = getpid();
    hello_msg.studentid_n = 3;
    strncpy(hello_msg.studentids[0], "VR517101", STUDENTID_MAXLEN - 1);
    strncpy(hello_msg.studentids[1], "VR517631", STUDENTID_MAXLEN - 1); 
    strncpy(hello_msg.studentids[2], "VR516245", STUDENTID_MAXLEN - 1);
    int key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    int msqid = msgget(key, 0); 
    if(msgsnd(msqid, &hello_msg, sizeof(hello_msg) - sizeof(long), 0) == -1) printf("gianni");
    return 0;
}
