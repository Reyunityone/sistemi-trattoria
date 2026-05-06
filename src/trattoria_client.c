#include "ipc.h"
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scenario.h>
#include <sys/msg.h>
#include <pthread.h>

char* SKILLS_TO_STRING[] = {"waiter", "cook", "helper", "cashier"};
char* TRAITS_TO_STRING[] = {"patience", "sociability", "professionality", "resistance"};
strategy_t STRATEGY;
void print_waiters(msg_welcome_t welcome){
    printf("\n");
    for(int i = 0; i < welcome.staff_n; i++){
        printf("%s statistiche: \n", welcome.staff[i].name);
        for(int j = 0; j < NUM_SKILLS; j++){
            char* current_skill = SKILLS_TO_STRING[j];
            char* current_trait = TRAITS_TO_STRING[j];
            printf("\t%s: %i %s: %i\n",current_skill,welcome.staff[i].skills[j],current_trait, welcome.staff[i].traits[j]);
        }
    }
}

void update_role(staff_member_t membro){
    if(STRATEGY == STRATEGY_PROFIT){

    }
}

void *worker(void* arg){
    staff_member_t membro = *(staff_member_t*) arg;
    while(1){
        update_role(membro);
        sleep(2);
    }
    return 0;
}

int receive_welcome(msg_welcome_t* welcome){
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, S_IRUSR);
    return msgrcv(msqid, welcome, sizeof(*welcome) - sizeof(long), MSGTYPE_WELCOME, 0);
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
            if(strncmp(strategy, "profit", strlen("profit")) == 0){
                STRATEGY = STRATEGY_PROFIT;
            }
            else if(strncmp(strategy, "reputation", strlen("reputation")) == 0){
                STRATEGY = STRATEGY_REPUTATION;
            }
            begin_handshake(STRATEGY);
        }
    }
    else begin_handshake_nostrategy();
    
    
    msg_welcome_t welcome;
    receive_welcome(&welcome); 
    print_waiters(welcome);

    pthread_t worker_threads[MAX_STAFF];
    for(int i = 0; i < welcome.staff_n; i++){
      pthread_create(&worker_threads[i],NULL , worker, &welcome.staff[i]);
    }
   
    for(int i = 0; i < welcome.staff_n; i++){
       pthread_join(worker_threads[i], NULL);
    }
}
