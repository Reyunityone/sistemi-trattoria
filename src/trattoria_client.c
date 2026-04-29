#include "ipc.h"
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scenario.h>
#include <sys/msg.h>

char* SKILLS_TO_STRING[] = {"waiter", "cook", "helper", "cashier"};

void print_waiters(msg_welcome_t welcome){
    printf("\n");
    for(int i = 0; i < welcome.staff_n; i++){
        printf("%s statistiche: \n", welcome.staff[i].name);
        for(int j = 0; j < NUM_SKILLS; j++){
            char* current_skill = SKILLS_TO_STRING[j];
            printf("\t%s: %i\n",current_skill,welcome.staff[i].skills[j]);
        }
    }
}

int begin_handshake(strategy_t strategy){
    char* strategy_string;
    if(strategy == STRATEGY_REPUTATION) strategy_string = "reputation";
    if(strategy == STRATEGY_PROFIT) strategy_string = "profit";
    printf("Sending hello with %s strategy", strategy_string);
    msg_hello_t hello_msg;
    hello_msg.mtype = MSGTYPE_HELLO;
    hello_msg.has_strategy = TR_TRUE;
    hello_msg.strategy = strategy;
    hello_msg.pid = getpid();
    hello_msg.studentid_n = 3;
    strncpy(hello_msg.studentids[0], "VR517101", STUDENTID_MAXLEN - 1);
    strncpy(hello_msg.studentids[1], "VR517631", STUDENTID_MAXLEN - 1); 
    strncpy(hello_msg.studentids[2], "VR516245", STUDENTID_MAXLEN - 1);
    int key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    int msqid = msgget(key, S_IWUSR | S_IRUSR); 
    return msgsnd(msqid, &hello_msg, sizeof(hello_msg) - sizeof(long), 0);
}


int begin_handshake_nostrategy(){
    msg_hello_t hello_msg;
    hello_msg.mtype = MSGTYPE_HELLO;
    hello_msg.has_strategy = TR_FALSE;
    hello_msg.pid = getpid();
    hello_msg.studentid_n = 3;
    strncpy(hello_msg.studentids[0], "VR517101", STUDENTID_MAXLEN - 1);
    strncpy(hello_msg.studentids[1], "VR517631", STUDENTID_MAXLEN - 1); 
    strncpy(hello_msg.studentids[2], "VR516245", STUDENTID_MAXLEN - 1);
    int key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    int msqid = msgget(key, 0); 
    return msgsnd(msqid, &hello_msg, sizeof(hello_msg) - sizeof(long), 0);
}

int main(int argc, char **argv) {
    char* strategy;
    if(argc > 2){
        for(int i=1; i < argc; i++){
            if(strncmp(argv[i], "--strategy", strlen("--strategy")) == 0
            && (i+1) < argc){
                strategy = argv[i+1];
            }
        }
        if(strategy != NULL){
            if(strncmp(strategy, "profit", strlen("profit"))) begin_handshake(STRATEGY_PROFIT);
            if(strncmp(strategy, "reputation", strlen("reputation"))) begin_handshake(STRATEGY_REPUTATION);
        }
    }
    else begin_handshake_nostrategy();
    
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, S_IRUSR);
    msg_welcome_t welcome;
    if(msgrcv(msqid, &welcome, sizeof(welcome) - sizeof(long), MSGTYPE_WELCOME, 0) == -1) return -1;

    print_waiters(welcome);
    
}
