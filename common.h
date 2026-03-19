/**
 * Contiene tutte le strutture dati, costanti e definizioni condivise
 * tra i processi della simulazione.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/* Variabili di configurazione (lette da file .conf) */
extern int SIM_DURATION;
extern int NOF_WORKERS;
extern int NOF_USERS;
extern int NOF_WORKER_SEATS;
extern int N_NANO_SECS;
extern int NOF_PAUSE;
extern int EXPLODE_THRESHOLD;
extern double P_SERV_MIN;
extern double P_SERV_MAX;


#define MAX_WORKERS 250
#define MAX_USERS 500
#define MAX_SPORTELLI 120
#define NUM_SERVIZI 6


/* Enumerazione dei tipi di servizio disponibili */
typedef enum {
    PACCHI = 0,
    LETTERE = 1,
    BANCOPOSTA = 2,
    BOLLETTINI = 3,
    PRODOTTI_FIN = 4,
    OROLOGI = 5
} TipoServizio;

/* Tempi medi di erogazione per servizio */
static const int TEMPI_SERVIZI[NUM_SERVIZI] = {10, 8, 6, 8, 20, 20};


static const char* NOMI_SERVIZI[NUM_SERVIZI] = {
    "Pacchi", "Lettere", "Bancoposta", "Bollettini", "Prodotti Finanziari", "Orologi"
};


/**
 * Rappresenta uno sportello fisico dell'ufficio postale
 *
 * occupato:     0 = libero, 1 = occupato da un operatore
 * servizio:     tipo di servizio offerto oggi
 * operatore_id: id dell'operatore che occupa lo sportello (-1 se libero)
 */
typedef struct {
    int occupato;
    TipoServizio servizio;
    int operatore_id;
} Sportello;

/**
 * Statistiche per un singolo tipo di servizio
 *
 * serviti:                    numero utenti serviti per questo servizio
 * non_serviti:                numero utenti non serviti 
 * tempo_attesa_totale:        somma tempo attesa in minuti di tutti gli utenti
 * tempo_erogazione_totale:    somma tempo erogazione in minuti di tutti i servizi
 */
typedef struct {
    int serviti;
    int non_serviti;
    long tempo_attesa_totale;
    long tempo_erogazione_totale;
} StatServizio;

/**
 * Statistiche aggregate giornaliere o totali
 *
 * utenti_serviti:          numero totale utenti che hanno ricevuto servizio
 * servizi_erogati:         numero totale servizi erogati
 * servizi_non_erogati:     numero totale servizi non erogati
 * tempo_attesa_totale:     somma totale tempi attesa
 * tempo_erogazione_totale: somma totale tempi erogazione
 * operatori_attivi:        numero operatori attivi
 * pause_effettuate:        numero pause totali effettuate
 * per_servizio:            array di statistiche per ogni tipo di servizio
 */
typedef struct {
    int utenti_serviti;
    int servizi_erogati;
    int servizi_non_erogati;
    long tempo_attesa_totale;
    long tempo_erogazione_totale;
    int operatori_attivi;
    int pause_effettuate;
    StatServizio per_servizio[NUM_SERVIZI];
} StatGiornaliere;

/* Memoria condivisa tra tutti i processi */
typedef struct {

    int giorno_corrente;
    int simulazione_attiva;     /* Flag: 1 = attiva, 0 = terminata */
    int termine_giornata;       /* Flag: 1 = giornata terminata, 0 = in corso */
    int utenti_in_coda;         /* Contatore utenti totali in coda */

    double p_serv_min;
    double p_serv_max;

    Sportello sportelli[MAX_SPORTELLI];

    StatGiornaliere stat_totali;
    StatGiornaliere stat_giornata;

    /* Code circolari usate per memorizzare id degli utenti in coda */
    int code_servizi[NUM_SERVIZI][MAX_USERS];
    int testa_coda[NUM_SERVIZI];         /* Indici testa per ogni coda (estrazione) */
    int coda_coda[NUM_SERVIZI];          /* Indici coda per ogni coda (inserimento) */

    int pause_operatori[MAX_WORKERS];           /* Contatore pause per operatore */
    TipoServizio mansione_operatori[MAX_WORKERS]; /* Mansione fissa assegnata a ogni operatore */
    int operatori_gia_contati_globale[MAX_WORKERS]; /* Flag: 1 = operatore già contato nei totali */
    long timestamp_attesa_completata[MAX_USERS];  /* Timestamp completamento attesa per ogni utente */
} MemoriaCondivisa;


