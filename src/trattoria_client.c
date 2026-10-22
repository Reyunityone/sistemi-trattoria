/*
STAFF
. lo staff viene classificato in base ai punti abilitá, tratti e fatica
. ci sono 4 classifiche, una per ogni abilitá, +1 matrice per la fatica in ogni ruolo
. il personale viene scelto in base alla persona con piú punti all'interno di ogni classifica
. quando arriva la fatica si aggiorna la classifica della fatica, che é intesa come malus nel punteggio, quindi vengon aggiornate le ladder di conseguenza

STRATEGIA PROFITTO:
. valutazione del personale nella classifica con sole ABILITÁ
. se uguali viene scelto chi ha fatigue+ABILITÁ migliori (i meno affaticati per abilitá)
. in caso di paritá viene scelta la persona con tratti peggiori [TRATTI come malus]
. alla fine ordino tutto per fatica, cosí i meno affaticati sono i primi ad essere scelti
. il sistema prioritizza le abilitá in ordine di: CUOCO, CAMERIERE, AIUTANTE, CASSIERE

STRETEGIA REPUTAZIONE
. valutazione del personale nella classifica con soli TRATTI
. in caso di paritá viene scelta la persona con fatigue+ABILITÁ peggiori
. se la fatica é uguale allora scelgo chi ha ABILITÁ peggiori
. il sistema di sostituzione del personale stanco funziona come nella strategia profitto
. tratto RESISTENZA viene ignorato, in quanto sfrutto il personale piú bravo finché resiste, quando arriva la notifica lo sostituisco subito
. il sistema prioritizza le abilitá in ordine come nella strategia profitto

GESTIONE DELLA FATICA:
. il punteggio della fatica viene aggiornato ogni volta che arriva dal server il messaggio di fatica nel ruolo specifico
. la ladder della fatica é una matrice per ruolo, cosí se uno é stanco come cuoco non lo é nelle altre mansioni

THREAD ATTIVI
. nel client girano due gruppi di thread principali:
    > 4 thread per il personale, uno per ogni membro dello staff, gestiscono la fatica
    > 1 thread monitor per i messaggi inviati dal server e aggiornamento variabili globali del client

VALUTAZIONE: 14.75 [per 24 famiglie la fatica in reputazione non va al 100%]
*/

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
#include <stdatomic.h>

//struttura per gli argomenti dei thread
typedef struct {
    staff_member_t membro;
    int id;
} thread_args;

//struttura per gli argomenti delle ladder
typedef struct{
    staff_member_t membro;
    int id;
    int stato;  //0 libero, 1 occupato
} ladder_args;

//memorie condivise
shm_blackboard_t* shm_blackboard;
shm_cashdesk_t* shm_cashdesk;
shm_diningroom_t* shm_dining_room;
shm_kitchen_t* shm_kitchen;

//semafori
struct sembuf lock[1];
struct sembuf unlock[1];
int sem_group;

//stringhe per output
char* SKILLS_TO_STRING[] = {"waiter", "cook", "helper", "cashier"};
char* TRAITS_TO_STRING[] = {"patience", "sociability", "professionality", "resistance"};

//ladder classifiche per lo staff
ladder_args waiter_ladder[MAX_STAFF];
ladder_args cook_ladder[MAX_STAFF];
ladder_args helper_ladder[MAX_STAFF];
ladder_args cashier_ladder[MAX_STAFF];

//matrice ladder della fatica (staffxabilitá), utilizzata come malus nelle classifiche
atomic_int fatigue_ladder[4][4]={0};

//variabile atomica globale per indicare se il server sta ancora andando
atomic_int game_running=1;

//variabile atomica per le strategie mandate dalle istanze del server
atomic_int current_strategy=0;

//inizializzazione delle memorie condivise
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

//inizializzazione dei semafori
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

//lock e unlok della blackboard 
void lock_blackboard(){
    semop(sem_group,lock, 1);
}
void unlock_blackboard(){
    semop(sem_group,unlock, 1);
}

//print dello staff
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

//print di debug per le ladder
void debug_print_ladder(){
    printf("----------STAMPA LADDERS----------\n");

    for(int i=0; i<MAX_STAFF; i++){
        printf("waiter ");
        printf("%d: %s ", i+1, waiter_ladder[i].membro.name);
        printf("\n");
    }
    for(int i=0; i<MAX_STAFF; i++){
        printf("cook ");
        printf("%d: %s ", i+1, cook_ladder[i].membro.name);
        printf("\n");
    }
    for(int i=0; i<MAX_STAFF; i++){
        printf("helper ");
        printf("%d: %s ", i+1, helper_ladder[i].membro.name);
        printf("\n");
    }
    for(int i=0; i<MAX_STAFF; i++){
        printf("cashier ");
        printf("%d: %s ", i+1, cashier_ladder[i].membro.name);
        printf("\n");
    }
}

