#include "common.h"

/* Valori di default, sovrascritti da leggi_configurazione() */
int SIM_DURATION = 5;
int NOF_WORKERS = 4;
int NOF_USERS = 10;
int NOF_WORKER_SEATS = 6;
int N_NANO_SECS = 10000000;
int NOF_PAUSE = 2;
int EXPLODE_THRESHOLD = 50;
double P_SERV_MIN = 0.8;
double P_SERV_MAX = 0.9;

volatile sig_atomic_t termina = 0;

void sigterm_handler(int signo) {
    (void)signo;
    termina = 1;
}


/* Legge file configurazione .conf e valida i valori */
void leggi_configurazione(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("Errore apertura file configurazione");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        char key[64];
        if (sscanf(line, "%s", key) == 1) {
            if (strcmp(key, "SIM_DURATION") == 0)
                sscanf(line, "%*s %d", &SIM_DURATION);
            else if (strcmp(key, "NOF_WORKERS") == 0)
                sscanf(line, "%*s %d", &NOF_WORKERS);
            else if (strcmp(key, "NOF_USERS") == 0)
                sscanf(line, "%*s %d", &NOF_USERS);
            else if (strcmp(key, "NOF_WORKER_SEATS") == 0)
                sscanf(line, "%*s %d", &NOF_WORKER_SEATS);
            else if (strcmp(key, "N_NANO_SECS") == 0)
                sscanf(line, "%*s %d", &N_NANO_SECS);
            else if (strcmp(key, "NOF_PAUSE") == 0)
                sscanf(line, "%*s %d", &NOF_PAUSE);
            else if (strcmp(key, "EXPLODE_THRESHOLD") == 0)
                sscanf(line, "%*s %d", &EXPLODE_THRESHOLD);
            else if (strcmp(key, "P_SERV_MIN") == 0)
                sscanf(line, "%*s %lf", &P_SERV_MIN);
            else if (strcmp(key, "P_SERV_MAX") == 0)
                sscanf(line, "%*s %lf", &P_SERV_MAX);
        }
    }
    fclose(f);

    if (SIM_DURATION <= 0) {
        fprintf(stderr, "ERRORE: SIM_DURATION deve essere > 0 (valore: %d)\n", SIM_DURATION);
        exit(1);
    }
    if (NOF_WORKERS <= 0) {
        fprintf(stderr, "ERRORE: NOF_WORKERS deve essere > 0 (valore: %d)\n", NOF_WORKERS);
        exit(1);
    }
    if (NOF_USERS <= 0) {
        fprintf(stderr, "ERRORE: NOF_USERS deve essere > 0 (valore: %d)\n", NOF_USERS);
        exit(1);
    }
    if (NOF_WORKER_SEATS <= 0) {
        fprintf(stderr, "ERRORE: NOF_WORKER_SEATS deve essere > 0 (valore: %d)\n", NOF_WORKER_SEATS);
        exit(1);
    }
    if (N_NANO_SECS <= 0) {
        fprintf(stderr, "ERRORE: N_NANO_SECS deve essere > 0 (valore: %d)\n", N_NANO_SECS);
        exit(1);
    }
    if (NOF_PAUSE < 0) {
        fprintf(stderr, "ERRORE: NOF_PAUSE deve essere >= 0 (valore: %d)\n", NOF_PAUSE);
        exit(1);
    }
    if (EXPLODE_THRESHOLD <= 0) {
        fprintf(stderr, "ERRORE: EXPLODE_THRESHOLD deve essere > 0 (valore: %d)\n", EXPLODE_THRESHOLD);
        exit(1);
    }
    if (P_SERV_MIN < 0.0 || P_SERV_MIN > 1.0) {
        fprintf(stderr, "ERRORE: P_SERV_MIN deve essere tra 0 e 1 (valore: %.2f)\n", P_SERV_MIN);
        exit(1);
    }
    if (P_SERV_MAX < 0.0 || P_SERV_MAX > 1.0) {
        fprintf(stderr, "ERRORE: P_SERV_MAX deve essere tra 0 e 1 (valore: %.2f)\n", P_SERV_MAX);
        exit(1);
    }
    if (P_SERV_MIN > P_SERV_MAX) {
        fprintf(stderr, "ERRORE: P_SERV_MIN (%.2f) deve essere <= P_SERV_MAX (%.2f)\n",
                P_SERV_MIN, P_SERV_MAX);
        exit(1);
    }
}