/**
 * Messaggio di risposta: erogatore -> utente
 *
 * mtype:               tipo messaggio ( uguale a user_id + 2)
 * ticket_number:       numero ticket assegnato (positivo) o codice errore: -2 = coda piena
 * servizio:            tipo servizio richiesto
 */
typedef struct {
    long mtype;
    int ticket_number;
    TipoServizio servizio;
} MsgTicket;

/**
 * Messaggio di richiesta: utente -> erogatore
 *
 * mtype:    tipo messaggio ( 1 per l'erogatore)
 * user_id:  id utente che fa la richiesta
 * servizio: tipo servizio richiesto
 */
typedef struct {
    long mtype;
    int user_id;
    TipoServizio servizio;
} MsgRichiesta;


#define KEY_SHM 1234
#define KEY_SEM 5678
#define KEY_MSG 9012

 /* Indici Semafori */
#define SEM_SHM 0                                       /* Utilizzato per accesso variabili generali della memoria condivisa */
#define SEM_SPORTELLI 1                                 /* Utilizzato per accesso array sportelli */
#define SEM_CODE_BASE 2                                 /* Indice base dei semafori riguardanti code circolari dei servizi (SEM_CODE_BASE + 0..5) */
#define SEM_INIT_BARRIER (SEM_CODE_BASE + NUM_SERVIZI)  /* Barriera inizializzazione processi */
#define SEM_EVENTO_GIORNO_OPERATORI (SEM_INIT_BARRIER + 1)   /* Evento inizio giornata per operatori */
#define SEM_EVENTO_GIORNO_UTENTI (SEM_EVENTO_GIORNO_OPERATORI + 1) /* Evento inizio giornata per utenti */
#define SEM_EVENTO_FINE_GIORNATA (SEM_EVENTO_GIORNO_UTENTI + 1) /* Evento fine giornata complessivo */
#define SEM_SPORTELLO_DISPONIBILE (SEM_EVENTO_FINE_GIORNATA + 1) /* Utilizzato per bloccare operatori che non trovano sportelli disponibili con quel servizio */
#define SEM_FINE_AGGIORNAMENTI (SEM_SPORTELLO_DISPONIBILE + 1) /* Barriera completamento aggiornamenti per statistiche utenti-operatori */
#define SEM_UTENTI_PRESENTI_CODA_BASE (SEM_FINE_AGGIORNAMENTI + 1) /* Utilizzato per bloccare operatori con sportello che non possono servire perchè la coda del servizio è vuota */
#define SEM_OPERATORI_PRONTI (SEM_UTENTI_PRESENTI_CODA_BASE + NUM_SERVIZI) /* Barriera: operatori hanno completato posizionamento */
#define NUM_SEM (SEM_OPERATORI_PRONTI + 1)      /* Numero totale semafori = 21 */ 


/* Funzioni semafori */
void sem_wait_wrapper(int semid, int sem_num);
void sem_signal_wrapper(int semid, int sem_num);
void sem_init_value(int semid, int sem_num, int value);

/* Funzioni configurazione e simulazione */
void leggi_configurazione(const char* filename);    /* Legge e valida file .conf */
int simula_tempo_minuti(int minuti_simulati);       /* Converte minuti simulati in nanosecondi */
double random_double(double min, double max);       /* Genera double casuale in [min, max] */
int random_range(int min, int max);                 /* Genera intero casuale in [min, max] */

/* Funzioni memoria condivisa */
int crea_shm(void);
MemoriaCondivisa* attach_shm(int shmid);
void detach_shm(MemoriaCondivisa* shm);
void rimuovi_shm(int shmid);

int crea_semafori(void);
void rimuovi_semafori(int semid);

int crea_msgqueue(void);
void rimuovi_msgqueue(int msgid);

/* Gestione segnali */
extern volatile sig_atomic_t termina;   /* Flag per terminazione processi */
void sigterm_handler(int signo);

#endif