//print di debug per la stampa della blackboard: vedere chi ha preso i posti
void debug_print_blackboard(){
    printf("----------STAMPA LAVAGNA----------\n");
    printf("%d, %d, %d, %d\n", shm_blackboard->cook, shm_blackboard->tables[0].waiter, shm_blackboard->tables[0].cleaner, shm_blackboard->cashier);
}

//print di debug per vedere le statistiche delle abilitá per ogni staff member
void debug_print_staff_stats(){
    printf("----------STAMPA STATISTICHE STAFF----------\n");
    staff_member_t staff_members[4]={0};

    for(int i=0; i<MAX_STAFF; i++){
        if(cook_ladder[i].id==0) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==1) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==2) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==3) staff_members[i]=cook_ladder[i].membro;
    }

    for(int i=0; i<MAX_STAFF; i++){
        printf("%s: %d, %d, %d, %d\n",
             staff_members[i].name, staff_members[i].skills[0], staff_members[i].skills[1],staff_members[i].skills[2],staff_members[i].skills[3]);    
    }
}

//print di debug per la stampa della fatica attuale dello staff
void debug_print_staff_fatigue(){
    printf("----------STAMPA FATICA STAFF----------\n");
    staff_member_t staff_members[4]={0};

    for(int i=0; i<MAX_STAFF; i++){
        if(cook_ladder[i].id==0) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==1) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==2) staff_members[i]=cook_ladder[i].membro;
        if(cook_ladder[i].id==3) staff_members[i]=cook_ladder[i].membro;
    }

    for(int i=0; i<MAX_STAFF; i++){
        printf("%s\n", staff_members[i].name);
        printf("fatica ruolo WAITER: %d\n", fatigue_ladder[i][0]);
        printf("fatica ruolo COOK: %d\n", fatigue_ladder[i][1]);
        printf("fatica ruolo HELPER: %d\n", fatigue_ladder[i][2]);
        printf("fatica ruolo CASHIER: %d\n", fatigue_ladder[i][3]);
        printf("\n");   
    }
}

//estraggo il primo elemento dall'array specificato[ladder] se non é giá occupato
int pop(ladder_args *array, int staff_n) {
    for (int pos = 0; pos < staff_n; pos++) {
        //se é libero lo prendo
        if (array[pos].stato == 0) {
            int id = array[pos].id;
            //occupo il membro per tutte le altre ladder
            for (int j = 0; j < staff_n; j++) {
                if (waiter_ladder[j].id == id)waiter_ladder[j].stato = 1;
                if (cook_ladder[j].id == id)cook_ladder[j].stato = 1;
                if (helper_ladder[j].id == id)helper_ladder[j].stato = 1;
                if (cashier_ladder[j].id == id)cashier_ladder[j].stato = 1;
            }
            //restituisco l'id
            return id;
        }
    }

    return -1;  // nessun membro disponibile
}

// assegna nella lavagna il membro di pop in base al role passato
void assign_role(role_t role, int staff_n) {
    switch(role){
        case ROLE_COOK:{
            int cook=pop(cook_ladder, staff_n);
            shm_blackboard->cook=cook;
            break;
        }
        case ROLE_WAITER:{
            int waiter_id=pop(waiter_ladder, staff_n);
            for (int i=0; i<shm_blackboard->tables_n; i++)
                shm_blackboard->tables[i].waiter=waiter_id;

            break;
        }
        case ROLE_HELPER:
        case ROLE_DISHWASHER:{
            int helper_id=pop(helper_ladder, staff_n);
            for (int i=0; i<shm_blackboard->tables_n; i++)
                shm_blackboard->tables[i].cleaner=helper_id;

            shm_blackboard->dishwasher=helper_id;
            break;
        }
        case ROLE_CASHIER:{
            int cashier_id=pop(cashier_ladder, staff_n);
            shm_blackboard->cashier=cashier_id;
            break;
        }
        case ROLE_NONE: break;
    }
}