/* Simula il passaggio di minuti simulati tramite nanosleep */
int simula_tempo_minuti(int minuti_simulati) {

    long long total_nsecs = (long long)minuti_simulati * N_NANO_SECS;
    struct timespec ts;
    ts.tv_sec = total_nsecs / 1000000000LL;
    ts.tv_nsec = total_nsecs % 1000000000LL;

    return nanosleep(&ts, NULL);
}

/* Genera numero double casuale nell'intervallo [min, max] */
double random_double(double min, double max) {
    return min + (rand() / (double)RAND_MAX) * (max - min);
}

/* Genera numero intero casuale nell'intervallo [min, max],usato per erogazione casuale tempi di servizio */
int random_range(int min, int max) {
    return min + rand() % (max - min + 1);
}

/**
 * Operazione wait su semaforo
 * @param semid Id array semafori
 * @param sem_num Indice semaforo nell'array
 */
void sem_wait_wrapper(int semid, int sem_num) {
    struct sembuf sb;
    sb.sem_num = sem_num;
    sb.sem_op = -1;
    sb.sem_flg = 0;

    if (semop(semid, &sb, 1) == -1) {
        if (errno == EINTR) {
            return;
        }
        perror("semop wait");
        exit(1);
    }
}

/**
 * Operazione signal su semaforo
 *
 * @param semid Id array semafori
 * @param sem_num Indice semaforo nell'array
 */
void sem_signal_wrapper(int semid, int sem_num) {
    struct sembuf sb;
    sb.sem_num = sem_num;
    sb.sem_op = 1;
    sb.sem_flg = 0;
    semop(semid, &sb, 1);
}

/**
 * Inizializza valore di un semaforo
 *
 * @param semid Id array semafori
 * @param sem_num Indice semaforo nell'array
 * @param value Valore iniziale
 */
void sem_init_value(int semid, int sem_num, int value) {
    if (semctl(semid, sem_num, SETVAL, value) == -1) {
        perror("semctl SETVAL");
        exit(1);
    }
}


/* Crea segmento di memoria condivisa */
int crea_shm(void) {
    int shmid = shmget(KEY_SHM, sizeof(MemoriaCondivisa), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    return shmid;
}

/**
 * Attach memoria condivisa
 *
 * @param shmid Id del segmento di memoria condivisa
 */
MemoriaCondivisa* attach_shm(int shmid) {
    MemoriaCondivisa* shm = (MemoriaCondivisa*)shmat(shmid, NULL, 0);
    if (shm == (void*)-1) {
        perror("shmat");
        exit(1);
    }
    return shm;
}

/**
 * Detach memoria condivisa
 *
 * @param shm Puntatore alla memoria condivisa
 */
void detach_shm(MemoriaCondivisa* shm) {
    if (shmdt(shm) == -1) {
        perror("shmdt");
    }
}

/**
 * Rimuove segmento di memoria condivisa
 *
 * @param shmid Id del segmento di memoria condivisa
 */
void rimuovi_shm(int shmid) {
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID");
    }
}


/* Crea array di semafori */
int crea_semafori(void) {
    int semid = semget(KEY_SEM, NUM_SEM, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    return semid;
}

/**
 * Rimuove array di semafori
 *
 * @param semid Id dell'array di semafori
 */
void rimuovi_semafori(int semid) {
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID");
    }
}

/* Crea coda di messaggi */
int crea_msgqueue(void) {
    int msgid = msgget(KEY_MSG, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget");
        exit(1);
    }
    return msgid;
}

/**
 * Rimuove coda di messaggi
 *
 * @param msgid ID della coda messaggi
 */
void rimuovi_msgqueue(int msgid) {
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl IPC_RMID");
    }
}
