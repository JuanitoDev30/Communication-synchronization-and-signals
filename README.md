# Taller: señales en un servidor concurrente

Laboratorio de Sistemas Operativos — Universidad del Cauca

Servidor TCP concurrente que mantiene un tablero de texto compartido en memoria. Extiende la práctica anterior con manejo de señales, alarma periódica e informe de errores de llamadas al sistema.

## Compilar

```bash
cd src
make
```

## Ejecutar

```bash
./servidor_tablero sin|mutex|rwlock
```

- `sin` — sin protección sobre el tablero
- `mutex` — exclusión mutua total
- `rwlock` — varios lectores simultáneos, escritura exclusiva

## Cliente

```bash
./cliente_tablero <ip> lector|escritor <caracter> <repeticiones>
```

## Prueba de carga

```bash
./carga.sh [ip] [lectores] [lecturas_por_lector]
```

Lanza varios lectores y dos escritores en paralelo y reporta las lecturas incoherentes.

## Documentación

```bash
doxygen Doxyfile
xdg-open docs/html/index.html
```

## Autor

Juan Sebastian Molano — juanmolano@unicauca.edu.co
