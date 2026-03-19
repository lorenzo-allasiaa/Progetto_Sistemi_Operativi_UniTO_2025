CC = gcc
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE

all: direttore erogatore operatore utente

direttore: direttore.o common.o
	$(CC) $(CFLAGS) -o direttore direttore.o common.o

erogatore: erogatore.o common.o
	$(CC) $(CFLAGS) -o erogatore erogatore.o common.o

operatore: operatore.o common.o
	$(CC) $(CFLAGS) -o operatore operatore.o common.o

utente: utente.o common.o
	$(CC) $(CFLAGS) -o utente utente.o common.o

direttore.o: direttore.c common.h
	$(CC) $(CFLAGS) -c direttore.c

erogatore.o: erogatore.c common.h
	$(CC) $(CFLAGS) -c erogatore.c

operatore.o: operatore.c common.h
	$(CC) $(CFLAGS) -c operatore.c

utente.o: utente.c common.h
	$(CC) $(CFLAGS) -c utente.c

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

# Target clean: rimuove esegubili e file oggetto
clean:
	rm -f *.o direttore erogatore operatore utente
