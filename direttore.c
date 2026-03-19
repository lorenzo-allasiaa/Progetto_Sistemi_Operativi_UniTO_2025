#include "common.h"

extern char **environ;

void stampa_statistiche(MemoriaCondivisa* shm, int giorno);
void stampa_statistiche_finali(MemoriaCondivisa* shm, const char* motivo);
void inizializza_sportelli_giorno(MemoriaCondivisa* shm, int semid);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <file_configurazione>\n", argv[0]);
        exit(1);
    }

    srand(time(NULL));

    leggi_configurazione(argv[1]);

    if (NOF_WORKERS > MAX_WORKERS) {
        fprintf(stderr, "ERRORE: NOF_WORKERS (%d) supera MAX_WORKERS (%d)\n", 
                NOF_WORKERS, MAX_WORKERS);
        exit(1);
    }
    if (NOF_USERS > MAX_USERS) {
        fprintf(stderr, "ERRORE: NOF_USERS (%d) supera MAX_USERS (%d)\n", 
                NOF_USERS, MAX_USERS);
        exit(1);
    }
    if (NOF_WORKER_SEATS > MAX_SPORTELLI) {
        fprintf(stderr, "ERRORE: NOF_WORKER_SEATS (%d) supera MAX_SPORTELLI (%d)\n", 
                NOF_WORKER_SEATS, MAX_SPORTELLI);
        exit(1);
    }
    
    printf("--- AVVIO SIMULAZIONE UFFICIO POSTALE ---\n");
    printf("Durata: %d giorni\n", SIM_DURATION);
    printf("Operatori: %d, Utenti: %d, Sportelli: %d\n\n", NOF_WORKERS, NOF_USERS, NOF_WORKER_SEATS);

    int shmid = crea_shm();
    int semid = crea_semafori();
    int msgid = crea_msgqueue();

    MemoriaCondivisa* shm = attach_shm(shmid);
    memset(shm, 0, sizeof(MemoriaCondivisa));

    shm->p_serv_min = P_SERV_MIN;
    shm->p_serv_max = P_SERV_MAX;

    /* Inizializza semafori */
    sem_init_value(semid, SEM_SHM, 1);
    sem_init_value(semid, SEM_SPORTELLI, 1);

    for (int i = 0; i < NUM_SERVIZI; i++) {
        sem_init_value(semid, SEM_CODE_BASE + i, 1);
    }

    /* Inizializza semaforo barriera per sincronizzazione inizializzazione */
    int processi_totali = 1 + NOF_WORKERS + NOF_USERS;
    sem_init_value(semid, SEM_INIT_BARRIER, 0);

    sem_init_value(semid, SEM_EVENTO_GIORNO_OPERATORI, 0);
    sem_init_value(semid, SEM_EVENTO_GIORNO_UTENTI, 0);
    sem_init_value(semid, SEM_EVENTO_FINE_GIORNATA, 0);
    sem_init_value(semid, SEM_SPORTELLO_DISPONIBILE, 0);
    sem_init_value(semid, SEM_FINE_AGGIORNAMENTI, 0);

    for (int i = 0; i < NUM_SERVIZI; i++) {
        sem_init_value(semid, SEM_UTENTI_PRESENTI_CODA_BASE + i, 0);
    }

    sem_init_value(semid, SEM_OPERATORI_PRONTI, 0);

    /* Inizializza stato simulazione prima di creare i processi figli */
    shm->simulazione_attiva = 1;
    shm->giorno_corrente = -1;
    shm->termine_giornata = 1;

    /* Inizializza sportelli vuoti */
    for (int i = 0; i < NOF_WORKER_SEATS; i++) {
        shm->sportelli[i].occupato = 0;
        shm->sportelli[i].servizio = rand() % NUM_SERVIZI;
        shm->sportelli[i].operatore_id = -1;
    }

    /* Inizializza code circolari vuote */
    for (int i = 0; i < NUM_SERVIZI; i++) {
        shm->testa_coda[i] = 0;
        shm->coda_coda[i] = 0;
    }

    pid_t pid_operatori[MAX_WORKERS];
    pid_t pid_utenti[MAX_USERS];

    pid_t pid_erogatore = fork();
    if (pid_erogatore == 0) {
        char shmid_str[32], semid_str[32], msgid_str[32];
        sprintf(shmid_str, "%d", shmid);
        sprintf(semid_str, "%d", semid);
        sprintf(msgid_str, "%d", msgid);
        char *argv[] = {"erogatore", shmid_str, semid_str, msgid_str, NULL};
        execve("./erogatore", argv, environ);
        perror("execve erogatore");
        exit(1);
    }

    for (int i = 0; i < NOF_WORKERS; i++) {
        pid_operatori[i] = fork();
        if (pid_operatori[i] == 0) {
            char shmid_str[32], semid_str[32], id_str[32], seats_str[32], pause_str[32];
            sprintf(shmid_str, "%d", shmid);
            sprintf(semid_str, "%d", semid);
            sprintf(id_str, "%d", i);
            sprintf(seats_str, "%d", NOF_WORKER_SEATS);
            sprintf(pause_str, "%d", NOF_PAUSE);
            char *argv[] = {"operatore", shmid_str, semid_str, id_str, seats_str, pause_str, NULL};
            execve("./operatore", argv, environ);
            perror("execve operatore");
            exit(1);
        }
    }

    for (int i = 0; i < NOF_USERS; i++) {
        pid_utenti[i] = fork();
        if (pid_utenti[i] == 0) {
            char shmid_str[32], semid_str[32], msgid_str[32], id_str[32], nano_str[32], seats_str[32];
            sprintf(shmid_str, "%d", shmid);
            sprintf(semid_str, "%d", semid);
            sprintf(msgid_str, "%d", msgid);
            sprintf(id_str, "%d", i);
            sprintf(nano_str, "%d", N_NANO_SECS);
            sprintf(seats_str, "%d", NOF_WORKER_SEATS);
            char *argv[] = {"utente", shmid_str, semid_str, msgid_str, id_str, nano_str, seats_str, NULL};
            execve("./utente", argv, environ);
            perror("execve utente");
            exit(1);
        }
    }

    printf("Attendo inizializzazione di %d processi...\n", processi_totali);
    fflush(stdout);

    /* Direttore resta in attesa che tutti i figli facciano signal su SEM_INIT_BARRIER */
    for (int i = 0; i < processi_totali; i++) {
        sem_wait_wrapper(semid, SEM_INIT_BARRIER);
    }

    printf("Tutti i processi inizializzati. Avvio simulazione.\n\n");
    fflush(stdout);

    for (int giorno = 0; giorno < SIM_DURATION && shm->simulazione_attiva; giorno++) {
        printf("\n------ GIORNO %d ------\n", giorno + 1);
        fflush(stdout);

        /* Inizializza giornata */
        sem_wait_wrapper(semid, SEM_SHM);
        shm->giorno_corrente = giorno;
        shm->termine_giornata = 0; /* Sblocca operazioni giornata */
        memset(&shm->stat_giornata, 0, sizeof(StatGiornaliere));
        memset(shm->timestamp_attesa_completata, 0, sizeof(long) * MAX_USERS);
        sem_signal_wrapper(semid, SEM_SHM);

       /* Reset semaforo sportello disponibile per nuova giornata */
       sem_init_value(semid, SEM_SPORTELLO_DISPONIBILE, 0);

        /* Reset semafori che indicano utenti presenti nelle code circolari agli operatori per nuova giornata */
        for (int i = 0; i < NUM_SERVIZI; i++) {
            sem_init_value(semid, SEM_UTENTI_PRESENTI_CODA_BASE + i, 0);
        }

        inizializza_sportelli_giorno(shm, semid);

        for (int i = 0; i < NUM_SERVIZI; i++) {
            sem_wait_wrapper(semid, SEM_CODE_BASE + i);
            shm->testa_coda[i] = 0;
            shm->coda_coda[i] = 0;
            sem_signal_wrapper(semid, SEM_CODE_BASE + i);
        }

        sem_wait_wrapper(semid, SEM_SHM);
        shm->utenti_in_coda = 0;
        sem_signal_wrapper(semid, SEM_SHM);

        /* Reset semaforo barriera operatori pronti per la nuova giornata */
        sem_init_value(semid, SEM_OPERATORI_PRONTI, 0);

        /* Segnala inizio giornata solo agli operatori */
        for (int i = 0; i < NOF_WORKERS; i++) {
            sem_signal_wrapper(semid, SEM_EVENTO_GIORNO_OPERATORI);
        }

        /* Direttore attende che tutti gli operatori siano pronti */
        for (int i = 0; i < NOF_WORKERS; i++) {
            sem_wait_wrapper(semid, SEM_OPERATORI_PRONTI);
        }

        /* Direttore segnala inizio giornata agli utenti */
        for (int i = 0; i < NOF_USERS; i++) {
            sem_signal_wrapper(semid, SEM_EVENTO_GIORNO_UTENTI);
        }

        /* Simula giornata lavorativa */
        simula_tempo_minuti(480);

        /* Fine giornata: imposta flag e legge numero utenti rimasti in coda */
        sem_wait_wrapper(semid, SEM_SHM);
        shm->termine_giornata = 1; /* Blocca nuove operazioni */
        int utenti_rimasti = shm->utenti_in_coda;
        sem_signal_wrapper(semid, SEM_SHM);

        /* Sveglia operatori bloccati su SEM_SPORTELLO_DISPONIBILE perche giornata finita */
        for (int i = 0; i < NOF_WORKERS; i++) {
            sem_signal_wrapper(semid, SEM_SPORTELLO_DISPONIBILE);
        }

        /* Sveglia operatori bloccati sui semafori perche code servizi erano vuote */
        for (int i = 0; i < NUM_SERVIZI; i++) {
            for (int j = 0; j < NOF_WORKERS; j++) {
                sem_signal_wrapper(semid, SEM_UTENTI_PRESENTI_CODA_BASE + i);
            }
        }

        /* Segnala fine giornata a tutti i processi */
        for (int i = 0; i < NOF_WORKERS + NOF_USERS; i++) {
            sem_signal_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
        }

        /* Attende che utenti + operatori completino aggiornamenti prima di stampare statistiche giornata */
        for (int i = 0; i < NOF_USERS + NOF_WORKERS; i++) {
            sem_wait_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
        }

        stampa_statistiche(shm, giorno + 1);

        if (utenti_rimasti > EXPLODE_THRESHOLD) {
            printf("\n!!! TERMINAZIONE: Troppi utenti in coda (%d > %d) !!!\n",
                   utenti_rimasti, EXPLODE_THRESHOLD);
            shm->simulazione_attiva = 0;
            stampa_statistiche_finali(shm, "EXPLODE");
            break;
        }
    }

    if (shm->simulazione_attiva) {
        stampa_statistiche_finali(shm, "TIMEOUT");
    }

    /* Segnala fine simulazione ai processi figli */
    shm->simulazione_attiva = 0;

    kill(pid_erogatore, SIGTERM);
    for (int i = 0; i < NOF_WORKERS; i++) {
        kill(pid_operatori[i], SIGTERM);
    }
    for (int i = 0; i < NOF_USERS; i++) {
        kill(pid_utenti[i], SIGTERM);
    }

    for (int i = 0; i < NOF_WORKERS + NOF_USERS + 1; i++) {
        wait(NULL);
    }

    detach_shm(shm);
    rimuovi_shm(shmid);
    rimuovi_semafori(semid);
    rimuovi_msgqueue(msgid);

    printf("\n--- SIMULAZIONE TERMINATA ---\n");
    return 0;
}

