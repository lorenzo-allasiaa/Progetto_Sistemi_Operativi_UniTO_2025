#include "common.h"

int main(int argc, char* argv[]) {

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <shmid> <semid> <msgid>\n", argv[0]);
        exit(1);
    }

    int shmid = atoi(argv[1]);
    int semid = atoi(argv[2]);
    int msgid = atoi(argv[3]);

    MemoriaCondivisa* shm = attach_shm(shmid);

    signal(SIGTERM, sigterm_handler);

    /* Segnala inizializzazione completata con semaforo SEM_INIT_BARRIER */
    sem_signal_wrapper(semid, SEM_INIT_BARRIER);

    int ticket_counter = 0;

    while (!termina && shm->simulazione_attiva) {
        MsgRichiesta richiesta;

        /* Attende di ricevere messaggi con mtype = 1 */
        ssize_t result = msgrcv(msgid, &richiesta, sizeof(MsgRichiesta) - sizeof(long), 1, 0);

        if (result > 0) {

            sem_wait_wrapper(semid, SEM_CODE_BASE + richiesta.servizio);

            int testa = shm->testa_coda[richiesta.servizio];
            int coda = shm->coda_coda[richiesta.servizio];

            int prossima_pos = (coda + 1) % MAX_USERS; /* Prossima posizione inserimento */

            if (prossima_pos == testa) {
                /* Coda piena: rifiuta richiesta */
                sem_signal_wrapper(semid, SEM_CODE_BASE + richiesta.servizio);

                dprintf(STDOUT_FILENO, "EROGATORE: Coda %s piena! Rifiuto ticket a utente %d\n", NOMI_SERVIZI[richiesta.servizio], richiesta.user_id);

                MsgTicket ticket;
                ticket.mtype = richiesta.user_id + 2;
                ticket.ticket_number = -2;
                ticket.servizio = richiesta.servizio;

                if (msgsnd(msgid, &ticket, sizeof(MsgTicket) - sizeof(long), 0) == -1) {
                    perror("msgsnd coda piena");
                    continue;
                }

                sem_wait_wrapper(semid, SEM_SHM);
                shm->stat_giornata.servizi_non_erogati++;
                shm->stat_totali.servizi_non_erogati++;
                shm->stat_giornata.per_servizio[richiesta.servizio].non_serviti++;
                shm->stat_totali.per_servizio[richiesta.servizio].non_serviti++;
                sem_signal_wrapper(semid, SEM_SHM);
                continue;
            }

            /* Coda ha spazio: eroga ticket positivo */
            MsgTicket ticket;
            ticket.mtype = richiesta.user_id + 2;
            ticket.ticket_number = ++ticket_counter;
            ticket.servizio = richiesta.servizio;

            if (msgsnd(msgid, &ticket, sizeof(MsgTicket) - sizeof(long), 0) == -1) {
                perror("msgsnd ticket");
                sem_signal_wrapper(semid, SEM_CODE_BASE + richiesta.servizio);
                continue;
            }

            dprintf(STDOUT_FILENO, "Utente %d: Erogatore stampa ticket %d per servizio %s\n",
                    richiesta.user_id,ticket.ticket_number,
                    NOMI_SERVIZI[richiesta.servizio]);

            /* Inserimento in coda */
            shm->code_servizi[richiesta.servizio][coda] = richiesta.user_id;
            shm->coda_coda[richiesta.servizio] = prossima_pos;

            sem_wait_wrapper(semid, SEM_SHM);
            shm->utenti_in_coda++;
            sem_signal_wrapper(semid, SEM_SHM);

            sem_signal_wrapper(semid, SEM_CODE_BASE + richiesta.servizio);

            /* Notifica agli operatori che è presente un utente in coda per questo servizio */
            sem_signal_wrapper(semid, SEM_UTENTI_PRESENTI_CODA_BASE + richiesta.servizio);
        }
    }

    detach_shm(shm);
    return 0;
}
