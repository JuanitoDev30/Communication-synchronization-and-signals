# Makefile del tablero compartido
#
# make          compila el servidor y el cliente
# make clean    elimina los ejecutables

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -pthread

PROGRAMAS = servidor_tablero cliente_tablero

all: $(PROGRAMAS)

servidor_tablero: servidor_tablero.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

cliente_tablero: cliente_tablero.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGRAMAS)

.PHONY: all clean