//settaggio dello stato del membro passato in tutte le ladder
void set_staff_state(int member_id, int state, int staff_n) {
    ladder_args *ladders[]={waiter_ladder,cook_ladder,helper_ladder,cashier_ladder};
    for (int l=0; l<4; l++) {
        for (int i=0; i<staff_n; i++) {
            if (ladders[l][i].id==member_id) ladders[l][i].stato=state;
        }
    }
}

//se due ruoli sono uguali
int same_role_group(role_t a, role_t b) {
    if (a==b)
        return 1;

    return (a==ROLE_HELPER || a==ROLE_DISHWASHER) &&
           (b==ROLE_HELPER || b==ROLE_DISHWASHER);
}

//in base al ruolo ritorno la posizione dell'abilitá corrispondente: ROLE_WAITER=SKILL_WAITER=0
int pos_role(msg_fatigue_t fatigue){
    if(fatigue.role==ROLE_WAITER){return 0;}
    else if(fatigue.role==ROLE_COOK){return 1;}
    else if(fatigue.role==ROLE_HELPER || fatigue.role==ROLE_DISHWASHER){return 2;}
    else if(fatigue.role==ROLE_CASHIER){return 3;}
    else{return -1;}
}

//insertion sort dell'array passato [ladder] in base alla pos passata [skill]
void sort_by_fatigue(ladder_args* array, int pos) {
    for (int i=1; i<MAX_STAFF; i++) {
        ladder_args current=array[i];
        int j=i-1;

        while (j>=0 && atomic_load(&fatigue_ladder[array[j].id][pos]) < atomic_load(&fatigue_ladder[current.id][pos])) {
            array[j+1]=array[j];
            j--;
        }

        array[j+1]=current;
    }
}

/*
    PROFITTO
    . ordino solo per ABILITÁ
    . se risultato uguale scelgo chi ha fatigue+ABILITÁ migliori (chi é meno stanco)
    . se risultato uguale allora scelgo chi ha i tratti peggiori
    . maximizzo la velocitá a scapito della reputazione

    REPUTAZIONE
    . ordino per TRATTI
    . se risultato uguale scelgo chi ha fatigue+ABILITÁ peggiore
    . se sono stachi uguali scelgo chi ha ABILITÁ peggiore
    . maximizzo la reputazione a scapito della velocitá

    per tutte e due le strategie faccio il sorting per fatica alla fine, cosí i piú stanchi per quella skill non vengono scelti
*/
void sort_by(ladder_args* array, int skill, int trait, atomic_int strategy) {
    if(strategy==STRATEGY_PROFIT){
        for(int i=0; i<MAX_STAFF-1; i++) {
            int max_idx=i;

            for(int j=i+1; j<MAX_STAFF; j++){
                int fatigue_id=atomic_load(&fatigue_ladder[j][skill]);
                int fatigue_max_idx=atomic_load(&fatigue_ladder[max_idx][skill]);

                //chi ha abilitá maggiore
                if(array[j].membro.skills[skill]>array[max_idx].membro.skills[skill]){
                    max_idx=j;

                //se skill uguale chi ha fatigue+ABILITÁ maggiore
                }else if(array[j].membro.skills[skill]==array[max_idx].membro.skills[skill]){
                    if(((fatigue_id)+(array[j].membro.skills[skill]))
                            >
                            ((fatigue_max_idx)+(array[max_idx].membro.skills[skill]))){
                    max_idx=j;

                    //se fatigue+ABILITÁ uguale scelgo tratti peggiori
                    }else if(fatigue_id==fatigue_max_idx){
                        max_idx=(array[j].membro.traits[trait]<array[max_idx].membro.traits[trait])?j:max_idx;
                    } 
                }
            }

            if(max_idx!=i){
                ladder_args temp=array[i];
                array[i]=array[max_idx];
                array[max_idx]=temp;
            }
        }

        sort_by_fatigue(array, skill);
    }

    else if(strategy==STRATEGY_REPUTATION){
        for(int i=0; i<MAX_STAFF-1; i++) {
            int max_idx=i;

            for(int j=i+1; j<MAX_STAFF; j++){
                int fatigue_id=atomic_load(&fatigue_ladder[j][skill]);
                int fatigue_max_idx=atomic_load(&fatigue_ladder[max_idx][skill]);

                //se tratti maggiori
                if(array[j].membro.traits[trait]>array[max_idx].membro.traits[trait]){
                    max_idx=j;

                //se tratti uguali scelgo chi ha fatigue+ABILITÁ peggiori
                }else if(array[j].membro.traits[trait]==array[max_idx].membro.traits[trait]){
                    if(((fatigue_id)+(array[j].membro.skills[skill]))
                            <
                            ((fatigue_max_idx)+(array[max_idx].membro.skills[skill]))){
                        max_idx=j;

                    //se la fatica é uguale scelgo i meno bravi
                    }else if(fatigue_id==fatigue_max_idx){
                       max_idx=(array[j].membro.skills[skill]<array[max_idx].membro.skills[skill])?j:max_idx;
                    }
                }
            }

            if(max_idx!=i){
                ladder_args temp=array[i];
                array[i]=array[max_idx];
                array[max_idx]=temp;
            }
        }

        sort_by_fatigue(array, skill);
    }
}

