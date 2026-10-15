#!/bin/bash
#
# Lanza varios lectores y dos escritores contra el servidor de tablero, todos a la vez,
# y espera a que terminen. El servidor debe estar en ejecucion antes de invocarlo.
#
# Uso: ./carga.sh [direccion_ip] [numero_de_lectores] [lecturas_por_lector]

DIRECCION=${1:-127.0.0.1}
LECTORES=${2:-8}
LECTURAS=${3:-20}
ESCRITURAS=10

echo "Servidor en $DIRECCION, $LECTORES lectores con $LECTURAS lecturas cada uno"
echo

rm -f lector_*.txt escritor_*.txt

INICIO=$SECONDS

for i in $(seq 1 "$LECTORES"); do
    ./cliente_tablero "$DIRECCION" lector x "$LECTURAS" > "lector_$i.txt" 2>&1 &
done

./cliente_tablero "$DIRECCION" escritor A "$ESCRITURAS" > escritor_A.txt 2>&1 &
./cliente_tablero "$DIRECCION" escritor B "$ESCRITURAS" > escritor_B.txt 2>&1 &

wait

echo "Resumen de cada lector:"
grep -h "lecturas" lector_*.txt

TOTAL=$(cat lector_*.txt | grep -c "incoherente:")

echo
echo "Lecturas incoherentes en total: $TOTAL"
echo "Tiempo total de la serie: $((SECONDS - INICIO)) segundos"
