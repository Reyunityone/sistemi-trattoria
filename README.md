# Client Trattoria

## Panoramica

Il client gestisce il personale della trattoria durante le istanze di gioco. 
Il suo compito principale è assegnare i membri dello staff alle mansioni disponibili e aggiornare le assegnazioni quando il server comunica che un membro ha accumulato fatica.

La scelta del personale avviene usando quattro classifiche (*ladder*), una per ciascuna abilità:
-   **Cameriere** (`waiter`)
-   **Cuoco** (`cook`)
-   **Aiutante** (`helper`)
-   **Cassiere** (`cashier`)

A queste si affianca una matrice della fatica, che registra separatamente la fatica accumulata da ciascun membro in ciascun ruolo. La fatica viene trattata come un malus: quando arriva una notifica dal server, il client aggiorna il valore corrispondente e riordina le classifiche.

Il client supporta due strategie di selezione: **profitto** e **reputazione**.

## Organizzazione dei dati

### Le ladder dello staff

Ogni ladder è un array di elementi `ladder_args`. Ogni elemento contiene:
-   `membro`: le informazioni del membro dello staff, comprese abilità e
    tratti;
-   `id`: l'identificativo del membro;
-   `stato`: indica se il membro è libero (`0`) oppure già occupato (`1`).

Le quattro ladder contengono gli stessi membri, ma sono ordinate in modo diverso in base alla mansione considerata. In questo modo il client può trovare rapidamente il candidato migliore per ciascun ruolo.

### Matrice della fatica

`fatigue_ladder` è una matrice atomica indicizzata per membro e ruolo.
La fatica è distinta per mansione: essere affaticati come cuoco, per esempio, non implica avere lo stesso livello di fatica come cameriere.

Quando arriva una notifica, il client aggiorna la cella relativa al membro e al ruolo indicati dal messaggio. Il valore viene memorizzato con segno negativo, così un valore più vicino a zero rappresenta una situazione più favorevole nell'ordinamento basato sulla fatica.

### Blackboard

Il client scrive le assegnazioni nella memoria condivisa della
*blackboard*:
-   il ruolo di cuoco;
-   il ruolo di cassiere;
-   il cameriere assegnato ai tavoli;
-   l'aiutante assegnato come addetto alla pulizia e alla lavastoviglie.

La blackboard viene condivisa con il server. Per evitare che più thread modifichino contemporaneamente le assegnazioni, il client usa il semaforo associato alla blackboard durante le operazioni di assegnazione e sostituzione.

## Strategie di selezione

### Strategia profitto

La strategia profitto privilegia le **abilità** del personale, con l'obiettivo di favorire l'efficienza e la velocità del servizio, anche a scapito della reputazione. L'ordinamento previsto segue questi criteri:
1.  privilegia il membro con il punteggio più alto nell'abilità richiesta;
2.  in caso di parità, considera la combinazione tra abilità e fatica, favorendo il candidato con il risultato migliore;
3.  se permane una parità, usa il tratto pertinente come criterio aggiuntivo, preferendo il valore più basso;
4.  applica infine l'ordinamento per fatica, in modo da favorire i membri meno affaticati per quella mansione.

### Strategia reputazione

La strategia reputazione privilegia i **tratti** del personale, con l'obiettivo di massimizzare la reputazione, anche a scapito della velocità. L'ordinamento previsto segue questi criteri:
1.  privilegia il membro con il punteggio più alto nel tratto pertinente;
2.  in caso di parità, considera la combinazione tra fatica e abilità, favorendo il candidato con il risultato più basso;
3.  se la fatica è uguale, usa l'abilità come ulteriore criterio, favorendo il membro con l'abilità più bassa;
4.  applica infine l'ordinamento per fatica.

Secondo le note di progetto, nella strategia reputazione il tratto **resistenza** viene ignorato: il personale più adatto viene sfruttato finché il server non segnala la fatica, momento in cui il client lo sostituisce.

### Priorità di assegnazione

Le mansioni vengono assegnate in questo ordine:
1.  cuoco;
2.  cameriere;
3.  aiutante;
4.  cassiere.

L'ordine è importante perché lo stesso membro non può ricoprire contemporaneamente più mansioni incompatibili.

## Assegnazione iniziale del personale

Dopo aver ricevuto il messaggio di benvenuto dal server, il client stampa le statistiche dello staff e popola le quattro ladder con i membri ricevuti.

Successivamente:
1.  ordina ciascuna ladder secondo la strategia attiva;
2.  rende liberi i membri nelle ladder;
3.  seleziona il primo membro disponibile di ogni ladder;
4.  quando seleziona un membro, lo marca come occupato in tutte le ladder;
5.  scrive gli identificativi scelti nella blackboard condivisa.

La funzione `pop()` realizza la selezione del primo membro disponibile. Il suo stato viene aggiornato in tutte le ladder, così un membro già assegnato a una mansione non viene selezionato nuovamente per un'altra mansione.
L'aiutante viene assegnato sia alla pulizia dei tavoli sia alla pulizia dei piatti.

## Gestione della fatica e sostituzione