//aggiorna la fatica per member_id passato, riordina le ladder
void update_ladder_fatigue(int member_id, msg_fatigue_t fatigue){
    int fatigue_value=atomic_load(&fatigue_ladder[member_id][pos_role(fatigue)]);
    atomic_store(&fatigue_ladder[member_id][pos_role(fatigue)], -(fatigue.new_lvl));

    //DEBUG: print fatigue
    // debug_print_staff_fatigue();

    sort_by(waiter_ladder, SKILL_WAITER,TRAIT_PATIENCE+TRAIT_PROFESSIONALITY ,atomic_load(&current_strategy));
    sort_by(cook_ladder, SKILL_COOK,TRAIT_PROFESSIONALITY ,atomic_load(&current_strategy));
    sort_by(helper_ladder, SKILL_HELPER,0 ,atomic_load(&current_strategy));
    sort_by(cashier_ladder, SKILL_CASHIER,TRAIT_SOCIABILITY ,atomic_load(&current_strategy));
}

//gestisce la fatica del member_id passato facendo scegliere al suo posto il secondo migliore, riordinando le ladder
void update_role(int member_id, msg_fatigue_t fatigue, int staff_n) {

    lock_blackboard();
    
    // 1. reset degli stati dello staff nella ladder e nella blackboard
    //ladder: sono tutti liberi
    ladder_args *ladders[] = {waiter_ladder,cook_ladder,helper_ladder,cashier_ladder};
    for (int l = 0; l < 4; l++) {
        for (int i = 0; i < staff_n; i++)
            ladders[l][i].stato = 0;
    }
    //blackboard: non c'é nessuno a fare niente
    for(int i=0; i<shm_blackboard->tables_n; i++){
        shm_blackboard->tables[i].waiter=-1;
        shm_blackboard->tables[i].cleaner=-1;
    }
    shm_blackboard->cook=-1;
    shm_blackboard->cashier=-1;

    //DEBUG: azzeramento corretto
    // printf("----------DEBUG: print blackboard dopo azzeramento----------\n");
    // debug_print_blackboard();

    // 2. aggiornamento del personale in base alla fatica (malus -1), quindi aggiornamento ladder
    update_ladder_fatigue(member_id, fatigue);

    // 3. Il membro affaticato non può essere scelto
    set_staff_state(member_id, 1, staff_n);

    // 4. Riassegno la mansione interessata
    assign_role(fatigue.role, staff_n);

    // 5. Rendo nuovamente disponibile il membro affaticato
    set_staff_state(member_id, 0, staff_n);

    // 6. Riassegno le altre mansioni
    role_t roles[] = {ROLE_COOK,ROLE_WAITER,ROLE_HELPER,ROLE_CASHIER};
    for (int i = 0; i < 4; i++) {
        if (!same_role_group(roles[i], fatigue.role))
            assign_role(roles[i], staff_n);
    }

    //DEBUG: assegnazione corretta
    // printf("----------DEBUG: print blackboard dopo rotazione----------\n");
    // debug_print_blackboard();

    unlock_blackboard();
}

//ricezione della fatica
int receive_fatigue(int staff_id, msg_fatigue_t *fatigue) {
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_FATIGUE);
    int msqid = msgget(key, 0);
    return msgrcv(msqid,fatigue,sizeof(*fatigue) - sizeof(long),(long)staff_id + 1,IPC_NOWAIT);
}