/* Resetta tutti gli sportelli liberandoli e assegnando casualmente un servizio a ciascuno */
void inizializza_sportelli_giorno(MemoriaCondivisa* shm, int semid) {
    sem_wait_wrapper(semid, SEM_SPORTELLI);
    for (int i = 0; i < NOF_WORKER_SEATS; i++) {
        shm->sportelli[i].occupato = 0;
        shm->sportelli[i].servizio = rand() % NUM_SERVIZI;
        shm->sportelli[i].operatore_id = -1;
    }
    sem_signal_wrapper(semid, SEM_SPORTELLI);
}

/* Stampa statistihce della giornata corrente */
void stampa_statistiche(MemoriaCondivisa* shm, int giorno) {
    printf("\n--- STATISTICHE GIORNO %d ---\n", giorno);
    printf("Utenti serviti: %d\n", shm->stat_giornata.utenti_serviti);
    printf("Servizi erogati: %d\n", shm->stat_giornata.servizi_erogati);
    printf("Servizi non erogati: %d\n", shm->stat_giornata.servizi_non_erogati);

    /* Calcola e stampa tempo medio attesa se ci sono utenti serviti */
    if (shm->stat_giornata.utenti_serviti > 0) {
        printf("Tempo medio attesa giornata: %.1f min\n",
               shm->stat_giornata.tempo_attesa_totale / (double)shm->stat_giornata.utenti_serviti);
    }

    /* Calcola e stampa tempo medio erogazione se ci sono servizi erogati */
    if (shm->stat_giornata.servizi_erogati > 0) {
        printf("Tempo medio erogazione giornata: %.1f min\n",
               shm->stat_giornata.tempo_erogazione_totale / (double)shm->stat_giornata.servizi_erogati);
    }

    printf("Operatori attivi: %d\n", shm->stat_giornata.operatori_attivi);
    printf("Pause effettuate: %d", shm->stat_giornata.pause_effettuate);

    /* Calcola media pause per operatore */
    if (shm->stat_giornata.operatori_attivi > 0) {
        printf(" (media %.1f per operatore)",
               shm->stat_giornata.pause_effettuate / (double)shm->stat_giornata.operatori_attivi);
    }
    printf("\n");

    /* Rapporto operatori/sportelli */
    printf("\nRapporto operatori/sportelli per sportello:\n");
    int conteggio_per_servizio[NUM_SERVIZI] = {0};

    /* Conta numero di sportelli per ogni servizio */
    for (int i = 0; i < NOF_WORKER_SEATS; i++) {
        conteggio_per_servizio[shm->sportelli[i].servizio]++;
    }

    /* Per ogni sportello, mostra operatori disponibili vs sportelli servizio */
    for (int i = 0; i < NOF_WORKER_SEATS; i++) {
        TipoServizio servizio = shm->sportelli[i].servizio;
        int operatori_disponibili = 0;

        for (int j = 0; j < NOF_WORKERS; j++) {
            if (shm->mansione_operatori[j] == servizio) {
                operatori_disponibili++;
            }
        }

        printf("  Sportello %d (%s): %d operatori disponibili / %d sportelli servizio\n",
               i, NOMI_SERVIZI[servizio], operatori_disponibili, conteggio_per_servizio[servizio]);
    }

    /* Statistiche per tipo di servizio */
    printf("\nPer servizio:\n");
    for (int i = 0; i < NUM_SERVIZI; i++) {
        if (shm->stat_giornata.per_servizio[i].serviti > 0 ||
            shm->stat_giornata.per_servizio[i].non_serviti > 0) {
            printf("  %s: %d serviti, %d non serviti\n",
                   NOMI_SERVIZI[i],
                   shm->stat_giornata.per_servizio[i].serviti,
                   shm->stat_giornata.per_servizio[i].non_serviti);
        }
    }
}

