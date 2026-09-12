# Roadmap de desarrollo de Azahar para PS Vita

## Objetivo

Crear un fork mantenible de Azahar capaz de ejecutar software de Nintendo 3DS en una PS Vita real,
comenzando con homebrew y avanzando gradualmente hacia una selección de juegos comerciales.

El proyecto prioriza:

- corrección antes que rendimiento;
- componentes pequeños y verificables;
- pruebas frecuentes en hardware real;
- mediciones reproducibles de memoria y rendimiento;
- código revisable, documentado y compatible con la licencia de Azahar;
- distribución sin ROM, firmware, claves ni archivos protegidos.

## Definición de una primera versión útil

La primera versión útil deberá:

1. instalarse como VPK;
2. iniciar y cerrar de forma segura;
3. cargar homebrew de 3DS desde almacenamiento local;
4. ejecutar código ARM11 correctamente;
5. mostrar ambas pantallas de 3DS;
6. recibir botones y entrada táctil;
7. reproducir audio estable;
8. guardar configuración, partidas y registros de diagnóstico;
9. ejecutar una selección documentada de software a velocidad razonable.

La compatibilidad completa con el catálogo de 3DS no forma parte del alcance inicial.

## Estado general

| Hito | Resultado esperado | Estado |
| --- | --- | --- |
| 0 | Sonda nativa instalada y validada en Vita | En desarrollo |
| 1 | Núcleo mínimo de Azahar compilando para ARMv7/Vita | Pendiente |
| 2 | Intérprete ARM11 ejecutando pruebas deterministas | Pendiente |
| 3 | Loader, memoria y kernel HLE ejecutando homebrew sin video | Pendiente |
| 4 | Presupuesto de memoria estable y medible | Pendiente |
| 5 | Imagen correcta mediante renderizador de referencia | Pendiente |
| 6 | Backend gráfico acelerado para Vita | Pendiente |
| 7 | Controles, táctil e interfaz mínima | Pendiente |
| 8 | Audio nativo estable | Pendiente |
| 9 | Backend de CPU de alto rendimiento | Pendiente |
| 10 | Optimización integral y perfiles por juego | Pendiente |
| 11 | Matriz pública de compatibilidad y regresiones | Pendiente |
| 12 | Primera alpha pública reproducible | Pendiente |

---

## Hito 0 — Base del proyecto y prueba de hardware

### Objetivo

Confirmar que VitaSDK puede producir una aplicación ARMv7 en C++20, empaquetarla como VPK y
ejecutarla correctamente en una Vita real.

### Entregables

- rama `vita-port`;
- build reproducible con CMake y Make;
- framebuffer nativo de 960 × 544 en CDRAM;
- lectura de botones;
- log en `ux0:data/azahar-vita/boot.log`;
- VPK de diagnóstico.

### Criterios de aceptación

- el VPK se instala con VitaShell;
- aparecen seis franjas de colores;
- `START` cierra la aplicación;
- se crea el log de inicio y cierre;
- la aplicación puede ejecutarse repetidamente sin bloquear la consola.

## Hito 1 — Núcleo mínimo de Azahar

### Objetivo

Compilar las utilidades fundamentales de Azahar para ARMv7 sin frontend de escritorio, red, audio
ni renderizadores no disponibles en Vita.

### Alcance

- tipos comunes y utilidades;
- logging adaptado a Vita;
- temporización básica;
- rutas y sistema de archivos;
- serialización estrictamente necesaria;
- configuración de compilación específica para Vita.

Qt, Vulkan, Discord RPC, telemetría, multijugador, scripting, dumping y depuración avanzada
permanecerán desactivados.

### Criterios de aceptación

- `citra_common` y sus dependencias mínimas compilan para ARMv7;
- no quedan dependencias accidentales de Windows, Linux, Android o Qt;
- la aplicación utiliza el logger de Azahar;
- se registra el tamaño del ejecutable y su consumo inicial de memoria.

## Hito 2 — Intérprete ARM11

### Objetivo

