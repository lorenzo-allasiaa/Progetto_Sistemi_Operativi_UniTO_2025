#include "common.h"

int trova_sportello_libero(MemoriaCondivisa* shm, int semid, TipoServizio mansione, int operatore_id);
int servi_cliente(MemoriaCondivisa* shm, int semid, int sportello_id, TipoServizio servizio);

int main(int argc, char* argv[]) {

    if (argc != 6) {
        fprintf(stderr, "Uso: %s <shmid> <semid> <operatore_id> <nof_worker_seats> <nof_pause>\n", argv[0]);
        exit(1);
    }

    int shmid = atoi(argv[1]);
    int semid = atoi(argv[2]);
    int operatore_id = atoi(argv[3]);
    NOF_WORKER_SEATS = atoi(argv[4]);
    NOF_PAUSE = atoi(argv[5]);

    srand(time(NULL) + getpid() + operatore_id);

    MemoriaCondivisa* shm = attach_shm(shmid);
    signal(SIGTERM, sigterm_handler);

    TipoServizio mansione = rand() % NUM_SERVIZI;

    sem_wait_wrapper(semid, SEM_SHM);
    shm->mansione_operatori[operatore_id] = mansione;
    shm->pause_operatori[operatore_id] = 0;
    sem_signal_wrapper(semid, SEM_SHM);

    /* Segnala inizializzazione completata con semaforo */
    sem_signal_wrapper(semid, SEM_INIT_BARRIER);

    int sportello_occupato = -1;

    while (!termina && shm->simulazione_attiva) {

        /* Attende segnale di inizio giornata dal direttore */
        sem_wait_wrapper(semid, SEM_EVENTO_GIORNO_OPERATORI);

        if (!shm->simulazione_attiva) break;

        sportello_occupato = -1;
        sportello_occupato = trova_sportello_libero(shm, semid, mansione, operatore_id);

        /* Segnala al direttore che ha completato il tentativo di occupazione sportello */
        sem_signal_wrapper(semid, SEM_OPERATORI_PRONTI);

        /* Se non ha trovato sportello, si blocca aspettando che si liberi uno sportello con quel servizio */
        while (sportello_occupato == -1 && shm->simulazione_attiva) {

            sem_wait_wrapper(semid, SEM_SHM);
            int giornata_finita = shm->termine_giornata;
            sem_signal_wrapper(semid, SEM_SHM);

            if (giornata_finita) {
                break;
            }

            /* Si blocca aspettando che uno sportello si liberi */
            sem_wait_wrapper(semid, SEM_SPORTELLO_DISPONIBILE);

            sem_wait_wrapper(semid, SEM_SHM);
            giornata_finita = shm->termine_giornata;
            sem_signal_wrapper(semid, SEM_SHM);

            if (giornata_finita) {
                break;
            }

            sportello_occupato = trova_sportello_libero(shm, semid, mansione, operatore_id);
        }

        if (sportello_occupato != -1) {
            /* Sportello trovato: registra operatore per la giornata */
            sem_wait_wrapper(semid, SEM_SHM);

            shm->stat_giornata.operatori_attivi++;
            if (!shm->operatori_gia_contati_globale[operatore_id]) {
                shm->stat_totali.operatori_attivi++;
                shm->operatori_gia_contati_globale[operatore_id] = 1;
            }

            sem_signal_wrapper(semid, SEM_SHM);
            dprintf(STDOUT_FILENO, "Operatore %d: occupato sportello %d per %s\n", operatore_id, sportello_occupato, NOMI_SERVIZI[mansione]);

            /* Serve clienti fino a fine giornata o pausa */
            while (shm->simulazione_attiva) {

                sem_wait_wrapper(semid, SEM_SHM);
                int giornata_finita = shm->termine_giornata;
                sem_signal_wrapper(semid, SEM_SHM);

                if (giornata_finita) {
                    break;
                }

                int ha_servito = servi_cliente(shm, semid, sportello_occupato, mansione);

                /* Se non ha servito nessuno allora si blocca sul semaforo aspettando che utenti entrino in coda */
                if (!ha_servito) {
                    sem_wait_wrapper(semid, SEM_UTENTI_PRESENTI_CODA_BASE + mansione);

                    sem_wait_wrapper(semid, SEM_SHM);
                    giornata_finita = shm->termine_giornata;
                    sem_signal_wrapper(semid, SEM_SHM);

                    if (giornata_finita) {
                        break;
                    }

                    continue;
                }

                sem_wait_wrapper(semid, SEM_SHM);
                giornata_finita = shm->termine_giornata;
                sem_signal_wrapper(semid, SEM_SHM);

                if (giornata_finita) {
                    break;
                }

                /* Ha servito un cliente: verifica pausa anticipata (probabilità 5% ) */
                if (shm->pause_operatori[operatore_id] < NOF_PAUSE && rand() % 100 < 5) {

                    dprintf(STDOUT_FILENO, "Operatore %d: pausa anticipata\n", operatore_id);

                    sem_wait_wrapper(semid, SEM_SPORTELLI);
                    shm->sportelli[sportello_occupato].occupato = 0;
                    shm->sportelli[sportello_occupato].operatore_id = -1;
                    sem_signal_wrapper(semid, SEM_SPORTELLI);

                    /* Notifica agli operatori in attesa sul semaforo che uno sportello è disponibile */
                    sem_signal_wrapper(semid, SEM_SPORTELLO_DISPONIBILE);

                    sem_wait_wrapper(semid, SEM_SHM);
                    shm->pause_operatori[operatore_id]++;
                    shm->stat_giornata.pause_effettuate++;
                    shm->stat_totali.pause_effettuate++;
                    sem_signal_wrapper(semid, SEM_SHM);

                    sportello_occupato = -1;
                    break;
                }
            }

            /* Rilascia sportello per fine giornata */
            if (sportello_occupato != -1) {
                sem_wait_wrapper(semid, SEM_SPORTELLI);
                shm->sportelli[sportello_occupato].occupato = 0;
                shm->sportelli[sportello_occupato].operatore_id = -1;
                sem_signal_wrapper(semid, SEM_SPORTELLI);
            }
        }

        /* Attende segnale di fine giornata dal direttore */
        sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);

        /* Segnala al direttore che ha completato la giornata in modo che ora può stampare statistiche corrette */
        sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
    }

    detach_shm(shm);
    return 0;
}


