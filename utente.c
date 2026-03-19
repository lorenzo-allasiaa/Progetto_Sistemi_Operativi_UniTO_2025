#include "common.h"

int main(int argc, char* argv[]) {

    if (argc != 7) {
        fprintf(stderr, "Uso: %s <shmid> <semid> <msgid> <user_id> <n_nano_secs> <nof_worker_seats>\n", argv[0]);
        exit(1);
    }

    int shmid = atoi(argv[1]);
    int semid = atoi(argv[2]);
    int msgid = atoi(argv[3]);
    int user_id = atoi(argv[4]);
    N_NANO_SECS = atoi(argv[5]);
    NOF_WORKER_SEATS = atoi(argv[6]);

    srand(time(NULL) + getpid() + user_id);

    MemoriaCondivisa* shm = attach_shm(shmid);
    signal(SIGTERM, sigterm_handler);

    double prob_servizio = random_double(shm->p_serv_min, shm->p_serv_max);

    /* Segnala inizializzazione completata con semaforo */
    sem_signal_wrapper(semid, SEM_INIT_BARRIER);

    int giorno_corrente = -1;
    int in_attesa = 0;
    struct timespec inizio_attesa = {0};
    TipoServizio servizio_richiesto;

    while (!termina && shm->simulazione_attiva) {

        /* Attende segnale di inizio giornata per utenti dal direttore */
        sem_wait_wrapper(semid, SEM_EVENTO_GIORNO_UTENTI);

        if (!shm->simulazione_attiva) break;

        sem_wait_wrapper(semid, SEM_SHM);
        giorno_corrente = shm->giorno_corrente;
        sem_signal_wrapper(semid, SEM_SHM);

        double r = random_double(0.0, 1.0);

        if (r < prob_servizio) {
            /* Decide di recarsi all'ufficio */

            servizio_richiesto = rand() % NUM_SERVIZI;

            /* Attende tempo casuale tra 0 e 399 minuti per simulare arrivo in diversi momenti della giornata */
            int minuti_attesa = rand() % 400;
            simula_tempo_minuti(minuti_attesa);

            sem_wait_wrapper(semid, SEM_SHM);
            int giornata_finita = shm->termine_giornata;
            sem_signal_wrapper(semid, SEM_SHM);

            if (giornata_finita) {
                sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
                sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
                continue;
            }

            /* Verifica disponibilità servizio prima di inviare richiesta */
            int servizio_disponibile = 0;
            sem_wait_wrapper(semid, SEM_SPORTELLI);
            for (int i = 0; i < NOF_WORKER_SEATS; i++) {
                if (shm->sportelli[i].servizio == servizio_richiesto &&
                    shm->sportelli[i].occupato == 1) {
                    servizio_disponibile = 1;
                    break;
                }
            }
            sem_signal_wrapper(semid, SEM_SPORTELLI);

            if (!servizio_disponibile) {
                /* Servizio non disponibile: aggiorna statistiche senza inviare richiesta all'erogatore */

                dprintf(STDOUT_FILENO, "Utente %d: servizio %s non disponibile oggi\n", user_id, NOMI_SERVIZI[servizio_richiesto]);

                sem_wait_wrapper(semid, SEM_SHM);
                shm->stat_giornata.servizi_non_erogati++;
                shm->stat_totali.servizi_non_erogati++;
                shm->stat_giornata.per_servizio[servizio_richiesto].non_serviti++;
                shm->stat_totali.per_servizio[servizio_richiesto].non_serviti++;
                sem_signal_wrapper(semid, SEM_SHM);

                sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
                sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
                continue;
            }

            /* Servizio disponibile: procedi con richiesta */

            dprintf(STDOUT_FILENO, "Utente %d (giorno %d): richiede servizio %s\n", user_id, giorno_corrente + 1, NOMI_SERVIZI[servizio_richiesto]);

            MsgRichiesta richiesta;
            richiesta.mtype = 1;
            richiesta.user_id = user_id;
            richiesta.servizio = servizio_richiesto;

            if (msgsnd(msgid, &richiesta, sizeof(MsgRichiesta) - sizeof(long), 0) == -1) {
                perror("msgsnd richiesta");
                sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
                sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
                continue;
            }

            /* Riceve o aspetta di ricevere ticket dall'erogatore (mtype=user_id+2) */
            MsgTicket ticket;
            if (msgrcv(msgid, &ticket, sizeof(MsgTicket) - sizeof(long), user_id + 2, 0) > 0) {

                if (ticket.ticket_number == -2) {
                    /* Coda piena */

                    dprintf(STDOUT_FILENO, "Utente %d: coda per %s è PIENA, riprovo domani\n", user_id, NOMI_SERVIZI[servizio_richiesto]);
                    sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
                    sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);

                } else {
                    /* Ticket valido */
                    in_attesa = 1;
                    clock_gettime(CLOCK_MONOTONIC, &inizio_attesa);
                }
            }
        } else {
            /* Non si reca all'ufficio oggi, attende fine giornata */
            sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);
            sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
        }

        /* Se in attesa, aspetta la fine giornata */
        if (in_attesa) {

            sem_wait_wrapper(semid, SEM_EVENTO_FINE_GIORNATA);

            sem_wait_wrapper(semid, SEM_SHM);
            long timestamp_completamento = shm->timestamp_attesa_completata[user_id];
            sem_signal_wrapper(semid, SEM_SHM);

            if (timestamp_completamento == 0) {
                /* Timestamp non impostato = utente non servito e quindi abbandona coda */
                
                dprintf(STDOUT_FILENO, "Utente %d: abbandono per fine giornata\n", user_id);

                sem_wait_wrapper(semid, SEM_CODE_BASE + servizio_richiesto);

                int testa = shm->testa_coda[servizio_richiesto];
                int coda = shm->coda_coda[servizio_richiesto];

                /* Marca se stesso con -1 nella coda */
                for (int i = testa; i != coda; i = (i + 1) % MAX_USERS) {
                    if (shm->code_servizi[servizio_richiesto][i] == user_id) {
                        shm->code_servizi[servizio_richiesto][i] = -1;
                        break;
                    }
                }
                sem_signal_wrapper(semid, SEM_CODE_BASE + servizio_richiesto);

                sem_wait_wrapper(semid, SEM_SHM);
                shm->stat_giornata.servizi_non_erogati++;
                shm->stat_totali.servizi_non_erogati++;
                shm->stat_giornata.per_servizio[servizio_richiesto].non_serviti++;
                shm->stat_totali.per_servizio[servizio_richiesto].non_serviti++;
                shm->utenti_in_coda--;
                sem_signal_wrapper(semid, SEM_SHM);

            } else {
                /* Timestamp impostato = utente servito e quindi calcola tempo attesa corretto */

                long inizio_attesa_ns = inizio_attesa.tv_sec * 1000000000L + inizio_attesa.tv_nsec;
                long nano_attesa = timestamp_completamento - inizio_attesa_ns;
                long minuti_attesa = nano_attesa / N_NANO_SECS;
                
                sem_wait_wrapper(semid, SEM_SHM);
                shm->stat_giornata.tempo_attesa_totale += minuti_attesa;
                shm->stat_totali.tempo_attesa_totale += minuti_attesa;

                shm->stat_giornata.per_servizio[servizio_richiesto].tempo_attesa_totale += minuti_attesa;
                shm->stat_totali.per_servizio[servizio_richiesto].tempo_attesa_totale += minuti_attesa;
                sem_signal_wrapper(semid, SEM_SHM);
            }

            /* Segnala completamento aggiornamenti statistiche al direttore */
            sem_signal_wrapper(semid, SEM_FINE_AGGIORNAMENTI);
            in_attesa = 0;
        }
    }

    detach_shm(shm);
    return 0;
}