Ejecutar instrucciones de la CPU de 3DS mediante el intérprete DynCom de Azahar. Esta será la
implementación de referencia para validar corrección, no la solución final de rendimiento.

### Pruebas mínimas

- aritmética y operaciones lógicas;
- saltos y llamadas;
- lectura y escritura de memoria;
- instrucciones ARM y Thumb;
- registros, banderas y excepciones básicas;
- sincronización entre núcleos emulados.

### Criterios de aceptación

- los resultados coinciden con las pruebas de Azahar en escritorio;
- no se producen accesos inválidos ni corrupción de memoria;
- cada prueba deja resultados verificables en el log;
- se obtiene una primera medición de instrucciones por segundo.

## Hito 3 — Loader, memoria y kernel HLE

### Objetivo

Inicializar una instancia mínima del sistema de 3DS y alcanzar el punto de entrada de un homebrew
sin requerir todavía gráficos ni audio.

### Criterios de aceptación

- el loader reconoce un ejecutable válido;
- se crea el proceso emulado;
- se inicializa el mapa de memoria;
- la CPU alcanza y ejecuta el punto de entrada;
- los servicios fundamentales responden;
- los fallos generan diagnósticos comprensibles.

## Hito 4 — Gestión y presupuesto de memoria

### Objetivo

Mantener todos los subsistemas dentro de los límites de memoria de PS Vita y fallar de manera
controlada cuando una asignación no sea posible.

### Trabajo requerido

- medir RAM y CDRAM por subsistema;
- separar asignaciones de CPU, memoria emulada, video y cachés;
- reducir o desactivar cachés de escritorio;
- verificar que cargar y cerrar software libere recursos;
- reservar margen para situaciones transitorias;
- evaluar memoria ampliada sin convertir plugins de kernel en requisito inicial.

### Criterios de aceptación

- el consumo queda disponible en pantalla o logs;
- no existe crecimiento continuo al cargar y cerrar software;
- los errores de memoria son recuperables;
- hay presupuesto suficiente para iniciar el renderizador.

## Hito 5 — Renderizador de referencia

### Objetivo

Obtener una imagen correcta usando primero el renderizador por software de Azahar y copiar el
resultado al framebuffer de Vita.

### Criterios de aceptación

- se muestran las pantallas superior e inferior;
- un homebrew gráfico produce una imagen reconocible;
- colores, texturas y coordenadas básicas son correctos;
- pueden generarse capturas comparables con Azahar de escritorio;
- no se exige todavía velocidad jugable.

## Hito 6 — Backend gráfico acelerado

### Objetivo

Crear `renderer_vita` para traducir el trabajo de la GPU PICA200 de 3DS a la GPU de Vita,
utilizando VitaGL cuando sea viable y recurriendo a GXM sólo si las limitaciones lo exigen.

### Trabajo requerido

- estados de rasterizado;
- formatos y transferencia de texturas;
- render targets y framebuffers;
- generación o traducción de shaders;
- caché de shaders limitada;
- reducción de copias entre CPU y GPU;
- fallbacks para funciones no soportadas.

### Criterios de aceptación

- homebrew 2D se ejecuta con aceleración;
- una escena 3D básica se renderiza correctamente;
- el resultado puede compararse con el renderizador de referencia;
- no existen fugas sostenidas de VRAM;
- el rendimiento mejora claramente frente al software renderer.

## Hito 7 — Controles, táctil e interfaz

### Objetivo

Convertir la prueba técnica en una aplicación manejable desde PS Vita.

### Alcance

- botones y sticks configurables;
- transformación del táctil a coordenadas de la pantalla inferior;
- emulación de ZL/ZR mediante panel trasero o combinaciones;
- selector de archivos;
- disposiciones configurables de las dos pantallas;
- configuración global y por juego;
- acceso sencillo a logs y cierre seguro.

### Criterios de aceptación

- toda la interfaz puede usarse con botones;
- el táctil coincide con la pantalla inferior representada;
- los mapeos y configuraciones persisten;
- suspender y reanudar no bloquea la aplicación.

## Hito 8 — Audio

