/**
 * @file
 * @brief Cliente de carga para el servidor de tablero compartido.
 *
 * Se conecta por TCP al servidor y repite una operacion un numero determinado de veces.
 * En modo lector comprueba cada respuesta: el tablero es coherente cuando todas sus
 * casillas contienen el mismo caracter, de modo que una respuesta con caracteres
 * mezclados corresponde a una lectura tomada mientras un escritor estaba a mitad de su
 * trabajo. Al terminar informa cuantas lecturas resultaron incoherentes y cuanto tiempo
 * tomo la serie completa.
 *
 * @author Erwin Meza Vega <emezav@unicauca.edu.co>
 * @copyright MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/** @brief Puerto en el que atiende el servidor. */
#define PUERTO 5500

/** @brief Tamano del buffer de ordenes y respuestas. */
#define TAM_BUFFER 256

/**
 * @brief Establece la conexion con el servidor.
 * @param direccion_ip Direccion IPv4 del servidor, en notacion decimal punteada.
 * @param puerto Puerto en el que atiende el servidor.
 * @return Descriptor del socket conectado, o -1 si ocurre un error.
 */
int conectar(const char *direccion_ip, int puerto);

/**
 * @brief Envia una orden al servidor y recibe la respuesta completa.
 * @param socket_servidor Descriptor del socket conectado.
 * @param orden Orden que se envia, sin el salto de linea final.
 * @param respuesta Buffer que recibe la respuesta, sin el salto de linea final.
 * @param tam Tamano del buffer de respuesta.
 * @return 1 si la respuesta se recibio completa, 0 en caso contrario.
 */
int enviar_orden(int socket_servidor, const char *orden, char *respuesta, int tam);

/**
 * @brief Determina si el contenido del tablero es coherente.
 * @param contenido Cadena con las casillas del tablero.
 * @return 1 si todas las casillas contienen el mismo caracter, 0 en caso contrario.
 */
int es_coherente(const char *contenido);

/**
 * @brief Extrae el contenido del tablero de una respuesta con formato OK version contenido.
 * @param respuesta Respuesta recibida del servidor.
 * @return Apuntador al contenido dentro de la respuesta, o NULL si el formato no corresponde.
 */
const char *extraer_contenido(const char *respuesta);

/**
 * @brief Calcula los segundos transcurridos entre dos instantes.
 * @param inicio Instante inicial.
 * @param fin Instante final.
 * @return Segundos transcurridos, con parte decimal.
 */
double segundos_entre(struct timespec inicio, struct timespec fin);

int main(int argc, char *argv[]) {
    int socket_servidor;
    int repeticiones;
    int i;
    int incoherentes = 0;
    int es_lector;
    char orden[TAM_BUFFER];
    char respuesta[TAM_BUFFER];
    const char *contenido;
    struct timespec inicio;
    struct timespec fin;

    if (argc != 5) {
        fprintf(stderr, "Uso: %s <direccion_ip> lector|escritor <caracter> <repeticiones>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    es_lector = (strcmp(argv[2], "lector") == 0);
    if (!es_lector && strcmp(argv[2], "escritor") != 0) {
        fprintf(stderr, "El modo debe ser lector o escritor\n");
        return EXIT_FAILURE;
    }

    repeticiones = atoi(argv[4]);
    if (repeticiones <= 0) {
        fprintf(stderr, "El numero de repeticiones debe ser mayor que cero\n");
        return EXIT_FAILURE;
    }

    socket_servidor = conectar(argv[1], PUERTO);
    if (socket_servidor == -1) {
        return EXIT_FAILURE;
    }

    if (es_lector) {
        snprintf(orden, TAM_BUFFER, "LEER");
    } else {
        snprintf(orden, TAM_BUFFER, "ESCRIBIR %c", argv[3][0]);
    }

    clock_gettime(CLOCK_MONOTONIC, &inicio);

    for (i = 0; i < repeticiones; i++) {
        if (!enviar_orden(socket_servidor, orden, respuesta, TAM_BUFFER)) {
            fprintf(stderr, "La conexion se interrumpio en la repeticion %d\n", i + 1);
            break;
        }

        if (es_lector) {
            contenido = extraer_contenido(respuesta);
            if (contenido != NULL && !es_coherente(contenido)) {
                incoherentes++;
                printf("Lectura %d incoherente: %s\n", i + 1, contenido);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &fin);

    enviar_orden(socket_servidor, "SALIR", respuesta, TAM_BUFFER);
    close(socket_servidor);

    if (es_lector) {
        printf("%d lecturas, %d incoherentes, %.3f segundos\n",
               i, incoherentes, segundos_entre(inicio, fin));
    } else {
        printf("%d escrituras del caracter %c, %.3f segundos\n",
               i, argv[3][0], segundos_entre(inicio, fin));
    }

    return 0;
}

int conectar(const char *direccion_ip, int puerto) {
    int socket_servidor;
    struct sockaddr_in direccion;

    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor == -1) {
        perror("socket");
        return -1;
    }

    memset(&direccion, 0, sizeof(direccion));
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(puerto);

    if (inet_pton(AF_INET, direccion_ip, &direccion.sin_addr) != 1) {
        fprintf(stderr, "Direccion IPv4 no valida: %s\n", direccion_ip);
        close(socket_servidor);
        return -1;
    }

    if (connect(socket_servidor, (struct sockaddr *) &direccion, sizeof(direccion)) == -1) {
        perror("connect");
        close(socket_servidor);
        return -1;
    }

    return socket_servidor;
}

int enviar_orden(int socket_servidor, const char *orden, char *respuesta, int tam) {
    char linea[TAM_BUFFER];
    int total = 0;
    char caracter;
    ssize_t leidos;

    snprintf(linea, TAM_BUFFER, "%s\n", orden);
    if (write(socket_servidor, linea, strlen(linea)) == -1) {
        return 0;
    }

    if (strcmp(orden, "SALIR") == 0) {
        return 1;
    }

    while (total < tam - 1) {
        leidos = read(socket_servidor, &caracter, 1);
        if (leidos <= 0) {
            return 0;
        }
        if (caracter == '\n') {
            break;
        }
        respuesta[total] = caracter;
        total++;
    }

    respuesta[total] = '\0';
    return 1;
}

int es_coherente(const char *contenido) {
    int i;

    for (i = 1; contenido[i] != '\0'; i++) {
        if (contenido[i] != contenido[0]) {
            return 0;
        }
    }

    return 1;
}

const char *extraer_contenido(const char *respuesta) {
    const char *espacio;

    /* La respuesta tiene la forma OK <version> <contenido> */
    if (strncmp(respuesta, "OK ", 3) != 0) {
        return NULL;
    }

    espacio = strchr(respuesta + 3, ' ');
    if (espacio == NULL) {
        return NULL;
    }

    return espacio + 1;
}

double segundos_entre(struct timespec inicio, struct timespec fin) {
    return (double) (fin.tv_sec - inicio.tv_sec)
           + (double) (fin.tv_nsec - inicio.tv_nsec) / 1000000000.0;
}
