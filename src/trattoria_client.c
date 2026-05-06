#include "ipc.h"
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>
#include <scenario.h>
#include <sys/msg.h>
#include <pthread.h>

typedef struct {
    staff_member_t membro;
    int id;
} thread_args;

shm_blackboard_t* shm_blackboard;
shm_cashdesk_t* shm_cashdesk;
shm_diningroom_t* shm_dining_room;
shm_kitchen_t* shm_kitchen;

struct sembuf lock[1];
struct sembuf unlock[1];

int sem_group;

char* SKILLS_TO_STRING[] = {"waiter", "cook", "helper", "cashier"};
char* TRAITS_TO_STRING[] = {"patience", "sociability", "professionality", "resistance"};

strategy_t STRATEGY;

void init_all_shm(){
    key_t key_blackboard = ftok(TRATTORIA_FTOK_PATH, PROJ_BLACKBOARD);
    key_t key_cashdesk = ftok(TRATTORIA_FTOK_PATH, PROJ_CASHDESK);
    key_t key_dining_room = ftok(TRATTORIA_FTOK_PATH, PROJ_DININGROOM);
    key_t key_kitchen = ftok(TRATTORIA_FTOK_PATH, PROJ_KITCHEN);

    int blackboard_id = shmget(key_blackboard, 0, S_IRUSR | S_IWUSR);
    int cashdesk_id = shmget(key_cashdesk, 0, S_IRUSR);
    int dining_room_id = shmget(key_dining_room, 0, S_IRUSR);
    int kitchen_id = shmget(key_kitchen, 0, S_IRUSR);

    shm_blackboard = shmat(blackboard_id, NULL, 0);
    shm_cashdesk = shmat(cashdesk_id, NULL, SHM_RDONLY);
    shm_dining_room = shmat(dining_room_id, NULL, SHM_RDONLY);
    shm_kitchen = shmat(kitchen_id, NULL, SHM_RDONLY);
}

void init_semaphores(){
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_SEM);
    sem_group = semget(key, SEM_NSEMS, S_IRUSR | S_IWUSR);
    lock[0].sem_num = SEMIDX_BLACKBOARD;
    lock[0].sem_op = -1;
    lock[0].sem_flg = 0;
    unlock[0].sem_num = SEMIDX_BLACKBOARD;
    unlock[0].sem_op = 1;
    unlock[0].sem_flg = 0;
}

void lock_blackboard(){
    semop(sem_group,lock, 1);
}

void unlock_blackboard(){
    semop(sem_group,unlock, 1);
}

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

void update_role(staff_member_t membro, int member_id){
    if(STRATEGY == STRATEGY_PROFIT){
        lock_blackboard(); 
        if(member_id == 1){
           for(int i = 0; i < shm_dining_room->tables_n; i++){
            if(shm_dining_room->tables[i].state == TABLE_TAKEN) shm_blackboard->tables[i].waiter = member_id - 1;
           } 
        }
        else if(member_id == 2){
            shm_blackboard->cook = member_id - 1;
        }
        else if(member_id == 3){
            shm_blackboard->cashier = member_id - 1;
        }
        else if(member_id == 4){
           for(int i = 0; i < shm_dining_room->tables_n; i++){
            if(shm_dining_room->tables[i].state == TABLE_FREED) shm_blackboard->tables[i].cleaner = member_id - 1;
           } 
        }
        unlock_blackboard();
    }
}

void *worker(void* arg){
    thread_args casted_args = *(thread_args*) arg;
    staff_member_t membro = casted_args.membro;
    int member_id = casted_args.id;
    while(1){
        msg_fatigue_t fatigue;
        update_role(membro, member_id);
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
    //INIT
    init_all_shm();
    init_semaphores();
    //END INIT

    //Strategy parsing
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
    
    //Receive staff stats from server and print them in user friendly view for debugging
    msg_welcome_t welcome;
    receive_welcome(&welcome); 
    print_waiters(welcome);

    //MAIN EXECUTION
    pthread_t worker_threads[MAX_STAFF];
    thread_args worker_args[MAX_STAFF];
    for(int i = 0; i < welcome.staff_n; i++){
      worker_args[i].membro = welcome.staff[i];
      worker_args[i].id = i+1;
      pthread_create(&worker_threads[i],NULL , worker, &worker_args[i]);
    }
   
    //END EXECUTION: TODO
    for(int i = 0; i < welcome.staff_n; i++){
       pthread_join(worker_threads[i], NULL);
    }
}
