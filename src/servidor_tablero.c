/**
 * @file
 * @brief Servidor TCP concurrente con un recurso compartido entre lectores y escritores.
 *
 * El servidor mantiene un tablero de texto en memoria. Atiende cada conexion en un hilo
 * independiente, de modo que varios clientes operan sobre el tablero al mismo tiempo. La
 * politica de sincronizacion se elige al arrancar:
 *
 *   sin     - ninguna proteccion sobre el tablero
 *   mutex   - exclusion mutua total: un solo cliente a la vez, lea o escriba
 *   rwlock  - lectores y escritores: varias lecturas simultaneas, escritura exclusiva
 *
 * Protocolo de texto, una orden por linea:
 * @code
 *   LEER              -> OK <version> <contenido del tablero>
 *   ESCRIBIR <car>    -> OK <version>
 *   SALIR             -> cierra la conexion
 * @endcode
 *
 * @author Erwin Meza Vega <emezav@unicauca.edu.co>
 * @author Juan Sebastian Molano <juanmolano@unicauca.edu.co>
 * @copyright MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>




/** @brief Puerto en el que el servidor recibe conexiones. */
#define PUERTO 5500

/** @brief Numero de casillas del tablero compartido. */
#define TAM_TABLERO 64

/** @brief Tamano del buffer de ordenes y respuestas. */
#define TAM_BUFFER 256

/** @brief Microsegundos de espera entre casilla y casilla al escribir. */
#define RETARDO_ESCRITURA 2000

/** @brief Microsegundos de espera entre casilla y casilla al leer. */
#define RETARDO_LECTURA 200

/** @brief Segundos entre cada informe periodico del tablero. */
#define INTERVALO_ALARMA 5

/** @brief Politicas de sincronizacion disponibles. */
typedef enum {
    SIN_PROTECCION, /**< El tablero queda expuesto a accesos simultaneos. */
    CON_MUTEX,      /**< Un solo cliente accede al tablero, lea o escriba. */
    CON_RWLOCK      /**< Varios lectores a la vez, un escritor en exclusiva. */
} politica;

/* Recurso compartido: todas las casillas del tablero contienen el mismo caracter */
static char tablero[TAM_TABLERO];

/* Numero de la ultima escritura completada */
static int version_tablero;

/* Mecanismos de sincronizacion sobre el tablero */
static pthread_mutex_t candado = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t candado_rw = PTHREAD_RWLOCK_INITIALIZER;

/* Politica en uso durante esta ejecucion */
static politica politica_actual = SIN_PROTECCION;

/* Bandera activada por el manejador de SIGINT. Solo el manejador la escribe */
static volatile sig_atomic_t terminar = 0;

/* Bandera activada por el manejador de SIGALRM. Solo el manejador la escribe */
static volatile sig_atomic_t alarma = 0;

/* Contadores de operaciones despachadas. Los hilos los escriben, main los lee al salir */

static int lecturas_atendidas = 0;
static int escrituras_atendidas = 0;

/* Protege los dos contadores. Nunca se toma junto con candado ni candado_rw  */
static pthread_mutex_t candado_contadores = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Manejador de SIGINT. Solo registra la solicitud de terminar.
 *
 * Dentro de un manejador no se pueden llamar funciones que no sean seguras ante senales
 * (printf, malloc, pthread_mutex_lock), porque la senal puede llegar en medio de ellas.
 * El trabajo se realiza en el ciclo principal, que observa la bandera.
 * @param senal Numero de la senal recibida.
 */
static void al_interrumpir(int senal) {
    (void) senal;
    terminar = 1;
}

/**
 * @brief Manejador de SIGALRM. Solo registra que hay un informe pendiente.
 * @param senal Numero de la senal recibida.
 */
static void al_alarmar(int senal) {
    (void) senal;
    alarma = 1;
}

/**
 * @brief Traduce el nombre de la politica recibido por linea de comandos.
 * @param nombre Cadena con el nombre de la politica.
 * @param destino Apuntador a la variable que recibe la politica reconocida.
 * @return 1 si el nombre corresponde a una politica conocida, 0 en caso contrario.
 */
int reconocer_politica(const char *nombre, politica *destino);

/**
 * @brief Crea el socket de escucha del servidor.
 * @param puerto Puerto en el que se reciben las conexiones.
 * @return Descriptor del socket de escucha, o -1 si ocurre un error.
 */
int crear_socket_escucha(int puerto);

/**
 * @brief Atiende las ordenes de un cliente hasta que cierra la conexion.
 * @param arg Apuntador al descriptor del socket del cliente, reservado por el llamador.
 * @return NULL siempre.
 */
void *atender_cliente(void *arg);

/**
 * @brief Lee una linea terminada en salto de linea desde un socket.
 * @param socket_cliente Descriptor del socket.
 * @param destino Buffer que recibe la linea, sin el salto de linea final.
 * @param tam Tamano del buffer de destino.
 * @return Numero de caracteres leidos, o -1 si el cliente cerro la conexion o hubo error.
 */