/* Stampa statistiche finali di tutta la simulazione */
void stampa_statistiche_finali(MemoriaCondivisa* shm, const char* motivo) {
    int giorni = shm->giorno_corrente + 1;

    printf("\n\n--- STATISTICHE FINALI ---\n");
    printf("Motivo terminazione: %s\n", motivo);
    printf("Giorni simulati: %d\n\n", giorni);

    printf("TOTALI:\n");
    printf("  Utenti serviti: %d (media %.1f/giorno)\n",
           shm->stat_totali.utenti_serviti,
           shm->stat_totali.utenti_serviti / (double)giorni);
    printf("  Servizi erogati: %d (media %.1f/giorno)\n",
           shm->stat_totali.servizi_erogati,
           shm->stat_totali.servizi_erogati / (double)giorni);
    printf("  Servizi non erogati: %d (media %.1f/giorno)\n",
           shm->stat_totali.servizi_non_erogati,
           shm->stat_totali.servizi_non_erogati / (double)giorni);

    /* Calcola tempi medi totali */
    if (shm->stat_totali.utenti_serviti > 0) {
        printf("  Tempo medio attesa: %.1f min\n",
               shm->stat_totali.tempo_attesa_totale / (double)shm->stat_totali.utenti_serviti);
    }
    if (shm->stat_totali.servizi_erogati > 0) {
        printf("  Tempo medio erogazione: %.1f min\n",
               shm->stat_totali.tempo_erogazione_totale / (double)shm->stat_totali.servizi_erogati);
    }

    printf("\n  Operatori totali attivi: %d\n", shm->stat_totali.operatori_attivi);
    printf("  Pause totali: %d (media %.2f/giorno)\n",
           shm->stat_totali.pause_effettuate,
           shm->stat_totali.pause_effettuate / (double)giorni);

    /* Per servizio con tempi medi */
    printf("\nPER SERVIZIO:\n");
    for (int i = 0; i < NUM_SERVIZI; i++) {
        printf("  %s:\n", NOMI_SERVIZI[i]);
        printf("    Serviti: %d, Non serviti: %d\n",
               shm->stat_totali.per_servizio[i].serviti,
               shm->stat_totali.per_servizio[i].non_serviti);

        /* Tempi medi per servizio */
        if (shm->stat_totali.per_servizio[i].serviti > 0) {
            printf("    Tempo medio attesa: %.1f min\n",
                   shm->stat_totali.per_servizio[i].tempo_attesa_totale /
                   (double)shm->stat_totali.per_servizio[i].serviti);
            printf("    Tempo medio erogazione: %.1f min\n",
                   shm->stat_totali.per_servizio[i].tempo_erogazione_totale /
                   (double)shm->stat_totali.per_servizio[i].serviti);
        }
    }
}