//thread di ogni membro dello staff che prende la fatica e risistema le ladder
void *worker(void *arg) {
    thread_args *args = arg;
    int member_id = args->id;
    int staff_n = MAX_STAFF;
    staff_member_t membro = args->membro;
    msg_fatigue_t fatigue;
    
    const struct timespec pause = {
        .tv_sec = 0,
        .tv_nsec = 10 * 1000 * 1000  // 10 millisecondi
    };

    //finché il gioco é attivo controllo la fatica se arriva oppure metto in sleep il thread
    while (atomic_load(&game_running)) {
        if (receive_fatigue(member_id, &fatigue)>=0) {
            //DEBUG: fatica ricevuta
            // printf("[WORKER %d] Fatica ricevuta: id=%d, ruolo=%d\n",member_id, fatigue.staff_id, fatigue.role);
            // fflush(stdout);

            update_role(fatigue.staff_id, fatigue, staff_n);
        } 

        nanosleep(&pause, NULL);
    }

    return 0;
}

//popolo l'array [ladder] con i membri e id
void fill_ladder(msg_welcome_t welcome, ladder_args* array){
    for(int i=0; i<welcome.staff_n; i++){
        array[i].membro=welcome.staff[i];
        array[i].id=i;
    }
}

//ordina le ladder e scrive nella lavagna gli id corretti: viene utilizzato come metodo di iniziailzzazione
void assign_tasks_staff(atomic_int strategy){
    // //ordino le ladder
    sort_by(waiter_ladder, SKILL_WAITER,TRAIT_PATIENCE+TRAIT_PROFESSIONALITY ,strategy);
    sort_by(cook_ladder, SKILL_COOK,TRAIT_PROFESSIONALITY ,strategy);
    sort_by(helper_ladder, SKILL_HELPER,0,strategy);
    sort_by(cashier_ladder, SKILL_CASHIER,TRAIT_SOCIABILITY ,strategy);

    //DEBUG: stampa delle ladder
    // debug_print_ladder();

    //assegno i compiti ai piú bravi in ordine decrescente di abilitá
    //libero tutti dalla schiavitú
    for(int i=0; i<MAX_STAFF; i++){
        cook_ladder[i].stato=0;
        waiter_ladder[i].stato=0;
        helper_ladder[i].stato=0;
        cashier_ladder[i].stato=0;
    }

    //attribuzione
    int cook_id=pop(cook_ladder, MAX_STAFF);
    int waiter_id=pop(waiter_ladder, MAX_STAFF);
    int helper_id = pop(helper_ladder, MAX_STAFF);
    int cashier_id=pop(cashier_ladder, MAX_STAFF);

    //DEBUG: pop ha funzionato?
    // printf("%d, %d, %d, %d\n", cook_id, waiter_id, helper_id, cashier_id);

    //assegnamento
    for(int i=0; i<shm_blackboard->tables_n; i++){
        shm_blackboard->tables[i].waiter=waiter_id;
        shm_blackboard->tables[i].cleaner=helper_id;
    } 
    shm_blackboard->cook=cook_id;
    shm_blackboard->dishwasher=helper_id;
    shm_blackboard->cashier=cashier_id;

    //DEBUG: stampo le assegnazioni
    // debug_print_blackboard();
}

//ricezione fine dei giochi
int receive_end_game(msg_end_t* end_msg) {
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, 0);
    return msgrcv(msqid,end_msg,sizeof(*end_msg) - sizeof(long),MSGTYPE_END,IPC_NOWAIT);
}

//ricezione istanza di gioco
int receive_instance(msg_instance_t* instance) {
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, 0);
    return msgrcv(msqid,instance,sizeof(*instance) - sizeof(long),MSGTYPE_INSTANCE,IPC_NOWAIT);
}

//ricezione fine istanza di gioco
int receive_done_instance(msg_instance_done_t* instance_done) {
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, 0);
    return msgrcv(msqid,instance_done,sizeof(*instance_done) - sizeof(long),MSGTYPE_INSTANCE_DONE,IPC_NOWAIT);
}

//thread monitor che prende i messaggi mandati dal server: istanza, fine istanza e fine dei giochi
void *msgs_monitor(void *arg) {
    msg_end_t end_msg;
    msg_instance_t instance;
    msg_instance_done_t instance_done;

    //finché il gioco va continuo a prendere quello che il server mi manda
    while(atomic_load(&game_running)){

        //quando ricevo una nuova istanza assegno la strategia e inizializzo tutto
        if(receive_instance(&instance)>=0){
            atomic_store(&current_strategy, instance.strategy);

            //DEBUG: strategua ricevuta
            // printf("[MONITOR] strategia ricevuta: %d\n",STRATEGY);
            // fflush(stdout);

            lock_blackboard();
            assign_tasks_staff(atomic_load(&current_strategy));
            unlock_blackboard();
        }

        //quando ricevo la fine di un'istanza azzero la fatica del personale
        if(receive_done_instance(&instance_done)>=0){
            //DEBUG: fine istanza ricevuto
            // printf("[MONITOR] fine istanza ricevuto\n");
            // fflush(stdout);

            for(int i=0; i<MAX_STAFF; i++){
                for(int j=0; j<MAX_STAFF; j++){
                    atomic_store(&fatigue_ladder[i][j], 0);
                }
            }
        }

        //quando ricevo la fine dei giochi assegno la variabile globale
        if(receive_end_game(&end_msg)>=0){
            //DEBUG:
            printf("[MONITOR] end msg ricevuto\n");
            fflush(stdout);

            atomic_store(&game_running, 0);
        }
    }

    return NULL;
}