### Objetivo

Implementar una salida nativa estable y sincronizada.

### Criterios de aceptación

- homebrew reproduce audio reconocible;
- el buffer no sufre cortes continuos;
- la latencia no aumenta indefinidamente;
- pausa, suspensión y reanudación funcionan;
- el audio puede desactivarse para diagnosticar rendimiento.

## Hito 9 — Backend de CPU de alto rendimiento

### Objetivo

Superar el rendimiento del intérprete conservándolo como referencia y fallback.

### Líneas de investigación

1. optimizar DynCom para ARMv7;
2. implementar recompilación ARM11 a ARMv7;
3. ejecutar de forma controlada los bloques ARM compatibles;
4. combinar ejecución rápida con fallback al intérprete;
5. especializar rutas y servicios utilizados con frecuencia.

### Criterios de aceptación

- las pruebas mantienen los mismos resultados que DynCom;
- existe fallback seguro;
- cada mejora se demuestra mediante benchmarks;
- un error puede asociarse al bloque recompilado;
- no se aceptan cierres aleatorios como costo del rendimiento.

## Hito 10 — Optimización integral

### Objetivo

Alcanzar una velocidad útil en una selección limitada de software.

### Métricas obligatorias

- FPS y porcentaje de velocidad de emulación;
- tiempo consumido por CPU, GPU, audio y servicios;
- RAM y CDRAM;
- tamaño y actividad de cachés;
- stutters y frames perdidos;
- frecuencia de CPU/GPU usada durante la prueba.

### Criterios de aceptación

- al menos un homebrew complejo funciona a velocidad completa;
- algunos títulos ligeros alcanzan una velocidad razonable;
- una sesión prolongada no produce cierres ni fugas;
- las opciones rápidas no rompen las pruebas de exactitud.

## Hito 11 — Compatibilidad y regresiones

### Objetivo

Convertir demostraciones aisladas en resultados reproducibles y documentados.

### Clasificación

- no inicia;
- inicia;
- llega al menú;
- entra al juego;
- jugable con problemas;
- jugable;
- completable.

Cada informe deberá registrar versión del emulador, modelo de Vita, software probado, región,
configuración, rendimiento, errores, memoria utilizada y log.

### Criterios de aceptación

- existe una plantilla de informe;
- los fallos pueden reproducirse;
- las regresiones se detectan entre versiones;
- los ajustes especiales se almacenan por juego.

## Hito 12 — Alpha pública

### Objetivo

Publicar una versión experimental honesta y suficientemente estable para recibir pruebas y
contribuciones de la comunidad.

### Requisitos

- código fuente y licencia disponibles;
- VPK reproducible desde un commit identificado;
- documentación de instalación y compilación;
- lista de compatibilidad;
- configuración predeterminada conservadora;
- changelog y plantilla para reportar errores;
- logs fáciles de localizar;
- ningún archivo protegido incluido.

## Reglas de desarrollo

1. Cada hito debe producir una prueba ejecutable o verificable.
2. No se integran varios subsistemas grandes simultáneamente.
3. Todo fallo importante debe dejar un log útil.
4. No se optimiza código cuya corrección aún no está demostrada.
5. Toda optimización se compara con una implementación de referencia.
6. Llegar al menú no equivale a compatibilidad jugable.
7. La prueba en hardware real es obligatoria para cerrar los hitos correspondientes.
8. Cada VPK debe poder relacionarse con un commit.
9. Se conservan sondas pequeñas para aislar regresiones.
10. El código generado con IA debe revisarse, compilarse y probarse antes de integrarse.

## Próximas acciones

1. Validar físicamente el VPK de franjas de colores.
2. Preparar una prueba aislada del intérprete ARM DynCom.
3. Medir las dependencias necesarias para compilar `citra_common` en Vita.

El primer gran objetivo demostrable será ejecutar correctamente un homebrew de 3DS con CPU,
imagen, controles y logs. En ese punto el proyecto habrá pasado de ser una prueba de VitaSDK a un
emulador de 3DS funcional, aunque todavía experimental.