int leer_linea(int socket_cliente, char *destino, int tam);

/**
 * @brief Copia el contenido del tablero aplicando la politica de sincronizacion vigente.
 * @param destino Buffer que recibe la copia, con al menos TAM_TABLERO + 1 caracteres.
 * @return Version del tablero observada durante la lectura.
 */
int leer_tablero(char *destino);

/**
 * @brief Llena el tablero con un caracter aplicando la politica de sincronizacion vigente.
 * @param caracter Caracter que se escribe en todas las casillas.
 * @return Version del tablero despues de la escritura.
 */
int escribir_tablero(char caracter);

/**
 * @brief Registra una operacion en el contador correspondiente (hilo-seguro).
 * @param es_escritura 1 si la orden es ESCRIBIR, 0 si es LEER.
 */
void contar_operacion(int es_escritura);
/**
 * @brief Lee ambos contadores en una sola seccion critica para garantizar coherencia.
 * @param lecturas  Recibe el total de lecturas atendidas.
 * @param escrituras Recibe el total de escrituras atendidas.
 */
void obtener_contadores(int *lecturas, int *escrituras);

int main(int argc, char *argv[]) {
    int socket_escucha;
    int socket_cliente;
    int *descriptor;
    pthread_t hilo;
    struct sigaction accion;

    if (argc != 2 || !reconocer_politica(argv[1], &politica_actual)) {
        fprintf(stderr, "Uso: %s sin|mutex|rwlock\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Un cliente que cierra de golpe no debe terminar el servidor */
    signal(SIGPIPE, SIG_IGN);

    /* SA_RESTART queda fuera a proposito: accept debe retornar para que el ciclo vea la bandera */
    memset(&accion, 0, sizeof(accion));
    accion.sa_handler = al_interrumpir;
    sigemptyset(&accion.sa_mask);
    accion.sa_flags = 0;
    if (sigaction(SIGINT, &accion, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    accion.sa_handler = al_alarmar;
    if (sigaction(SIGALRM, &accion, NULL) == -1) {
        perror("sigaction SIGALRM");
        return EXIT_FAILURE;
    }

    memset(tablero, '.', TAM_TABLERO);
    version_tablero = 0;

    socket_escucha = crear_socket_escucha(PUERTO);
    if (socket_escucha == -1) {
        return EXIT_FAILURE;
    }

    printf("Tablero de %d casillas, politica %s, escuchando en el puerto %d\n",
           TAM_TABLERO, argv[1], PUERTO);
    fflush(stdout);

    /* La primera alarma arranca una vez que el servidor esta listo para atender */
    alarm(INTERVALO_ALARMA);

    while (!terminar) {
        if (alarma) {
            char copia_tablero[TAM_TABLERO + 1];
            int ver = leer_tablero(copia_tablero);
            printf("[informe] version %d: %s\n", ver, copia_tablero);
            fflush(stdout);
            alarma = 0;
            alarm(INTERVALO_ALARMA);
        } 

        socket_cliente = accept(socket_escucha, NULL, NULL);
        if (socket_cliente == -1) {
            if (errno != EINTR) {
                perror("accept");
            }
            continue;
        }

        /* El hilo recibe su propia copia del descriptor y la libera al terminar */
        descriptor = malloc(sizeof(int));
        if (descriptor == NULL) {
            close(socket_cliente);
            continue;
        }
        *descriptor = socket_cliente;

        /* Bloquear SIGALRM antes de crear el hilo: el hijo hereda la mascara y nunca
           recibe la senal, garantizando que los informes siempre lleguen a main */
        {
            sigset_t mascara;
            sigemptyset(&mascara);
            sigaddset(&mascara, SIGALRM);
            pthread_sigmask(SIG_BLOCK, &mascara, NULL);

            if (pthread_create(&hilo, NULL, atender_cliente, descriptor) != 0) {
                perror("pthread_create");
                pthread_sigmask(SIG_UNBLOCK, &mascara, NULL);
                free(descriptor);
                close(socket_cliente);
                continue;
            }

            pthread_sigmask(SIG_UNBLOCK, &mascara, NULL);
        }

        /* Nadie espera el resultado del hilo: sus recursos se liberan al terminar */
        pthread_detach(hilo);
    }

    close(socket_escucha);
    {
        int lecturas;
        int escrituras;

        obtener_contadores(&lecturas, &escrituras);
        printf("Servidor detenido. Lecturas atendidas: %d, escrituras atendidas: %d\n", lecturas, escrituras);
    }
    return 0;
}

int reconocer_politica(const char *nombre, politica *destino) {
    if (strcmp(nombre, "sin") == 0) {
        *destino = SIN_PROTECCION;
        return 1;
    }
    if (strcmp(nombre, "mutex") == 0) {
        *destino = CON_MUTEX;
        return 1;
    }
    if (strcmp(nombre, "rwlock") == 0) {
        *destino = CON_RWLOCK;
        return 1;
    }
    return 0;
}

int crear_socket_escucha(int puerto) {
    int socket_escucha;
    int reutilizar = 1;
    struct sockaddr_in direccion;

    socket_escucha = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_escucha == -1) {
        perror("socket");
        return -1;
    }

    /* El puerto queda disponible de inmediato al terminar el servidor */
    setsockopt(socket_escucha, SOL_SOCKET, SO_REUSEADDR, &reutilizar, sizeof(reutilizar));

    memset(&direccion, 0, sizeof(direccion));
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion.sin_port = htons(puerto);

    if (bind(socket_escucha, (struct sockaddr *) &direccion, sizeof(direccion)) == -1) {
        perror("bind");
        close(socket_escucha);
        return -1;
    }

    if (listen(socket_escucha, 16) == -1) {
        perror("listen");
        close(socket_escucha);
        return -1;
    }

    return socket_escucha;
}

void *atender_cliente(void *arg) {
    int socket_cliente = *((int *) arg);
    char orden[TAM_BUFFER];
    char copia[TAM_TABLERO + 1];
    char respuesta[TAM_BUFFER];
    int version;
    int longitud;

    free(arg);

    while (1) {
        longitud = leer_linea(socket_cliente, orden, TAM_BUFFER);
        if (longitud == -1) {
            break;
        }

       if (strcmp(orden, "LEER") == 0) {
            contar_operacion(0);
            version = leer_tablero(copia);
            snprintf(respuesta, TAM_BUFFER, "OK %d %s\n", version, copia);
        } else if (strncmp(orden, "ESCRIBIR ", 9) == 0 && orden[9] != '\0') {
            contar_operacion(1);
            version = escribir_tablero(orden[9]);
            snprintf(respuesta, TAM_BUFFER, "OK %d\n", version);
        } else if (strcmp(orden, "SALIR") == 0) {
            break;
        } else {
            snprintf(respuesta, TAM_BUFFER, "ERROR orden no reconocida\n");
        }

        if (write(socket_cliente, respuesta, strlen(respuesta)) == -1) {
            break;
        }
    }

    close(socket_cliente);
    return NULL;
}

int leer_linea(int socket_cliente, char *destino, int tam) {
    int total = 0;
    char caracter;
    ssize_t leidos;

    while (total < tam - 1) {
        leidos = read(socket_cliente, &caracter, 1);

        /* El cliente cerro la conexion: una linea a medias no se procesa */
        if (leidos <= 0) {
            return -1;
        }
        if (caracter == '\n') {
            break;
        }
        if (caracter != '\r') {
            destino[total] = caracter;
            total++;
        }
    }

    destino[total] = '\0';
    return total;
}

int leer_tablero(char *destino) {
    int i;
    int version;

    if (politica_actual == CON_MUTEX) {
        pthread_mutex_lock(&candado);
    } else if (politica_actual == CON_RWLOCK) {
        pthread_rwlock_rdlock(&candado_rw);
    }

    /* La copia casilla por casilla amplia la ventana en la que otro hilo puede intervenir */
    for (i = 0; i < TAM_TABLERO; i++) {
        destino[i] = tablero[i];
        usleep(RETARDO_LECTURA);
    }
    destino[TAM_TABLERO] = '\0';
    version = version_tablero;

    if (politica_actual == CON_MUTEX) {
        pthread_mutex_unlock(&candado);
    } else if (politica_actual == CON_RWLOCK) {
        pthread_rwlock_unlock(&candado_rw);
    }

    return version;
}

int escribir_tablero(char caracter) {
    int i;
    int version;

    if (politica_actual == CON_MUTEX) {
        pthread_mutex_lock(&candado);
    } else if (politica_actual == CON_RWLOCK) {
        pthread_rwlock_wrlock(&candado_rw);
    }

    for (i = 0; i < TAM_TABLERO; i++) {
        tablero[i] = caracter;
        usleep(RETARDO_ESCRITURA);
    }
    version_tablero++;
    version = version_tablero;

    if (politica_actual == CON_MUTEX) {
        pthread_mutex_unlock(&candado);
    } else if (politica_actual == CON_RWLOCK) {
        pthread_rwlock_unlock(&candado_rw);
    }

    return version;
}


void contar_operacion(int es_escritura){
    pthread_mutex_lock(&candado_contadores);
    if(es_escritura){
        escrituras_atendidas++;
    } else {
        lecturas_atendidas++;
    }
    pthread_mutex_unlock(&candado_contadores);
}

void obtener_contadores(int *lecturas, int *escrituras){
    pthread_mutex_lock(&candado_contadores);
    *lecturas = lecturas_atendidas;
    *escrituras = escrituras_atendidas;
    pthread_mutex_unlock(&candado_contadores);
}