//ricezione messaggio di welcome
int receive_welcome(msg_welcome_t* welcome){
    key_t key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_S2C);
    int msqid = msgget(key, S_IRUSR);
    return msgrcv(msqid, welcome, sizeof(*welcome) - sizeof(long), MSGTYPE_WELCOME, 0);
}

//invio del messaggio di hello al server con strategia specificata
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
    strncpy(hello_msg.studentids[0], "VR517101", STUDENTID_MAXLEN);
    strncpy(hello_msg.studentids[1], "VR500669", STUDENTID_MAXLEN); 
    strncpy(hello_msg.studentids[2], "VR500517", STUDENTID_MAXLEN);
    int key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    int msqid = msgget(key, S_IWUSR | S_IRUSR); 
    return msgsnd(msqid, &hello_msg, sizeof(hello_msg) - sizeof(long), 0);
}

//invio del messaggio di hello al server senza strategia specificata
int begin_handshake_nostrategy(){
    msg_hello_t hello_msg;
    hello_msg.mtype = MSGTYPE_HELLO;
    hello_msg.has_strategy = TR_FALSE;
    hello_msg.pid = getpid();
    hello_msg.studentid_n = 3;
    strncpy(hello_msg.studentids[0], "VR517101", STUDENTID_MAXLEN);
    strncpy(hello_msg.studentids[1], "VR500669", STUDENTID_MAXLEN); 
    strncpy(hello_msg.studentids[2], "VR500517", STUDENTID_MAXLEN);
    int key = ftok(TRATTORIA_FTOK_PATH, PROJ_MSG_C2S);
    int msqid = msgget(key, 0); 
    return msgsnd(msqid, &hello_msg, sizeof(hello_msg) - sizeof(long), 0);
}

//metodo finale per chiudere tutte le memorie
void close_all(void) {
    if(shmdt(shm_blackboard) == -1 || shmdt(shm_cashdesk) == -1 || shmdt(shm_dining_room) == -1 || shmdt(shm_kitchen) == -1){
        perror("shmdt");
    }
}

int main(int argc, char **argv) {
    //inizializzazione
    init_all_shm();
    init_semaphores();

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
                atomic_init(&current_strategy, STRATEGY_PROFIT);
            }
            else if(strncmp(strategy, "reputation", strlen("reputation")) == 0){
                atomic_init(&current_strategy, STRATEGY_REPUTATION);   
            }
            begin_handshake(current_strategy);
        }
    }
    else begin_handshake_nostrategy();
    
    //ricezione del messaggio di welcome e stampa delle info utili del personale
    msg_welcome_t welcome;
    receive_welcome(&welcome); 
    print_waiters(welcome);

    //popolo le ladder
    fill_ladder(welcome, waiter_ladder);
    fill_ladder(welcome, cook_ladder);
    fill_ladder(welcome, helper_ladder);
    fill_ladder(welcome, cashier_ladder);

    //assegnazione statica in base alla strategia
    assign_tasks_staff(current_strategy);

    //thread monitor per la ricezione dei messagi del server
    pthread_t s2c_msgs_thread;
    pthread_create(&s2c_msgs_thread, NULL, msgs_monitor, NULL);

    //thread dello staff
    pthread_t worker_threads[MAX_STAFF];
    thread_args worker_args[MAX_STAFF];
    for(int i = 0; i < welcome.staff_n; i++){
      worker_args[i].membro = welcome.staff[i];
      worker_args[i].id = i;

      pthread_create(&worker_threads[i],NULL , worker, &worker_args[i]);
    }
   
    //fine dei thread
    for(int i = 0; i < welcome.staff_n; i++){
       pthread_join(worker_threads[i], NULL);
    }
    pthread_join(s2c_msgs_thread, NULL);

    //chiusura delle memorie
    close_all();
}
