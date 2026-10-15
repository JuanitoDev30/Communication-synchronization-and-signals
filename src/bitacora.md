# Bitácora: señales en un servidor concurrente

**Autor:** Juan Sebastian Molano  
**Fecha:** octubre 2026  
**Política usada en las pruebas:** `rwlock`

---

## Punto 1 — Servidor sin modificar

Se corrió el servidor con política `rwlock`, se conectó un cliente lector con 2000 lecturas y se interrumpió con Ctrl-C.

El servidor no imprimió nada al terminar. Sin manejador instalado, SIGINT tiene la acción por defecto de matar el proceso de inmediato, sin cerrar el socket ni mostrar ningún conteo.

El cliente estaba en la lectura 124 cuando perdió la conexión, imprimió `La conexión se interrumpió en la repetición 124` y después murió por SIGPIPE al intentar enviar `SALIR` sobre el socket ya cerrado. Nunca mostró su resumen. La terminación brusca del servidor afectó directamente a los clientes conectados.

---

## Punto 4 — Protección de contadores

Se eligió un mutex independiente (`candado_contadores`), separado de los candados del tablero.

Con `rwlock` varios hilos lectores entran a `leer_tablero` al mismo tiempo. Si los contadores se incrementaran ahí sin protección, dos hilos podrían leer el mismo valor, sumarle uno cada uno y perder un incremento: la misma carrera que la práctica anterior midió sobre el tablero. El mutex propio lo evita sin interferir con la política del tablero.

`obtener_contadores` lee los dos valores en la misma sección crítica para que el par sea coherente al imprimirlo. Se descartó reusar la rwlock del tablero porque los contadores son solo escritura desde los hilos trabajadores, así que la semántica de múltiples lectores no aplica.

---

## Punto 6 — Efecto de cada señal

```bash
kill -ALRM $PID   # activa la bandera alarma: imprime el tablero y reprograma
kill -INT  $PID   # activa la bandera terminar: sale del ciclo, cierra y muestra contadores
kill -KILL $PID   # termina el proceso de forma incondicional
```

Los manejadores de `SIGALRM` y `SIGINT` solo activan una bandera porque dentro de un manejador no se puede llamar a `printf` ni tomar un mutex: la señal puede llegar en medio de cualquiera de esas operaciones. El trabajo real lo hace el ciclo principal cuando observa la bandera.

`SIGKILL` no se puede capturar ni ignorar. El kernel termina el proceso directamente, no ejecuta ningún manejador y el servidor pierde sus contadores. Intentar instalar un manejador con `sigaction` devuelve `EINVAL`. Que no se pueda capturar garantiza que siempre haya una forma de terminar un proceso, incluso si está en deadlock o su manejador tiene un bug.

---

## Punto 7 — Errores de llamadas al sistema

Todas las llamadas agregadas tienen su retorno verificado y usan `perror` para reportar el error junto con la descripción del sistema para `errno`.

Para probarlo se arrancó un segundo servidor con el primero ya escuchando en el puerto 5500:

```
bind: Address already in use
```

`bind` devuelve -1, `perror` imprime el mensaje y el servidor termina con `EXIT_FAILURE`. Sin esa verificación el error pasaría silencioso.

---

## Punto 8 — `signal(SIGPIPE, SIG_IGN)`

Cuando un cliente cierra la conexión de golpe, el siguiente `write` del servidor genera SIGPIPE. La acción por defecto es terminar el proceso entero, lo que tumbaría el servidor aunque hubiera otros clientes conectados.

Con `SIG_IGN` el `write` devuelve -1 con `errno == EPIPE`, el hilo cierra el socket del cliente y termina sin afectar a nadie más. Es lo mismo que le pasó al cliente en el punto 1 cuando intentó enviar `SALIR` después de que el servidor desapareció, solo que en ese caso no había nada que lo ignorara.

---