Il client avvia un thread per ogni membro dello staff. Ogni worker controlla la coda dei messaggi di fatica cercando quelli indirizzati al proprio identificativo. Quando un worker riceve una notifica, richiama `update_role()`, che esegue la sostituzione in una sezione protetta dal semaforo:
1.  **Acquisisce il semaforo** della blackboard.
2.  **Azzera temporaneamente gli stati** delle ladder e rimuove dalla blackboard le assegnazioni correnti.
3.  **Aggiorna la fatica** del membro nel ruolo indicato dal messaggio.
4.  **Riordina le ladder** secondo la strategia corrente e i nuovi valori di fatica.
5.  **Esclude temporaneamente il membro affaticato**, marcandolo come occupato in tutte le ladder.
6.  **Assegna nuovamente la mansione interessata**, scegliendo il miglior membro disponibile.
7.  **Rende di nuovo disponibile il membro affaticato** per eventuali altre mansioni.
8.  **Riassegna le altre mansioni**, rispettando l'ordine di priorità e impedendo che lo stesso membro venga scelto più volte.
9.  **Rilascia il semaforo** della blackboard.

I ruoli `helper` e `dishwasher` sono considerati appartenenti allo stesso gruppo: una notifica relativa a uno dei due non provoca una seconda assegnazione separata dell'altro, perché vengono coperti dallo stesso membro.
La fatica è quindi gestita per coppia **membro--ruolo**, non come un unico valore generale associato alla persona.

## Thread del client

Il client usa due tipi di thread.

### Thread worker dello staff

Viene creato un worker per ciascun membro dello staff. Ogni worker:
-   controlla periodicamente la coda dei messaggi di fatica;
-   riceve le notifiche associate al proprio identificativo;
-   avvia la procedura di aggiornamento e sostituzione quando riceve una notifica;
-   termina quando `game_running` diventa falso.

Il controllo della coda è non bloccante (`IPC_NOWAIT`); quando non ci sono messaggi, il worker attende brevemente prima di riprovare.

### Thread monitor

Il thread `msgs_monitor` controlla i messaggi provenienti dal server. Gestisce tre eventi principali:
-   **Nuova istanza:** legge la strategia dell'istanza e assegna inizialmente i compiti secondo tale strategia.
-   **Fine dell'istanza:** azzera la matrice della fatica, preparando il personale per l'istanza successiva.
-   **Fine del gioco:** imposta `game_running` a falso, segnalando ai thread worker che devono terminare.

La variabile atomica `game_running` consente ai thread di condividere il segnale di arresto. Anche `current_strategy` è atomica, così la strategia corrente può essere letta e aggiornata dai thread.

## Comunicazione e memoria condivisa

Il client comunica con il server attraverso code di messaggi System V e accede alle strutture condivise tramite memoria condivisa System V.

All'avvio:
1.  recupera gli identificativi delle aree di memoria condivisa usando `ftok()` e `shmget()`;
2.  collega le aree con `shmat()`;
3.  recupera il semaforo che protegge la blackboard;
4.  invia al server il messaggio di `HELLO`, con o senza una strategia specificata;
5.  attende il messaggio `WELCOME`, che contiene le informazioni dello staff.

La blackboard viene collegata in lettura e scrittura, mentre le aree di cassa, sala e cucina vengono collegate in sola lettura. Alla fine del gioco, il client scollega le aree di memoria condivisa con `shmdt()`.

## Ciclo di vita

In sintesi, il flusso del client è il seguente:
1.  inizializza memoria condivisa e semafori;
2.  interpreta l'eventuale strategia indicata negli argomenti della riga di comando;
3.  invia `HELLO` e riceve `WELCOME`;
4.  inizializza le ladder e assegna i ruoli iniziali;
5.  avvia il thread monitor e i thread worker;
6.  aggiorna le assegnazioni quando arrivano notifiche di fatica e prepara il personale per le istanze successive;
7.  termina i thread alla fine del gioco e scollega le memorie condivise.

## Funzioni principali

  -----------------------------------------------------------------------
  Funzione                            Responsabilità
  ----------------------------------- -----------------------------------
  `init_all_shm()`                    Collega le aree di memoria condivisa usate dal client.

  `init_semaphores()`                 Recupera e configura le operazioni sul semaforo della blackboard.

  `fill_ladder()`                     Copia i membri dello staff in unaladder.

  `sort_by()`                         Ordina una ladder in base alla strategia selezionata.

  `sort_by_fatigue()`                 Ordina una ladder considerando la fatica nel ruolo.

  `pop()`                             Seleziona il primo membro disponibile e lo marca come occupato in tutte le ladder.

  `assign_role()`                     Scrive nella blackboard l'assegnazione relativa a una mansione.

  `assign_tasks_staff()`              Ordina le ladder ed effettua l'assegnazione iniziale.

  `receive_fatigue()`                 Riceve un messaggio di fatica per uno specifico membro.

  `update_ladder_fatigue()`           Aggiorna la fatica e riordina le ladder.

  `update_role()`                     Gestisce la sostituzione del membro affaticato e la riassegnazione dei ruoli.

  `worker()`                          Esegue il ciclo di controllo della fatica di un membro dello staff.

  `msgs_monitor()`                    Gestisce i messaggi di nuova istanza, fine istanza e fine gioco.

  `close_all()`                       Scollega le aree di memoria condivisa.
  -----------------------------------------------------------------------