/**
 * Cerca sportello libero compatibile con mansione
 * Restituisce indice sportello o -1 se nessuno disponibile
 */
int trova_sportello_libero(MemoriaCondivisa* shm, int semid, TipoServizio mansione, int operatore_id) {
    
    int sportello_trovato = -1;
    sem_wait_wrapper(semid, SEM_SPORTELLI);

    for (int i = 0; i < NOF_WORKER_SEATS; i++) {  
        if (!shm->sportelli[i].occupato && shm->sportelli[i].servizio == mansione) {
            shm->sportelli[i].occupato = 1;
            shm->sportelli[i].operatore_id = operatore_id;
            sportello_trovato = i;
            break;
        }
    }

    sem_signal_wrapper(semid, SEM_SPORTELLI);
    return sportello_trovato;
}

/**
 * Tenta di servire un cliente dalla coda del servizio, aggiornando le statistiche se utente è viene servito
 * Restituisce 1 se ha servito un cliente, 0 se coda vuota
 */
int servi_cliente(MemoriaCondivisa* shm, int semid, int sportello_id, TipoServizio servizio) {

    sem_wait_wrapper(semid, SEM_CODE_BASE + servizio);
    int testa = shm->testa_coda[servizio];
    int coda = shm->coda_coda[servizio];

    /* se coda non vuota */
    if (testa != coda) {

        int user_id = shm->code_servizi[servizio][testa];

        /* Salta utenti che hanno abbandonato la coda per fine giornata (marcati con -1) */
        while (user_id == -1 && testa != coda) {
            shm->testa_coda[servizio] = (testa + 1) % MAX_USERS;
            testa = shm->testa_coda[servizio];
            if (testa != coda) {
                user_id = shm->code_servizi[servizio][testa];
            } else {
                break;
            }
        }

        if (testa == coda || user_id == -1) {
            sem_signal_wrapper(semid, SEM_CODE_BASE + servizio);
            return 0;
        }

        /* Consuma utente in coda e quindi avanza testa_coda */
        shm->testa_coda[servizio] = (testa + 1) % MAX_USERS;

        sem_wait_wrapper(semid, SEM_SHM);
        shm->utenti_in_coda--;
        sem_signal_wrapper(semid, SEM_SHM);

        sem_signal_wrapper(semid, SEM_CODE_BASE + servizio);

        int tempo_base = TEMPI_SERVIZI[servizio];
        int tempo_min = tempo_base / 2;
        int tempo_max = tempo_base + tempo_base / 2;
        int tempo_erogazione = random_range(tempo_min, tempo_max);

        struct timespec ts_completamento_attesa;
        clock_gettime(CLOCK_MONOTONIC, &ts_completamento_attesa);
        long timestamp_completamento_ns = ts_completamento_attesa.tv_sec * 1000000000L + ts_completamento_attesa.tv_nsec;

        sem_wait_wrapper(semid, SEM_SHM);
        shm->timestamp_attesa_completata[user_id] = timestamp_completamento_ns;
        sem_signal_wrapper(semid, SEM_SHM);

        /* Simula erogazione servizio */
        simula_tempo_minuti(tempo_erogazione);
        dprintf(STDOUT_FILENO, "Utente %d: servito correttamente\n", user_id);

        sem_wait_wrapper(semid, SEM_SHM);

        shm->stat_giornata.utenti_serviti++;
        shm->stat_totali.utenti_serviti++;
        shm->stat_giornata.servizi_erogati++;
        shm->stat_totali.servizi_erogati++;
        shm->stat_giornata.tempo_erogazione_totale += tempo_erogazione;
        shm->stat_totali.tempo_erogazione_totale += tempo_erogazione;

        shm->stat_giornata.per_servizio[servizio].serviti++;
        shm->stat_totali.per_servizio[servizio].serviti++;
        shm->stat_giornata.per_servizio[servizio].tempo_erogazione_totale += tempo_erogazione;
        shm->stat_totali.per_servizio[servizio].tempo_erogazione_totale += tempo_erogazione;

        sem_signal_wrapper(semid, SEM_SHM);
        return 1;

    } else {
        sem_signal_wrapper(semid, SEM_CODE_BASE + servizio);
        return 0;
    }
}