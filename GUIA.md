# Guía: Harvest Moon (SNES) por dentro

[English](GUIDE.md) · **Español**

Esta guía explica, sin dar nada por sabido, **cómo funciona el juego por
dentro** según lo que vamos descubriendo al descompilarlo. Se actualiza con
cada descubrimiento nuevo.

- La primera parte (1-4) es lo mínimo para entender el resto: qué es
  descompilar, cómo es la consola por dentro y cómo comprobamos que lo que
  escribimos es correcto.
- La segunda parte (5) es el juego, sistema por sistema: el clima, la
  granja, las herramientas, los animales, las personas, los eventos, el
  final...
- Al final: los errores del juego original (6), un glosario (8) y el
  avance (9).

> Cómo leer los números: `$1F` es hexadecimal (base 16): `$0A` = 10,
> `$10` = 16, `$64` = 100, `$FF` = 255. Una dirección como `$7F:1F19` es
> "banco $7F, posición $1F19" (ver 2.3).

---

## 1. Qué estamos haciendo

### 1.1 Recompilación y descompilación

El juego original es un cartucho: una **ROM**, millones de bytes con las
instrucciones del procesador del SNES y los datos (gráficos, textos,
mapas).

- **La recompilación** (el proyecto HarvestMoonSNESRecomp) traduce esas
  instrucciones a C de forma automática para que el juego corra en una
  PC. El resultado funciona, pero es ilegible: miles de líneas del tipo
  "carga este byte, súmale este otro, si el resultado es cero salta".
- **La descompilación** (este proyecto) reescribe esas rutinas **a mano**,
  como C que una persona puede leer: con nombres (`ChangeStamina`,
  `ToolUsedHoe`) y comentarios que dicen qué hace cada cosa y por qué.

Una **rutina** es un pedazo de código con un principio y un final, como una
función: "sumar dinero", "decidir el clima de mañana", "usar la azada".

### 1.2 HM-Decomp

Partimos del desensamblado de la comunidad,
[HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp): el código del
cartucho escrito como ensamblador del 65816 con muchos nombres puestos. De
ahí salen los nombres de las rutinas y de las variables. Cuando una rutina
no tiene nombre allí (`CODE_81A688`, es decir "código en $81:A688"), le
ponemos uno nosotros (`ChangeMapTile`).

---

## 2. Lo mínimo del SNES que hay que saber

### 2.1 El procesador (65816) y sus registros

El procesador trabaja con unas pocas "casillas" internas llamadas
**registros**:

| Registro | Para qué |
|---|---|
| **A** | el acumulador: donde se hacen las cuentas |
| **X**, **Y** | índices: "el elemento número X de una tabla" |
| **S** | el puntero de la pila (ver 2.4) |
| **D** | la página directa (ver 2.5) |
| **DB** | el banco de datos: de qué banco se leen las variables |
| **PB** | el banco del programa: en qué banco está el código que corre |
| **P** | las banderas (ver abajo) |

Las **banderas** son bits que dejan las operaciones:

- **C** (carry, acarreo): "me pasé" al sumar, "no me faltó" al restar, y
  "mayor o igual" al comparar. Muchas rutinas devuelven información en C.
- **Z** (zero): el último resultado fue 0.
- **N** (negative): el último resultado tiene el bit alto en 1.

### 2.2 Modo de 8 y de 16 bits

A, X e Y pueden trabajar con 8 bits (0-255) o con 16 bits (0-65.535). Se
cambia con las instrucciones `SEP` (a 8 bits) y `REP` (a 16 bits). En
HM-Decomp se ven como `%Set8bit(!M)` y `%Set16bit(!MX)`.

Detalle que importa mucho al descompilar: en 8 bits, A sigue teniendo su
**byte alto guardado**. Una instrucción de 8 bits solo cambia el byte bajo,
así que lo que quedó arriba sigue ahí y puede reaparecer después.

### 2.3 La memoria

| Zona | Qué es |
|---|---|
| **WRAM** `$7E:0000-$7F:FFFF` | la memoria de trabajo (128 KB): todas las variables del juego |
| **ROM** bancos `$80-$FF`, `$8000-$FFFF` | el cartucho: código y datos, solo lectura |
| `$0000-$1FFF` de los bancos `$00-$3F` y `$80-$BF` | un "espejo" de los primeros 8 KB de la WRAM |
| `$2100-$21FF`, `$4200-$43FF` | registros del hardware: video, sonido, DMA |
| SRAM (banco `$70`) | la memoria con pila del cartucho, donde se guarda la partida |

Así, `$7F:1F19` (la estación del año) es el byte `$1F19` del segundo banco
de WRAM. Y `$0921` (el objeto en la mano) cae en los primeros 8 KB, que se
ven desde casi cualquier banco.

### 2.4 La pila, JSL/RTL y JSR/RTS

Para llamar a una rutina, el procesador guarda en la **pila** la dirección
a la que tiene que volver, y luego salta:

- `JSL` (salto largo) guarda 3 bytes: el banco y la dirección de vuelta.
  La rutina termina con `RTL`.
- `JSR` (salto corto, dentro del mismo banco) guarda 2 bytes. La rutina
  termina con `RTS`.
- `PHA`/`PLA`, `PHX`/`PLX` guardan y sacan valores de la pila.

La pila vive en la WRAM, así que esos bytes quedan escritos en memoria. Por
eso las reescrituras también escriben en la pila exactamente lo mismo: las
direcciones de vuelta se leen de la ROM con `tools/calls.py`, nunca se
calculan a mano.

### 2.5 La página directa y los punteros largos

La **página directa** son 256 bytes de acceso rápido. En este juego empieza
en `$0000`, así que `$72` es simplemente la variable `$0072`. El juego las
usa como variables de trabajo:

- `$72-$74`: un puntero largo de 3 bytes (banco + dirección) al registro
  de un animal.
- `$CC-$CE`: el slot de evento actual.
- `$C9-$CB`: la posición en un guion de evento.

`LDA [$72],Y` significa "lee del sitio al que apunta `$72-$74`, más Y".

### 2.6 Cuadros, NMI y DMA

- El juego avanza en **cuadros**: 60 por segundo.
- Al terminar cada cuadro, el hardware avisa con una interrupción, el
  **NMI**. En ese momento el juego copia gráficos a la memoria de video.
- Esas copias se hacen con **DMA**: el hardware mueve bloques de bytes
  solo, sin que el procesador vaya byte por byte. El juego deja anotadas
  durante el cuadro las copias que quiere (una "cola"), y el NMI las
  arranca todas juntas.

### 2.7 El chip de sonido

La música y los efectos los hace otro procesador (el SPC700, o "APU"). El
juego habla con él por 4 puertos (`$2140-$2143`): escribe un pedido y
**espera** a que el chip responda (ver 5.15 para la conversación y 3.4
para cómo lo maneja el comprobador).

---

## 3. Cómo sabemos que la reescritura es correcta

### 3.1 El comprobador

Cada rutina reescrita se compara contra la original, **byte por byte**:

1. Se copia toda la memoria (128 KB) y los registros.
2. Corre la rutina original.
3. Corre la reescritura sobre la copia.
4. Se comparan la memoria completa (la pila incluida), los registros A, X,
   Y y la bandera C, y los registros de DMA.

Si un solo byte es distinto, el comprobador avisa con la entrada exacta que
lo produjo.

### 3.2 Dos formas de probar

- **En vivo**: el juego se juega solo con un guion (una partida nueva y 10
  noches forzadas) y se comprueba cada llamada real.
- **Al azar** ("fuzz"): cada rutina se llama 2.000 veces con entradas al
  azar, con 10 semillas distintas. Cada rutina tiene su **preparación**
  (`prep_*`), que arma entradas con sentido: una granja al azar, animales
  al azar, un guion de evento con bytes al azar...

### 3.3 Que la prueba pruebe de verdad

Algunos trucos que fueron necesarios:

- **Que salgan los casos raros.** Si un hallazgo es 1 en 64, hay que
  sortear también la semilla del generador de azar. Si no, todas las
  llamadas sacan la misma tirada y el hallazgo nunca aparece.
- **Que las herramientas funcionen.** Hay una tabla de casillas propia
  donde casi todo se puede usar desde todos los lados.
- **Que la igualdad aparezca.** En "salta si es igual", la igualdad al azar
  sale 1 de cada 256 veces, así que la preparación la fuerza de vez en
  cuando.
- **Sabotaje.** Para confirmar que una rama se prueba, se rompe a propósito
  un número en la reescritura y se mira que el comprobador avise. Siempre
  avisó.

### 3.4 Lo que el comprobador no puede ver

- **Las partidas guardadas (SRAM)** también se comparan: el comprobador
  copia la RAM con batería del cartucho antes de la llamada y revisa que
  la reescritura la deje exactamente igual que el original.
- **El hardware de video, resuelto corriendo dos veces.** El chip de
  imagen (PPU), la unidad de DMA y los registros propios de la CPU ($42xx)
  no son memoria, pero guardan estado. Para las rutinas que los usan (flag
  `HM_IO`, `src/hm_io.c`) el comprobador guarda ese estado antes de la
  llamada (los registros del PPU, sus punteros y pestillos de VRAM, CGRAM
  y OAM, las tres memorias, los canales de DMA y HDMAEN, NMITIMEN, los
  temporizadores, el multiplicador y el divisor, el puerto de WRAM). El
  original corre como siempre. Después el comprobador aparta su resultado,
  repone el estado de antes (chips y WRAM) y corre la reescritura sobre la
  máquina real: sus escrituras van a los chips emulados, así que un DMA
  que lance copia de verdad. Los dos resultados tienen que ser idénticos,
  y se repone el del original para que el juego siga. Las escrituras de la
  reescritura no mueven el haz emulado. Las del original sí (cada escritura
  pone el haz al día con el reloj de la CPU, un DMA cobra su tiempo), así
  que la prueba al azar apaga el HDMA y las IRQ por línea y empieza cada
  llamada en el mismo punto del cuadro: ya no pone el reloj de la CPU a 0,
  que hacía saltar el haz. En vivo, en el juego, StartProgramedDMA pasó
  1.037 de 1.039 llamadas (2 saltadas, cortadas por el cuadro).
- **El chip de sonido, resuelto grabando.** Sus respuestas vienen de otro
  procesador y no se pueden calcular. Entonces, mientras corre el
  original, el comprobador anota cada lectura y escritura en los puertos
  de sonido, en orden. Después, cuando corre la reescritura, cada lectura
  de un puerto recibe el valor que recibió el original en ese mismo punto,
  y cada escritura tiene que ser exactamente la que hizo el original
  (mismo puerto, mismo valor, mismo orden). Una diferencia y la prueba
  falla. En la prueba al azar, estas rutinas se prueban en un momento más
  tranquilo del juego (900 cuadros antes), porque a veces el chip está
  ocupado cargando música mucho tiempo y nunca dice "listo". Desde el 28
  de octubre se guarda la máquina entera (chip incluido, la misma
  instantánea que una partida guardada rápida) antes de la primera llamada
  y se repone antes de cada una: con el chip siguiendo de una llamada a
  otra mientras la WRAM que lleva la cuenta se reponía, lo que mandaban
  las llamadas se amontonaba en el chip hasta que dejaba de contestar, y
  todas las rutinas de después se quedaban sin probar. Una llamada que aun
  así espera para siempre (a un chip que sigue ocupado mientras suena la
  música) la corta el vigilante de cuadros y cuenta como saltada; a las
  tres, se deja esa rutina.
- **Esperar al cuadro siguiente, resuelto adelantando el NMI.** Una
  rutina que espera al NMI (`WaitForNMI`: borra `$00` y da vueltas hasta
  que el NMI lo pone) nunca se completa como una sola llamada en el juego
  recompilado: en el bucle el código compilado le pasa el control al
  planificador de cuadros, el cuadro sigue y el resto corre después en el
  intérprete. Así que el control en vivo se salta esas llamadas y la
  prueba al azar es la verificación (bandera `HM_WAIT`): corre el original
  en el intérprete y, cuando llega al `LDA $00` del bucle con `$00` todavía
  a 0, hace que el NMI llegue justo ahí, como lo haría la CPU (mete el
  banco, la dirección y los flags, pone I, quita el modo decimal y salta
  al manejador `$00:867B`; su RTI vuelve al bucle). La reescritura hace
  exactamente lo mismo en el mismo punto (`hm_nmi`). Todo lo que hace el
  NMI (los DMA preparados, la tabla de sprites, los mandos, un número al
  azar, el desplazamiento) se compara como siempre, bytes de la pila
  incluidos. Hizo falta algo más. Los flags hay que seguirlos exactos en
  las reescrituras que esperan: `WaitForNMI` los guarda (PHP) y el NMI los
  mete en la pila, y los dos bytes se quedan ahí. Y las rutinas que esperan
  son las que tocan efectos de sonido y cargan instrumentos en el chip de
  sonido: con el chip siguiendo de una llamada a otra mientras la WRAM que
  lleva la cuenta se repone, los instrumentos se amontonaban en la memoria
  del chip hasta que el programa de sonido dejaba de contestar. Por eso la prueba al
  azar ahora repone la máquina entera antes de cada llamada de una rutina
  de sonido (ver el chip de sonido, arriba). Además, el multiplicador y el
  divisor ($4202-$4206) también son registros de chip:
  `MultiplicationUnsigned` y `DivisionUnsigned` ahora los escriben como el
  original, y la comparación de chips lo revisa.
- **Lo que corre en el intérprete.** Algunas rutinas no tienen versión
  compilada: la recompilación las corre en un intérprete. El comprobador
  en vivo no ve esas llamadas, y entonces la prueba al azar es la
  verificación (llama al original por el intérprete).
- **Rutinas que nunca vuelven, resueltas parándolas (bandera
  `HM_TOLOOP`).** El bucle del juego (`GameLoop`, `$80:8083`) da vueltas
  para siempre, y también la pantalla de título, la de los nombres y las
  demos; las rutinas de antes (las de los nombres, el comienzo de un día,
  RESET) saltan a ellas y no regresan. Sus comprobaciones paran el
  original donde entra al bucle o, para un bucle mismo, en el salto de
  vuelta al comienzo de su cuadro (un cuadro), o donde sigue a otra de
  estas rutinas: un gancho en esa instrucción lo manda a un RTL
  (`$80:972B`) que saca el marco del comprobador, y la reescritura vuelve
  en el mismo lugar. Dos necesitaron más: `Unk_NamesInput` deja un byte
  suelto en la pila (error 30, abajo), que la parada quita primero; y RESET
  borra toda la WRAM, la pila también, así que el comprobador guarda su
  marco y el puntero de pila en la primera instrucción de RESET y los
  repone en la parada (la reescritura hace lo mismo al final). RESET además
  sube el programa de sonido por la ROM de arranque del chip, así que,
  como `UNK_Audio1`, se prueba una vez en el cuadro 1 (`HM_LIVE`). El bucle
  de una sola instrucción `SUB_82878E` (`BRA` a sí mismo) se para en su
  segunda vuelta.
- **Rutinas largas hechas de otras (bandera `HM_LOOSE`).** Las esperas que
  llaman estas rutinas guardan las banderas (PHP), y el NMI las empuja:
  esos bytes dependen de N, Z y V dejadas por todo lo anterior, que una
  reescritura hecha de llamadas no sigue. Por eso en ellas no se compara la
  pila por debajo del puntero de pila final (bytes que quedan ahí y nadie
  vuelve a leer); todo lo demás sí, el acarreo y el ancho de los índices
  incluidos.
- **Esperas muy adentro.** Mientras se prueba una de las rutinas que
  esperan, el original corre en el intérprete, que pasa las llamadas que
  encuentra a sus versiones compiladas; una rutina compilada que espera el
  NMI ahí no lo vería nunca (el NMI del intérprete es el del comprobador).
  La lista del intérprete de "estas déjalas interpretadas" tiene sitio
  para 16, y ya hay más de 40, así que los envoltorios de comprobación
  (`tools/decomp_overrides.py`) preguntan primero: mientras se prueba una
  rutina que espera, una llamada a otra rutina que espera desde el código
  compilado la corre en el intérprete (`hm_decomp_wait_interp`).
- **Entradas que una rutina larga no debe recibir.** Una rutina hecha de
  muchas otras se encuentra con lo que encuentra cada una, así que su
  preparación tiene que dejar las entradas reales para todas. Las
  encontradas: un mapa que se entra por `SUB_80972C` debe tener paleta de
  sprites (los mapas $4B-$56 no la tienen: su entrada en `UNK_Table11`
  apunta más allá de la tabla de paletas, al bus abierto); el bit 15 de
  `$0196` (un dibujo guardado tal cual) solo viene con la granja, el
  pueblo y la montaña, y con él los dibujos comprimidos del título se
  copiarían tal cual, $8000 bytes, a la zona de E/S; el año de las
  partidas guardadas debe ser 0-2; el HDMA debe estar apagado (un HDMA que
  copia a la WRAM mientras corre el original no tiene igual en la corrida
  de la reescritura, que no mueve el haz); una granja que dibuja
  `UNK_PartialMap` debe tener códigos de casilla reales; y `ChangeStamina`
  lee la bandera N de quien la llama (error 11), que una reescritura que
  la llama debe poner como la pondría el `LDA` del original.
  `HM_DECOMP_WATCH=<índice de WRAM>` imprime cada instrucción del original
  tras la que ese byte cambió, y así se encontraron.
- **Lecturas fuera del cartucho.** Si una prueba al azar hace que una
  rutina lea una tabla tan lejos que se sale de la ROM, cae en direcciones
  donde no hay nada (el "bus abierto": se lee lo último que pasó por los
  cables). En el juego eso nunca pasa, así que las preparaciones dejan las
  entradas dentro de la ROM. Ojo con lo que la preparación *no* toca: la
  regresión completa del 23 de octubre encontró una diferencia en
  `UNK_CheckTileProperty` porque la mitad de las veces se dejaba el puntero
  a la tabla de propiedades ($0D-$0F) que había en ese momento del juego, y
  en ese cuadro apuntaba por debajo de $8000 de un banco de la ROM. Ahora
  solo se conserva si toda la tabla (4 bytes por 256 tipos) es memoria de
  verdad.

### 3.5 El porcentaje

`tools/progress.py` cuenta las instrucciones de todo el código de HM-Decomp
(45.176) y cuántas pertenecen a rutinas ya reescritas y verificadas. Una
rutina que recorre las 4.096 casillas de la granja pesa más que una de tres
líneas. El 21 de octubre se corrigió la medición: antes no se contaba el
código sin dirección anotada y daba de más (10,49 % → 7,29 %).
Después vino una segunda corrección: HM-Decomp a veces escribe varias
rutinas una detrás de otra bajo un solo nombre. Ahora una etiqueta a la
que se llama desde fuera de su bloque cuenta como rutina aparte, y las
instrucciones se le acreditan a la que corresponde (20,45 % → 20,33 %).
Una tercera el 23 de octubre: cuatro comandos de guion tienen en HM-Decomp
la dirección anotada con el banco equivocado ($80 en vez de $84), y uno de
ellos caía sobre una rutina real del banco $80 (`CallIndexedSubrutines`,
$80:9553), cuya cuenta lo incluía (56,14 % → 56,10 %).

---

## 4. Dónde está cada cosa en el proyecto

| Archivo | Qué hay |
|---|---|
| `src/rng.c` | números al azar |
| `src/math.c` | división |
| `src/weather.c` | el clima |
| `src/farm.c` | el campo de la granja |
| `src/tools.c` | qué casilla toca una herramienta y si funciona |
| `src/toolused.c` | qué hace cada herramienta |
| `src/stamina.c` | la energía y el almuerzo |
| `src/stock.c` | madera, pasto, dinero, felicidad |
| `src/animals.c` | vacas y gallinas |
| `src/people.c` | la chica favorita, esposa e hijos |
| `src/calendar.c` | los nombres de la fecha |
| `src/events.c` | los slots de evento |
| `src/eventcmds.c` | los comandos de los guiones |
| `src/mapedit.c` | cambiar casillas del mapa |
| `src/dma.c` | la cola de DMA |
| `src/objects.c` | objetos con sprite |
| `src/text.c` | el cuadro de diálogo |
| `src/audio.c` | hablar con el chip de sonido |
| `src/video.c` | el chip de imagen: DMA, negro, modos, borrar |
| `src/save.c` | las partidas guardadas |
| `src/menus.c` | botones en los menús |
| `src/scroll.c` | desplazar el mapa |
| `src/palette.c` | los colores del día |
| `src/collision.c` | chocar con el mapa, los objetos y el jugador |
| `src/toolanim.c` | la animación de la herramienta al apretar Y |
| `src/interact.c` | apretar A frente a algo |
| `src/playerinput.c` | los botones al caminar |
| `src/decompress.c` | los datos comprimidos (LZSS) |
| `src/coloranim.c` | colores que se mueven (ciclo de paleta) |
| `src/joypad.c` | leer el mando |
| `src/toolsound.c` | el ruido de cada herramienta |
| `src/maptiles.c` | lo que recibe un mapa al cargarse |
| `src/shipping.c` | el envío de las 5 de la tarde |
| `src/ending.c` | el puntaje del final |
| `src/hm_decomp.c` | el comprobador y las preparaciones |
| `src/hm_io.c` | la parte del comprobador para los chips (HM_IO) |
| `src/nmi.c` | las interrupciones y las esperas al cuadro siguiente |
| `src/transition.c` | ir de un mapa a otro (cargar el nuevo) |
| `src/mapevents.c` | quién aparece al entrar a un mapa |
| `src/day.c` | el reloj, la noche y la primera mañana |
| `src/game.c` | el bucle del juego y el comienzo de una partida cargada |
| `src/names.c` | la pantalla de los nombres y adónde va cada nombre |
| `src/demo.c` | las demos de la pantalla de título |
| `src/intro.c` | la pantalla de título, su menú y las pantallas de partidas |
| `src/reset.c` | el encendido |
| `native/main.c` | la versión nativa: la ventana, los controles, el sonido, el orden de las rutinas |
| `native/hw.c` | la versión nativa: los chips de la consola alrededor del C (mapa de memoria, registros) |

---

## 5. El juego por dentro

### 5.1 Números al azar

- `GetRNG` ($83:8138) es un generador de 24 bits: con el número actual
  calcula el siguiente. La semilla vive en `$0100-$0102`.
- `RNGReturn0toA` ($80:89F9) es "tirar un dado de n caras" y da de 0 a
  n-1. El juego lo usa como "1 de cada n": **salió 0**.
- **No es justo.** Con un dado de 6, el 0 sale con 43 de los 256 valores
  posibles, en vez de 42,7. El 0 sale un poco más de la cuenta.
- `$DE` es otro byte al azar que usan los animales (el perro, las
  gallinas).

### 5.2 Fecha y hora

- **Calendario.** `$7F:1F18` año (0 = año 1), `1F19` estación (0
  primavera, 1 verano, 2 otoño, 3 invierno), `1F1A` día de la semana,
  `1F1B` día (1-30).
- **Reloj.** `1F1C` hora, `1F1D` minutos (en pasos de 4).
- `$0973`: 0 = el reloj está detenido, 1 = corre, 2 = pasar al día
  siguiente. A las 18:00 el reloj se detiene.
- **Festivales:** Flores (primavera 23), Cosecha (otoño 12), Huevo (otoño
  20), Acción de Gracias (invierno 10), Noche de Estrellas (invierno 24),
  Año Nuevo (invierno 30).
- **El sufijo de la fecha** es "st/nd/rd" solo para los días 1-3, así que
  el juego escribe "21th", "22th", "23th".

**El envío de las 5 de la tarde** (`src/shipping.c`). Cada hora del juego
`UpdateTime` llama a `ShippingScene`: a las 17:00 y en la granja apaga los
controles, marca la escena ($7F1F5A bit 10) y arranca su guion ($26, en el
slot de evento 6). Cuando el guion termina (pone el bit 11), y no hay un
cambio de mapa en marcha, `ShippingSceneDialogue` muestra el texto del día:
lo enviado (texto $031A), o "nada" ($031B) cuando el dinero enviado
($7F1F07-$7F1F09) es 0; luego el guion $27, y se borra la marca.

### 5.3 El clima

Cada noche se decide el clima de mañana (`WeatherTomorrow`) y se guarda un
código en `$098C`:

| Código | Clima |
|---|---|
| 0 | soleado |
| 1 | lluvia |
| 2 | nieve |
| 3 | huracán |
| 4, 5 | las dos tormentas del primer año |
| 6-11 | días de festival |

- **Probabilidades:** lluvia 1/6 en primavera y 1/10 en verano y otoño.
  Nieve 1/3, solo en invierno. Huracán 1/30, solo en verano; con el
  caparazón de tortuga, 1/60.
- **Las tormentas del primer año:** en invierno es segura el día 8, en
  verano el día 30, y cada una pasa una sola vez.
- **A la mañana siguiente:** `SetWeatherFlags` convierte el código en las
  banderas de `$0196`, que leen los mapas y la tele.

### 5.4 La granja

- **El campo** es una cuadrícula de 64 × 64 casillas (`$7E:A4E6`, un byte
  por casilla). Cada casilla mide 16 × 16 píxeles.
- **Códigos de casilla:**

  | Código | Casilla |
  |---|---|
  | 02 | tierra |
  | 03 | maleza o piedra |
  | 05 | cerca |
  | 06 | cerca rota |
  | 07, 08 | tierra arada |
  | 1E-6F | cultivos en todas sus etapas |
  | 70-79 | pasto creciendo (79 = crecido) |
  | A0 en adelante | ya no es campo |

- **Seco y regado van en pares:** código par = seco, el impar siguiente =
  regado.
- **Cada noche** (`NightlyFarmTilesCheck`):
  - La lluvia riega todo lo seco.
  - Lo regado, si no llueve, se seca.
  - Un cultivo regado crece una etapa.
  - En otoño lo regado solo se seca, y en invierno nada crece.
  - El primer día del invierno, todo cultivo vuelve a tierra arada.
- **Daños de la noche:** la lluvia rompe cercas (1/96); el huracán también
  arranca pasto (1/16) y cultivos (1/4); el año nuevo borra pasto (1/64) y
  cultivos (1/32).
- **El "desarrollo del rancho"** (`CalculateFarmDevelopment`) es contar
  las casillas con cerca, cultivo o pasto.
  En porcentaje, para el final (`CODE_83F4D8`, el 30 de verano del año
  3, el último día): cuenta x 10 / 295, como mucho 100. Esa misma rutina
  arranca el guion del final ($2E) y elige la vaca que muestra el final:
  la de más cariño (+04; la última si hay empate), cuyo nombre va a
  $0889-$088F (en blanco si no hay vacas).
- **La granja se guarda aparte.** En la granja el juego trabaja sobre el
  mapa actual (`$7E:09B6`); `CopyCurrentMaptoFarmMap` lo guarda en la copia
  propia de la granja (`$7E:A4E6`), para que sobreviva al ir a otro sitio.
  Una partida nueva empieza con el campo del cartucho (`LoadDefaultFarmMap`,
  desde `$A7:8000`, el primer puntero de `MapPointerTable`, que está en
  $82:B174, no en B173 como dice HM-Decomp).

### 5.5 Las herramientas

**¿Funciona aquí?** Cada mapa tiene una tabla de propiedades: 4 bytes por
tipo de casilla, que dicen qué herramientas funcionan y desde qué lados y en
qué estaciones. Bits: hoz 01, azada 02, martillo 04, hacha 08, agua 10,
semillas 20. `CheckToolSuccess` responde: 1 funciona, 0 no, 2 se llena la
regadera.

**Rareza:** cualquier objeto que no esté en la lista "funciona".

**Primero la animación** (`src/toolanim.c`). Apretar Y corre
`UNK_ToolUsed`: sin energía ($D2 bit 3) el jugador solo muestra la
animación de "cansado" ($4D). Si no, revisa la casilla de enfrente, lo que
también elige el sonido de la herramienta, y llama a la rutina de animación
de la herramienta (`Tool_Animation_Table`, de nuevo una por objeto). Cada
una solo pone la animación del jugador en $0901: un número por herramienta,
más la dirección para las herramientas que se ven distintas según hacia
dónde mira. La regadera tiene tres: regando, vacía y llenándose (frente al
agua además llena la regadera, 20 usos). La campana además levanta $7F1F5A
bit $10, que llama a las vacas. Lo que la herramienta le hace a la casilla
viene después, cuando la animación llega a ese momento (`SUB_829260`).

**Quién las llama:** `SUB_829260` toma el objeto en la mano ($0921), busca
su rutina en `Tool_Used_Table` (una por objeto, en el orden de los
objetos: 28 en total) y la corre; después levanta la bandera "se usó una
herramienta" ($7F1F5A bit $0040).

**Qué hace cada una** (`src/toolused.c`; todas gastan energía al final):

| Herramienta | Qué hace |
|---|---|
| Hoz | Corta la maleza; el pasto se vuelve forraje (+1 en el silo). No sirve en invierno. |
| Azada | Ara la tierra. |
| Martillo | Rompe piedras chicas al instante; las rocas grandes de 2×2, al 6.º golpe. |
| Hacha | Corta tocones al 6.º golpe: +6 de madera. |
| Semillas | Cada uso siembra las 9 casillas de alrededor, una por llamada. |
| Pintura | Pinta una de las 4 partes de la pared de la casa: +10 de felicidad cada una. |
| Comida | En el gallinero o el establo llena el comedero de enfrente; si no, se cae al suelo. |
| Medicina, poción | Se gastan y dejan una bandera que lee el código de las vacas. |
| Regadera | Riega la casilla de enfrente: su tipo pasa al código siguiente (seco → regado). Un agua menos por uso; con la regadera vacía, o frente al agua (respuesta 2), nada. Le pregunta dos veces a `PreCheckToolSuccess`. Efecto de sonido 0, 7. |

El contador de golpes del martillo y el hacha es el mismo, `$096D`.

**Las semillas solo prenden en su estación:**

| Semilla | Estación |
|---|---|
| Maíz, tomate | verano |
| Papa, nabo | primavera |
| Pasto | primavera y verano |

Fuera de su estación, la casilla queda como brote muerto.

**Las herramientas de oro:**

| Herramienta de oro | Diferencia |
|---|---|
| Hoz | corta las 9 casillas de alrededor |
| Azada | ara hasta 6 casillas en línea y se detiene en la primera que no puede |
| Martillo | rompe la roca grande de un golpe (gasta 4 de energía) |
| Hacha | corta el tocón de un golpe (gasta 8) |
| Aspersor | riega las 9 casillas de alrededor, una por llamada, y nunca se queda sin agua (gasta 8) |

**Hallazgos.** Ninguno ocurre si `$7F:1F60` bit 3 está puesto:

| Herramienta | Hallazgo | Probabilidad | Cuándo |
|---|---|---|---|
| Azada | moneda (10 G) | 1/16 | una vez al día |
| Azada | bolsa de dinero (50 G), si no hubo moneda | 1/16 | una vez al día |
| Azada | topo | 1/32 | primavera y verano, una vez al día |
| Azada | baya de poder | 1/64 | solo 2 veces en todo el juego |
| Hoz | una rana (+2 de felicidad) | 1/16 | verano, una vez al día |
| Martillo | baya en la roca del manantial (mapas 0C-0F) | 1/4 | una vez en todo el juego |
| Hacha | baya en un tocón fuera de la granja | 1/16 | una vez |
| Hacha en la montaña (casilla F4) | evento del hacha de oro | — | una vez |

**El ruido de cada herramienta** (`src/toolsound.c`). `Tool_Noise_Table`
($82:9054) tiene 3 bytes por objeto en la mano: un banco de sonidos y dos
efectos, uno para cuando la herramienta da en algo y otro para cuando no
($0922 = 0 o 1; 2 es en silencio). `ToolUsedSound1` ($82:8FB1) elige el
efecto y lo toca en el chip de sonido (`UNK_Audio19`); un efecto 0 es sin
sonido.

### 5.6 La energía

- **Energía actual** `$0918` y **máxima** `$0917`; empieza en 100.
- **Al llegar a 0** el jugador se desmaya.
- **Cansancio** (`$096C`, de 0 a 3): sube por etapas a medida que la
  energía baja de la mitad, de la cuarta parte, etc.
- **Rareza:** `ChangeStamina` decide si el cambio es positivo o negativo
  con la bandera N que dejó **quien la llama**, no con el valor de A.

### 5.7 Los animales

**Registro de vaca** (16 bytes, desde `$7E:C1C6`, hasta 12 vacas):

| Byte | Contenido |
|---|---|
| +0 | estado: 01 existe, 02 recién nacida, 04 cría, 08 adulta, 10 de mal humor, 20 enferma, 40 preñada, 80 acaba de parir |
| +2 | dónde está (`$27` = establo) |
| +3 | un contador de días |
| +4 | felicidad (0-255) |
| +5 | días fuera del establo |

**Registro de gallina** (8 bytes, desde `$7E:C286`, hasta 13):

| Byte | Contenido |
|---|---|
| +0 | estado: 01 existe, 02 huevo, 04 pollito, 08 gallina, 10 con hambre |
| +1 | dónde está (`$28` = gallinero) |

**La noche de los animales:**

- **Vacas sin comer** pierden felicidad y pueden enfermar: 1/4, o 1/2 con
  nieve o huracán.
- **Enferma hasta el final, muere**, salvo que mañana sea festival.
- **Una vaca 10 días fuera** se pone de mal humor.
- **El parto:** la cría empieza con felicidad 20, 50 o 100 según la de la
  madre.

**Animales nuevos.** `AddNewCow` y `AddNewChicken` buscan el primer
registro libre. Una vaca nacida en la granja te da +70 de felicidad.

**Contarlos, sin usar.** Justo después de los punteros a los registros hay
dos rutinas que cuentan los animales: `UnusedBarnCoopCheck` ($83:C9BA) las
gallinas en uso (bit 0 del registro, de 13) y, justo después de su RTL y
sin etiqueta propia, `UnusedBarnCheck` ($83:C9E5) las vacas (de 12). Nada
las llama: parecen una primera versión, y el juego cuenta en otro sitio.

**Al entrar a un mapa** (`PlaceAnimalsOnMap`), cada gallina, vaca, el perro
y el caballo que esté ahí arranca su guion. "Está ahí" agrupa mapas:

| El animal está en… | Se ve en… |
|---|---|
| 00-03 (la granja en cualquier estación) | 00-03 |
| 04-07 | 04-07 |
| 08-0F | 0C-0F |
| 10-13 | 10-13 |
| 14-17 | 15-17 |
| 31 en adelante | 31 en adelante |
| cualquier otro | solo ese mismo mapa |

El caballo no tiene el grupo 14-17. Con lluvia, las vacas fuera del establo
usan otros guiones.

**La gallina, en cada cuadro:**

- Si pulsas A frente a ella con las manos libres, **la levantas**: como
  pollito (objeto $26) o como gallina ($25).
- Si está marcada para la venta, se vende (precio $32).
- Si no la tocas, un contador llega a $200 y arranca otro guion.

**El perro, en cada cuadro:**

- Cada **abrazo** se cuenta en `$7F:1F52`.
- Su **ánimo** (`$7F:1F58`) pasa entre 5 estados cada 2 segundos, y una
  tirada al azar decide qué hace.
- Cuando lo **llaman**, corre hacia ti por el eje en que está más lejos.

**La vaca, cada cuadro** (`Event_UNKCowRelated`, la rutina más grande
descompilada hasta ahora: 1.567 instrucciones). Cada vaca tiene una ficha
de 16 bytes: estado (ternera, joven, adulta, enferma, preñada, "huye de
ti"), banderas del día (cepillada, ordeñada, le hablaste, caminando,
cruzada), mapa, días de preñez, **felicidad**, veces golpeada y posición.

- **Hablarle** (A frente a ella): un texto según su estado. La primera vez
  de cada día: **+1 de felicidad**.
- **Cepillo:** una vez al día, **+3 de felicidad**.
- **Ordeñadora:** una vez al día, solo adultas, ni enfermas ni preñadas. El
  tamaño de la leche depende de la felicidad: menos de 96 chica, menos de
  192 mediana, si no grande.
- **Golpearla** con la hoz, azada, martillo o hacha (o las de oro): se
  queja y sube el contador de "veces golpeada" (hasta 10).
- **Poción milagrosa** en una adulta: queda preñada (**+10 de
  felicidad**), salvo que ya haya 12 vacas. **Medicina:** se cura.
- **Venderla** (desde la granja): el precio depende de la felicidad: 500 G
  con menos de 96, 800 G con menos de 192, 1000 G por encima.
- **Pasear:** cada 256 cuadros (unos 4 segundos) una tirada al azar elige
  un guion para caminar, distinto para terneras, jóvenes y adultas.
- **La campana:** las vacas se voltean hacia ti (o al revés, si son de las
  que "huyen") y vienen.
- Cada guion tiene cuatro versiones, una por tipo de vaca (byte +02 del
  slot).

### 5.8 Las personas

- **Corazones** de las cinco chicas: `$7F:1F1F + 2·n` (0-999), en el orden
  María, Ann, Nina, Ellen, Eve. Hablarles suma +8.
- **La favorita** (`FindMostLovedName`): en caso de empate gana la
  última.
- **De puntos a corazones** (`CODE_81D1C5`): los puntos (0-999) se
  convierten en un nivel de corazones, 0-9, según estos rangos (tabla $81:D1E8): 0-49,
  50-119, 120-199, 200-249, 250-299, 300-399, 400-499, 500-599, 600-799,
  800-999. Los escalones no son iguales: los primeros corazones cuestan y
  el último pide 200 puntos.
- **Hijos.** El primero necesita 450 corazones con la esposa; el segundo,
  650 y que el primero tenga 90 días. Los dos necesitan la casa en nivel 2
  y 20 días de casados. Un hijo nace 60 días después de concebido.

### 5.9 Dinero, madera, pasto

- **El dinero** (`$7F:1F04`, 3 bytes) se guarda en **decenas de G**: el
  último 0 que ves en pantalla es de adorno. Tiene un tope de 999.999.
- **Madera, pasto y felicidad** tienen tope 999.
- **Cuando un guion cobra o paga** (comando `ChangeMoney`) y el dinero
  quedaría en negativo, el dinero queda en **0** en vez de no alcanzar.
  Ver 6.

### 5.10 Los eventos

Todo lo guionado (escenas, aldeanos que caminan, animales, festivales)
corre como un **guion de evento** en uno de los **slots** ("CC"). Son
estructuras de 64 bytes desde `$7E:B586`:

| Byte | Contenido |
|---|---|
| +00 | en uso |
| +01 | banderas |
| +10 | contador de cuadros |
| +1A, +1C | posición |
| +30, +32 | dónde va su guion |
| +3F | su propio número |

**Números de slot:** perro $16, caballo $17, vacas $18 + n, gallinas $24 +
n. Hay 49 ($31); `ClearsofAllCCStructs` ($84:8000) los libera todos (primer
byte a 0), por ejemplo al entrar en un mapa.

**El guion es un mini lenguaje.** Cada byte es un **comando** (hay 90, en
la tabla `$84:9419`), seguido de sus argumentos. `$C9-$CB` señala por dónde
va. Algunos comandos:

| Comando | Qué hace |
|---|---|
| `JumptoInstruction` | salta a otra parte del guion |
| `JumpifFlagSet`, `JumpifEquals`, `JumpifBetween` | saltan si se cumple algo |
| `UNKCompareset7Aflag` | espera hasta que se ponga una bandera; mientras, repite el comando cada cuadro |
| `TimeRunning`, `TimeStopped`, `SetHour` | mueven el reloj |
| `SetPlayerPosition`, `SetPlayerDirection` | mueven al jugador |
| `NewCCStructure` | arranca otro slot con otro guion |
| `JumpifEqualsValue` / `...Long`, `JumpifBetweenValue` / `...Long` | saltan comparando un valor de 16 o 24 bits en cualquier dirección |
| `SetFlag`, `StoreValue`, `SetValueSort`, `SetValueLong` | escriben una bandera o un valor en cualquier dirección |
| `UNKTextrelated` | suma (con signo) a un byte, entre 0 y 255; pese al nombre, no tiene que ver con texto |
| `UNKCCchanges14` | suma (con signo) a un valor, entre 0 y 999 (corazones, felicidad...) |
| `ChangeMoney` | suma o resta dinero |
| `UNKCCchanges12` / `13` | espera un número de cuadros dejando hacer ciertas cosas (`$08FD` / `$08FF`, estado bit `$1000`) |
| `UNK2` | espera hasta que se ponga `$7F:1F5A` bit `$8000` |
| `MapScrolling` | mueve la cámara sola: velocidad X, velocidad Y y cuadros |
| `TransitiontoX`, `TransitiontoHouse` | cambian de mapa (`$098B`; `$0196` bit `$4000` arranca la transición) |
| `SetItemonHand`, `ChangeItemonHand`, `DropItemAnimation` | ponen o quitan un objeto en las manos |
| `UNKCCchanges4`, `6`, `10` | preparan una caminata del slot: velocidad, destino, punto de partida |
| `UNKCCchanges8`, `15`, `3` | eligen la animación del slot: una tabla del banco $B3 y una entrada (dada, o la dirección del jugador); el 3 además coloca el slot |
| `UNKCCchanges17` | si el jugador le habló al slot, este se gira hacia el jugador, se para y el guion salta |
| `UNKCCchanges5` (1E, 1F), `UNKCCchanges9` (2D) | **hablar con alguien**: si el jugador le habló al slot (+0C), no está usando ni mostrando una herramienta ni silbando, no carga nada, los bits 1-2 de `$7F1F60` están apagados y acaba de apretar A: el slot se gira hacia el jugador, el jugador se para (acción 0, bit `$40` de `$D2`, `$096E` = `$56`) y el guion salta. 2D tiene una segunda dirección, para cuando el jugador muestra algo en alto (acción 5 con un objeto: ese objeto también va a `$091E`) |
| `UNKCFD1` (2A) | empieza una animación de mapa (`StartMapAnimation`, el número del argumento) |
| `AudioRelated2` (0E) | cambia la música a la pedida en `$0110` (`UNK_Audio25`) |

Los 90 comandos están reescritos. Los tres últimos fueron los dos
fundidos de pantalla (0A, 0F) y `AudioRelated3` (2C, un efecto de
sonido), que esperan cuadros enteros (`WaitForNMI`): se comprueban con el
NMI adelantado (3.4), igual que el intérprete de comandos y el bucle de
los slots (más abajo).
| `UNK1`, `UNKCCchanges7` | destruyen el objeto del propio slot, o otro slot con su objeto |

**Cosas que se levantan.** Varios comandos usan la misma prueba de "manos
libres": el slot lo permite (+0C), pulsas A, no tienes nada en las manos y
no estás usando una herramienta, en un menú o en una escena. Cada uno
revisa las condiciones en un orden un poco distinto:

| Comando | Qué levantas |
|---|---|
| `UNKChickenRelated` | una gallina ($25) o un pollito ($26) |
| `UNKPickupMole` | el topo ($29); si no, cada 3 segundos puede moverse |
| `UNKFishing` | un pescado ($07) |
| `UNK3` | el objeto $08 |

**Los comandos 50-59** ($84:BBDA-$84:C013). HM-Decomp solo le pone nombre
al primero; aquí son `Event_Cmd51`...`Event_Cmd59`:

| Comando | Qué hace |
|---|---|
| 50 `UNKChickenRelated2` | deja lo que se carga cuando empieza una escena: una gallina o un pollito vuelve a su lugar del gallinero (tabla $83:CA10), cualquier otro objeto desaparece, el perro cargado vuelve a la granja (el mapa de la estación, en 120, 424); el jugador queda de pie |
| 51 | lo mismo, pero sin la animación, y de los otros objetos solo se suelta el topo ($29) |
| 52 / 53 | si se apretó A sobre el objeto del espacio con las manos libres, el objeto se va y se prende $7F1F60 bit 1 / bit 2 (con esos bits, A frente a la baldosa $F5 da la acción $1A) |
| 54 | `<texto> <líneas>`: un cuadro de texto que se cierra solo (modo de entrada 6, 120 cuadros) |
| 55 | `<dirección> <bit> <destino>`: salta si ese bit de la palabra de ahí está apagado |
| 56 | usar la herramienta en la mano |
| 57 | `<n>`: cambia la energía en n (`ChangeStamina`: el signo sale del bit 7, error 11) |
| 58 | `<baldosa> <x> <y>`: pone ese tipo de baldosa en la granja |
| 59 | la acción del jugador $1C |

**Más comandos:** `StartTextBox` y `StartTextBoxCopy` (28 y 29, el mismo
código) abren un cuadro de texto con `<texto> <líneas>`; `ResetFlag` (38;
el 39 y el 40 apuntan al mismo código, ver el error 13) apaga un bit de una
palabra en cualquier lugar; `EditTileonMap` (75, $84:BA10) pone un tipo de
baldosa en el mapa actual, como el 58 en la granja. `Event_UNKTools`
devuelve al cobertizo las herramientas de la mano y de la mochila (un bit
por herramienta en $7F1F00), pero ningún número de comando llega a él.

**El comando 37, `UNKPlayerPos`** ($84:AB27), cada cuadro, para un objeto
del grupo $17 que el jugador puede cargar (probablemente el hijo: cuando el
jugador lo deja, empieza el guion $17/$14). Sus pedidos son bits de
$7F1F5C:

- cada 120 cuadros ($0939) una animación de espera elegida por el byte al
  azar $DE (guiones $4B-$50; fuera del lugar $15 de $7F1F32, el guion $80);
- bit 6: mirar hacia donde dice su byte +2 (guiones $28-$2B);
- bit 10: caminar hacia el jugador por el eje más largo (como el perro);
- bit 5: lo toman en brazos ($D2 bit 4: cargando; el objeto se va);
- A sobre él con las manos libres y lugar enfrente: el jugador lo levanta
  (acción $17); el bit 4 dice "tocado en este cuadro".

- **Correr los guiones** (`IterateCCStructures`, en cada cuadro): salvo
  que espere una caja de texto o el jugador esté en la acción 3, un número
  al azar va a `$DE` y cada uno de los 49 slots en uso corre su guion:
  mientras su espera (+10) sea 0, un comando tras otro
  (`EventInstructionInterpreter`: el byte del comando elige una de 90
  rutinas, `$84:9419`); si no, la espera baja en uno. `$DE` avanza $33 por
  slot, así que los slots que tiran el dado en el mismo cuadro sacan
  números distintos; los slots con sprite lo actualizan (`SUB_848286`). Al
  final se borran los bits que duran un cuadro.

### 5.11 Los mapas

- `$22` es el **mapa actual**:

  | Mapas | Lugar |
  |---|---|
  | 00-03 | granja (una por estación) |
  | 04-07 | pueblo |
  | 08-0B | festivales |
  | 0C-0F | cruce |
  | 10-13 | montaña |
  | 15-17 | casa (niveles 1-3) |
  | 1B | iglesia |
  | 27 | establo |
  | 28 | gallinero |
  | 29 | cueva |
  | 31-34 | cima de la montaña |

- **El mapa $30 cuelga el juego.**
- **Cargar las casillas de un mapa** (`LoadMap`): los 4.096 tipos de
  casilla del mapa `$22` al mapa actual (`$09B6`). La granja (mapas 0-3)
  sale de su propia copia (`$7E:A4E6`, así se conserva el campo); los
  demás, de su entrada en `MapPointerTable` ($82:B174, 3 bytes cada una).
  `$0D-$0F` recibe la tabla de propiedades de las casillas del mapa
  (`UNK_PointersTable`, $82:B294, mismo índice).
  Rareza: el índice (mapa x 3) se calcula en 8 bits, así que un mapa desde
  el $56 daría la vuelta (ninguno así tiene casillas).
- **El dibujo del mapa** está completo en la WRAM (`$7E:2000`); la memoria
  de video solo tiene una ventana de 64 × 64 que se va rellenando desde
  ahí.
- **Cambiar una casilla** (`ChangeMapTile`) hace tres cosas: escribe el
  dibujo nuevo en ese mapa en memoria, deja en la cola de DMA la copia a
  la memoria de video y, si hace falta, cambia el tipo de casilla. Lo que
  hay que poner sale de la tabla `$81:B363`.
- **Dibujar el mapa entero** (`UNK_PartialMap`): para cada una de las 64 ×
  64 baldosas toma el dibujo del byte +0 de la propiedad de su tipo (0:
  nada). Los tipos $73-$79 se unen con sus vecinos de la misma clase
  (cercas y parecidos, "autotiling"): los cuatro vecinos forman una máscara
  (izquierda 8, derecha 2, arriba 4, abajo 1) y la tabla `$82:B02A` dice
  cuál de los dibujos sumar. Es larga: no termina en un cuadro.
- **Datos comprimidos** (`DecompressTileMap`, LZSS). Los datos empiezan con
  su tamaño y siguen en grupos: un byte de control y 8 elementos. Cada bit
  del byte de control (el más bajo primero) dice qué es el siguiente
  elemento: 1 un byte que se copia tal cual; 0 dos bytes que significan
  "copia de 3 a 34 bytes desde este lugar de lo ya escrito" (un lugar de 11
  bits y un largo de 5 bits + 3). Lo escrito se recuerda en una ventana de
  2 KB en $7F:0000, que empieza en ceros y se escribe desde $07DE (2048 -
  34, la disposición usual de LZSS). Las repeticiones (una fila de la misma
  baldosa, el mismo dibujo otra vez) ocupan dos bytes en vez de muchos.

**Huellas en la nieve** (`CODE_81A58F`). Cada 8 pasos el juego recuerda
dónde está el jugador ($0907 / $0909); en invierno, en la granja o en la
montaña, el sitio recordado 8 pasos antes recibe una huella si su casilla
es nieve (tipos 1, 2, $A8, $F6). Solo cambia el dibujo en la memoria de
video, no el mapa: las huellas desaparecen al volver a cargar el mapa.

**Animaciones de mapa** (`CODE_81A600`, cada cuadro, tras
`StartMapAnimation`). Un guion de pasos de 7 bytes: un cambio de casillas,
su X y su Y, y cuántos cuadros esperar; `$FFFF` lo termina y `$FFFE` salta
a otro guion. Las judías mágicas creciendo son una.

### 5.12 Objetos con sprite

- **Objetos del juego** (`$019C`, 40 de 36 bytes): todo lo que tiene
  sprite en pantalla. `CreateGameObject` toma uno libre.
- **Sprites:** cada uno se arma con piezas de 16 × 16 (hasta 12). Los
  números 0-$261 se describen en el banco $86 y $262-$44F en el $87.
- **Gráficos de las piezas:** si el dibujo de una pieza ya está en video
  se reutiliza; si no, va a una cola que vacía el NMI.
- **El cargador** (`CODE_858CB2`, en el NMI): mientras la cola tiene
  piezas y al presupuesto del cuadro (`$C7`) le quedan $100 bytes para una
  más, toma la pieza siguiente y le da un hueco de gráficos: uno nuevo
  mientras se hayan repartido menos de 64 (`$0846`), y después el primero
  libre (`$7E:A320` = 0) desde un comienzo que va rotando (`$0848`, 1-64).
  La pieza que tenía el hueco queda como no cargada (`$7F:0F00` = $FF), la
  nueva lo recibe, el número del hueco se escribe donde decía la entrada
  de la cola, y una copia va a la lista que el NMI manda a video
  (`$7E:A420`, 6 bytes cada una: dirección de los gráficos, banco y
  dirección en VRAM; $FFFF la termina). `$0742` = cuántas piezas siguen
  esperando.
- **Objetos del mapa** (`$7E:B4E6`, 10): los efectos que dejan las
  herramientas, como la hierba cortada o el golpe en la piedra.
- **Destruir uno** (`CODE_8581A2`, `src/objects.c`): cada hueco de
  gráficos cuenta cuántas piezas lo usan ($7E:A320); destruir un objeto
  devuelve los huecos de sus piezas y libera el registro. Pero si alguna
  de sus piezas aún espera en la cola, no se libera nada: $0740 cuenta un
  rechazo más y el objeto simplemente se queda. Los guiones destruyen
  objetos con los comandos `Event_UNK1` (el suyo) y `Event_UNKCCchanges7`
  (el de otro slot).
- **Moverlo y cambiarlo:** `CODE_8580B9` mueve un objeto (volteo, X, Y);
  `CODE_858100` le pone otro sprite: se liberan las piezas viejas (con el
  mismo rechazo de arriba) y se cargan las nuevas. `CODE_8581CB` vuelve a
  leer un objeto en esas variables.
- **Sprites volteados:** la distancia de una pieza pasa a ser -distancia -
  16 cuando el objeto está volteado en ese sentido (`InvertIfFlipedX` /
  `Y`): el sitio reflejado de una pieza de 16 píxeles.
  `ClearSpriteDataTables` vacía la cola y libera todos los huecos de
  gráficos (al cargar un mapa).
- **Animar los objetos** (`CODE_8582C7`, cada cuadro): la entrada de un
  sprite (+0C) es una lista de cuadros de 3 bytes: la lista de piezas del
  cuadro y cuántos cuadros dura; una lista de piezas 0 vuelve al primer
  cuadro (un bucle). Cada objeto descuenta su temporizador (+12); al
  llegar a 0 pasa al cuadro siguiente (+0E): libera sus piezas viejas y
  carga las nuevas, salvo que alguna vieja siga esperando en la cola
  (entonces lo vuelve a intentar al cuadro siguiente). Una duración $FF
  se queda para siempre. Un temporizador en $FE libera el objeto (piezas
  y registro), pero solo puede encontrarse ahí si el objeto acaba de
  crearse o de recibir otro sprite (copian la duración del primer cuadro):
  un cuadro posterior de $FE se descuenta apenas se carga, así que solo
  dura 254 cuadros.
- **El orden de dibujo** (`GetOBJOrderToDraw`): una especie de orden por
  profundidad alrededor del objeto del jugador (`$0905`), en `$084C`:
  primero los objetos cuya Y no es menor que la del jugador, en su propio
  orden, después el jugador, y los que están más arriba (Y menor) llenan
  la lista desde el final hacia atrás. Solo cuenta de qué lado del
  jugador están; no es un orden completo.
- **Vaciar la tabla de sprites** (`PresetsWRAMCopyofOAM` y una copia
  exacta, `PresetsWRAMCopyOAMCopy`): la copia en WRAM de los 128 sprites
  del hardware ($7E:A000) toda en X = Y = $F0, fuera de la pantalla, y la
  tabla alta ($7E:A200) a 0. `InitializeOBJs` libera los 40 objetos (y
  $DC = 0) y vacía la tabla; los huecos de gráficos quedan como estaban.
- **El objeto del propio jugador** ($0905): su animación ($0901) elige un
  sprite y un volteo de `DATA8_81D210` (3 bytes cada una; el byte del
  volteo x 64 se guarda en $090F). `CODE_81CFA0` lo crea en la posición
  del jugador; cada cuadro `CODE_81CFE6` lo lleva hasta el jugador y,
  si la animación cambió ($0903 = la que se muestra), le da el sprite
  nuevo; después `CODE_81D03D` lo relee y, cuando su cuadro dura para
  siempre ($FF: la animación terminó), apaga el bit $2000 del estado ($D2).
- **Un slot que camina** (`SUB_848331`): con el bit 5 de +01, cada +05
  cuadros el slot se mueve (+03, +04), bytes con signo, sumados a su
  posición.
- **Un slot que pasea solo** (`SUB_8483CC`, 531 instrucciones; la gente
  del pueblo, los animales del establo): con el bit 3 de +01, justo
  después de cada paso (+05 = +06):
  - si no le quedan pasos (+08 = 0) elige un paseo nuevo de +07 pasos: la
    dirección +0F si el guion la impuso (1 abajo, 2 arriba, 4 derecha,
    8 izquierda) o si no la al azar de $DE (0 abajo, 1 arriba, 2 derecha,
    3 izquierda; otro valor: quedarse quieto);
  - si le quedan, sigue con un paso menos.
  - Sin el bit 7 de +01 el slot no sale de su zona: su casa (+1E, +20) más
    o menos el alcance (+09 de lado, +0A de alto). Un paso que se pasaría
    lo deja quieto (sin paso, +08 = 0, la animación de estar parado de la
    tabla de +33).
  - Al empezar a andar: dirección +02, paso (+03, +04) = (0, 1), (0, -1),
    (1, 0) o (-1, 0), la pausa +0B en +05/+06 y la animación de andar de la
    tabla de +36; en los dos casos +01 |= 4 (cambió la animación).
  - Luego mira adelante (CEEEE con el mapa, solo con el bit 7; el tipo de
    la primera esquina queda en +0E): se para si choca, si delante hay un
    tile $FF o $C0-$CF, o si el jugador está en medio (CDDDD). El tile
    $E1 solo deja pasar a los slots $18-$23 (+3F). Pararse: sin paso,
    +05/+06 = +0B y se borra la dirección impuesta.
  - Rareza: sin el bit 7 igual mira el tile de la segunda esquina ($EB),
    que es lo que dejó la última comprobación de cualquier otro.
  - El "azar" de $DE: el bucle de los slots le suma $33 antes de cada slot
    activo, así que cada uno recibe un valor distinto en el mismo cuadro.
- **El sprite de un slot, cada cuadro** (`SUB_848286`): si no está
  congelado (+01 bit 4), el slot pasea y da su paso (las dos de arriba).
  Luego su objeto con sprite (+12) recibe la posición (+1A, +1C), la
  animación (+14) y el sprite (+16), y según +01: bit 6 lo destruye, bit 1
  lo crea (el objeto nuevo queda en +12), bit 2 le cambia el sprite si
  +16 no es el de antes (+18), y si no, solo lo mueve. Al final +18 = +16
  y se borran los bits de un solo uso 1, 2 y 4.

### 5.13 El final

Al terminar el año 3 (verano, día 30), la evaluación calcula el **puntaje
del rancho** (`RanchMasteryCalculator`, de 0 a 999). Suma:

| Concepto | Puntos |
|---|---|
| Dinero | /128, máximo 78 (= 99.840 G) |
| Cada vaca y gallina | 3 |
| Energía máxima | (máx − 100) / 2 |
| Corazones de cada chica | /16 |
| Tomates, maíz, papas y nabos enviados | /16 cada uno |
| Felicidad | /32 |
| Cada ampliación de la casa | 16 |
| Cada hijo | 16 |
| Casa pintada | 32 |
| Reloj | 22 |
| Caparazón de tortuga | 21 |
| Desarrollo de la granja | /2 |
| Felicidad de cada vaca | /8, máximo 25 |

**La presentación** (`MapEvent_EBA6`, mapas $3D y $4B). Cada mañana del
final el jugador se despierta a las 6:00 y el paso `$7F1F47` dice qué
parte sigue; una parte que no corresponde se salta:

| Paso | Parte (grupo de guiones) |
|---|---|
| 0 | sin esposa, felicidad menor que 100, sin vacas ni gallinas: el final malo ($2F), derecho a los créditos |
| 1-4 | 7 o más vacas ($30), 1-6 vacas ($31), gallinas ($32), el cariño de la mejor vaca $C0 o más ($33) |
| 5-8 | 200 o más de maíz, tomates, nabos, papas enviados ($34, $35, $37, $36) |
| 9, $0A | sin esposa y felicidad 100+ / 800+ ($38, $39) |
| $0B-$0F | la esposa: Maria, Ann, Nina, Ellen, Eve ($3A-$3E) |
| $10 | sin esposa, felicidad 200+ y los corazones de las cinco chicas sumando 1500+ ($3F) |
| $11, $12 | un hijo, dos hijos ($40, $41) |
| $13 | todo: dos hijos, felicidad 900+, vacas y gallinas, las 10 bayas de poder, 10.000 G y 100 abrazos al perro ($42) |
| $14 | la chica con más corazones ($09B3) y los créditos ($46) |
| $20 | el puntaje del rancho (`RanchMasteryCalculator`) |

### 5.14 El cuadro de texto

Los diálogos se muestran en **BG3**, la capa de fondo que se usa para el
texto.

- **Abrir** (`StartTextBox`, X = número del texto): busca el texto en
  `Text_Pointer_Table` ($83:9BF6, 3 bytes por texto) y deja su dirección
  en $01-$03. Reinicia la "impresora" (la siguiente letra va a la VRAM
  $5010), elige una sola vez la altura del cuadro (2 líneas si $090D es
  menor que $81, si no 1), sube BG3 para que aparezca el cuadro, **detiene
  el reloj** y levanta la bandera "hay un cuadro abierto" ($7F:1F5A bit 14)
  para que otros sistemas esperen.
- **Cerrar** (`CloseTextBox`): lo contrario. Baja BG3, borra el cuadro
  copiándole encima un mapa de casillas vacío ($9C:BBA3) y deja correr el
  reloj otra vez, salvo que ya estuviera detenido antes ($0196 bit 5).
- **La flecha de "hay más texto"** (`BlinkTextBoxArrow`): mientras el
  cuadro espera un botón, cada 20 cuadros la flecha cambia entre sus dos
  dibujos. Es un bloque de 2×2 casillas que `UNK_SetDMA2` copia a la VRAM
  con dos DMA pequeños (fila de arriba y fila de abajo). Tiene 12 lugares
  posibles.

**Cómo llega una letra a la pantalla.** El cuadro no tiene un lugar por
letra en su mapa de casillas. En cambio, el mapa del cuadro apunta a una
fila de casillas en orden, y la impresora *dibuja* cada letra en la
casilla siguiente:

- La **fuente** está en la ROM en grupos de 64 letras (8 por fila). Cada
  letra son dos casillas de 8×8, una arriba de la otra.
  `FindLetterGraphics` convierte un código de letra en la dirección de sus
  gráficos.
- `DrawLetter` copia esas dos casillas a la VRAM en el cursor ($0185) con
  `UNK_SetDMA1` (dos DMA pequeños: mitad de arriba y mitad de abajo).
- `AdvanceLetterCursor` mueve el cursor una casilla; al final de una línea
  salta las mitades de abajo.
- Los **números** (dinero, días...) se imprimen cifra por cifra con
  `PrintDigit`: resta la potencia de diez (1, 10, 100...) hasta que el
  número queda negativo, eso da la cifra, y se salta los ceros del
  principio. Los códigos de las cifras son $BC-$C5.
- Mientras el texto espera un botón, `BlinkTextCursor` alterna cada 20
  cuadros la letra del cursor con una marca.

**La impresora** (`PrintTextFrame`, `ADDDDFFFF` en HM-Decomp) corre una
vez por cuadro. Un texto es una lista de códigos de 16 bits. Un código
normal es una letra: se imprime **una cada 5 cuadros**, cada una con un
pequeño efecto de sonido (`UNK_Audio19`, ver 5.15). Algunos códigos son
órdenes:

| Código | Significado |
|---|---|
| `$00A2` | esperar un botón (el cursor parpadea abajo) |
| `$00B1` | un espacio: el cursor avanza y el siguiente código se lee enseguida |
| `$FFFC n k` | el número de la variable k, con n cifras, una cifra por cuadro (dinero, día...) |
| `$FFFD n k` | el nombre k (el jugador, la granja, el perro...), n letras, sin sonido |
| `$FFFE n` | elegir entre n opciones (el cursor parpadea en la elegida) |
| `$FFFF` | el fin del texto |

Las variables de los números están en `Text_Number_Pointer_Table`
($83:98EE: dirección y tamaño en bytes) y los nombres en
`Text_Name_Pointer_Table` ($83:9AAE).

En una partida nueva la impresora corre 2.424 veces y el comprobador
comparó cada una, junto con sus 248 efectos de sonido (ver 3.4).

### 5.15 Hablar con el chip de sonido

El chip de sonido es un segundo procesador. El juego y el chip comparten
cuatro "puertos" de un byte. La conversación siempre es igual:

1. El juego lee el puerto 3 dos veces seguidas hasta que las dos lecturas
   coinciden (el chip podría estar cambiándolo en ese momento) y espera a
   que diga **1: "listo"**.
2. Escribe la orden en los puertos 0-2 y **2 en el puerto 3: "ahí va"**.
3. Espera a que el puerto 3 diga **4** (orden simple) u **8** (orden con
   argumentos): **"hecho"**.
4. Escribe **0** en el puerto 3.

Órdenes vistas hasta ahora: `$0A` toca un efecto de sonido (con tres
argumentos: $0114, $0115, $0116; cada letra de texto usa 3, 6, $0A), `$0B`
lleva un argumento, `$0C` ninguno. Antes de un efecto el juego pregunta si
el chip está ocupado (puerto 0, bit 4) y, si lo está, simplemente se salta
el sonido.

En una partida nueva, solo el texto manda 248 efectos de sonido: todos se
comprobaron en vivo, con la grabación de 3.4.

**La música y las subidas** (`src/audio.c`). El chip tiene su propia
memoria de 64 KB, así que hay que mandarle los instrumentos (muestras de
sonido) y las canciones. Todas las órdenes:

| Orden | Rutina | Qué es |
|---|---|---|
| $01 | `UNK_Audio4` | los datos del programa de sonido ($3F2 bytes desde $B2:D120), al encender |
| $02 | `UNK_Audio3` | los 10 instrumentos que están siempre |
| $03 | `UNK_Audio9`, `UNK_Audio12` | los instrumentos de una canción (hasta 11), o de un efecto de sonido |
| $04 | `UNK_Audio10` | parar un canal (y olvidar los instrumentos anotados desde él) |
| $05 | `UNK_Audio11` | las notas de una canción |
| $07 | `UNK_Audio13` | (sin argumento; el que recibe se guarda y nunca se manda) |
| $08, $09 | `UNK_Audio14`, `UNK_Audio6` | órdenes de dos argumentos que se usan al cambiar de canción (1 y $10 o $40: quizá un fundido) |
| $0A-$0C | ver arriba | efectos de sonido y dos pequeñas |

**Antes de todo eso, el propio programa de sonido** (`UNK_Audio1`, una
vez, al encender): el chip arranca en una ROM de arranque diminuta con su
propio protocolo. El juego espera a que los puertos 0-1 lean $BBAA,
escribe la dirección ($0500) en los puertos 2-3, 1 en el puerto 1 ("van
datos") y $CC en el puerto 0, y espera a que vuelva el $CC. Luego van uno
a uno los $FD6 bytes desde $AD:8000: el byte en el puerto 1 y su índice
(0, 1, 2... que vuelve a 0 después de $FF) en el puerto 0, cada uno
cuando el chip devolvió el índice anterior. Al final, otra vez la
dirección, 0 en el puerto 1 ("ejecútalo") y el índice + 4 en el puerto 0.
El bucle está preparado para más bloques (lo decide la bandera de
desbordamiento), pero el juego arma siempre la última cabecera sin más:
hay un solo bloque. Después el chip ejecuta el programa del juego y ya no
contesta el protocolo de arranque, así que esta rutina solo se comprueba
en vivo (`HM_LIVE`), al encender.

Los datos grandes viajan **de 3 en 3 bytes**: el juego manda primero
cuántos grupos de 3 (el tamaño entre 3, redondeado hacia arriba) y luego
cada grupo, esperando "0", borrando el puerto 3 y esperando "1" otra vez
después de cada uno. Las tablas: 26 canciones (`$83:8F83`, 14 bytes cada
una: dónde están las notas y los 11 instrumentos), 40 instrumentos
(`$83:90EF`, 10 bytes cada uno, y sus tamaños en `$83:8EFF`), y qué
instrumento necesita cada uno de los 50 efectos de sonido (`$83:927F`).

$0110 es la canción que se quiere y $0117 la que suena; cuando difieren,
`UNK_Audio21`, `UNK_Audio22` y `UNK_Audio25` mandan el cambio.
**Qué canción pide un mapa** (`UNK_Audio5`, al entrar): desde las 6 de la
tarde ninguna ($FF), salvo en el mapa $1E; con el bit 4 de `$0196` la
canción $16; con el bit 1 de `$0196` los mapas por debajo del $57 tienen
la $13 o la $14 (según el rango) y los $57-$5A ninguna; si no, cada mapa
tiene una lista de cuatro canciones, una por estación, en la tabla
`$80:B8E7` ($FFFF: ninguna).
`Unk_Audio23` no tiene nada que ver con el sonido: multiplica dos números
de 16 bits con el multiplicador de 8 x 8 de la consola.

*Rarezas:* `UNK_Audio6` solo manda su orden cuando el chip dice que está
ocupado (al revés que las demás). Después de los instrumentos de una canción,
`UNK_Audio9` manda siempre también el instrumento 6: lo hace cuando la
lista termina en huecos vacíos, y las listas de las 26 canciones terminan
así. Y otra vez una dirección de HM-Decomp va un byte antes:
`UNK_Audio14` empieza en $83:8D39, no en 8D38. `UNUSED2` ($83:8376), que nadie llama, solo
suma uno a la cuenta de instrumentos ($0103).

### 5.16 Las partidas guardadas

El cartucho tiene una RAM con batería (SRAM) con **dos ranuras de 4 KB**.
Lo que se guarda (`SaveGameSlot`), al principio de la ranura:

| Posición | Qué |
|---|---|
| +00-+03 | año, estación, día de la semana, día |
| +04-+0A | bolsas de semillas y comida |
| +0B, +0C | cantidad de vacas y gallinas |
| +0D-+1A | clima de mañana, energía máxima, objeto en la mano, agua, más banderas |
| +2E | 1 = esta ranura se guardó al último |
| +2F | suma de comprobación (16 bits) |
| +31-+38 | cosechas enviadas (maíz, tomates, nabos, papas) |
| +39-+3B | dinero (24 bits) |
| +3C-+3F | las letras "FARM": la ranura es válida |
| +40-+7E | madera, pasto, corazones de las chicas, banderas de eventos, el perro, felicidad, la familia |
| +80-+97 | nombres: jugador, perro, caballo, hijos (y los objetos de la bodega) |
| +98 | las 13 gallinas (8 bytes cada una) |
| +100 | las 12 vacas (16 bytes cada una) |
| +1C0 | las casillas de la granja, hasta el final de la ranura |

- La **suma de comprobación** es la suma de las 2.048 palabras (16 bits)
  de la ranura, contadas con +2E y +2F-+30 en cero.
- Al encender, `CheckSRAMIntegrity` revisa las dos ranuras: "FARM" y la
  suma. Una ranura que falla se **borra** (sus primeros 2 KB) y recibe
  "FARM" de nuevo, así que aparece vacía.
- La pantalla de título lee solo la fecha y el nombre de cada ranura
  (`LoadGameSlotResume`); una ranura con día 0 está vacía.
- **Cargar** (`LoadGameSlot`) es exactamente lo inverso de guardar, en el
  mismo orden, y no comprueba nada (ni "FARM" ni la suma: eso se hizo al
  encender). Cada letra de los cinco nombres va también a una segunda
  copia de 16 bits ($08D5-$08FC, dos bytes por letra). La granja primero
  vuelve a la de fábrica (`LoadDefaultFarmMap`) y después se copia encima
  la parte guardada (casillas desde la $C0).
- **Una partida nueva** (`NewGameSetup`) da a esas mismas variables sus
  valores iniciales: año 1, primavera, lunes 1, una bolsa de semillas de
  pasto, 100 de energía, 300 G (el dinero se guarda en decenas: 30), el
  galpón con las herramientas básicas ($0F) más la regadera y las
  semillas de pasto ($88), todos los nombres en blanco (carácter $B1), lo
  demás en 0, y la granja de fábrica. Las vacas, las gallinas y las
  copias de 16 bits de los nombres de los hijos no se tocan.
  Rareza: el nombre del caballo se borra dos veces (seguro que la segunda
  vez el código tenía que hacer otra cosa).

### 5.17 Botones y menús

Cada cuadro `InputTypeSelector` mira el **modo de entrada** ($019A): 1
caminando (5.22), 2 sin moverse, 3 una lista dentro de un cuadro de texto,
4 menús, 5 la pantalla de nombres, 6 un cuadro de texto que se cierra solo
(con $019B bit 2, a los 120 cuadros; después se vuelve a caminar). En los
menús, el **número de menú** ($95) dice cuál está abierto:

| Menú | Qué hacen los botones |
|---|---|
| 1 | al jugador lo mueve el control de un evento ($08FD); con $7F1F5A bit $8000, Start lo salta (menú 9, $7F1F5C bit 2) |
| 2, 3 | Start pasa al menú 6 (cuando lo permite $7F1F5A bit $8000) |
| 5 | el menú del título: tres opciones, arriba y abajo dan la vuelta; A abre el menú 8, 7 o $0A |
| 7 | elegir ranura para guardar: arriba / abajo cambian la ranura, B cancela, A acepta |
| 8 | elegir ranura para cargar: igual, pero A solo funciona en una ranura con partida (si no, un sonido de "no") |

La respuesta queda en $94 (1 = cancelar, 2 = aceptar). Cada movimiento
hace un efecto de sonido: 3 al moverse, 1 al aceptar, 2 al rechazar.

**La lista en un cuadro de texto** (modo 3): 22 lugares en dos columnas de
11 y una flecha. Arriba y abajo mueven dentro de la primera columna y dan
la vuelta (en la segunda no hacen nada); derecha e izquierda saltan a la
otra columna. Usan la **repetición automática** del botón, así que al
mantenerlo sigue moviéndose. A cierra la lista.

**La pantalla del nombre** (modo 5). Las letras están en una cuadrícula
de tres páginas (las cuadrículas de $82:EBF8, $82:EE30 y $82:F0D0, con 71,
84 y 49 casillas). Cada casilla tiene 8 bytes: su x y su y, la casilla a la
que lleva cada flecha, la letra y un código (1-3: mostrar la página 0-2,
4: listo). Las flechas (con repetición) mueven el cursor $0991 a la casilla
que dice la cuadrícula, así que los movimientos vienen "precocinados". A
agrega la letra (hasta 4, en $0885) u obedece el código; "listo" necesita
al menos una letra. B borra la última letra (una letra vacía es $B1).
Cambiar de página desplaza BG1: las tres páginas están en la misma imagen,
la 1 a la derecha de la 0 y la 2 debajo.
El sprite del cursor se pone en la x y la y de la casilla (`SUB_82EB57`,
en $099B / $099D).

**Mientras hay un cuadro de texto abierto** (modo 2, `ImmobileInputs`):

- Mientras se imprimen las letras, **mantener A** hace que la siguiente
  salga enseguida: así se acelera el texto.
- Cuando el texto espera un botón, A borra el cuadro y sigue.
- Al final del texto, A cierra el cuadro y puedes volver a caminar.
- En una **elección**, las opciones están en dos columnas de tres; arriba
  y abajo mueven dentro de una columna, izquierda y derecha saltan a la
  otra, y A cierra el cuadro con la opción elegida en $018F.

### 5.18 Desplazar el mapa

Un mapa puede ser más grande que la pantalla, y más grande que lo que
cabe en la memoria de video. El mapa entero está en la WRAM (`$7E:2000`,
ver 5.11); la memoria de video solo guarda una **ventana** de 64 × 32
baldosas alrededor de la cámara. Cuando la cámara se mueve, el juego trae
la fila o la columna que está a punto de verse, un poco antes de que la
veas.

**Cada cuadro, para cada dirección** (abajo `UpdateBGOffset`, arriba
`SUB_80A0E1`, derecha `UNK_StaticMapScroling`, izquierda `SUB_80A152`):

1. La posición de la cámara ($F5 horizontal, $F7 vertical) se copia al
   desplazamiento de BG1 ($013C / $013E): eso es lo que mueve la imagen.
2. Si la cámara está en uno de sus límites ($ED izquierda, $EF arriba,
   $F1 derecha, $F3 abajo), no pasa nada más.
3. La velocidad $1E se recorta para no pasarse del final.
4. Si el mapa se desplaza (bit 0 de $0196), la velocidad se suma (o se
   resta) a un **contador de píxeles dentro de la baldosa** ($20
   horizontal, $21 vertical, de 0 a 7). Cuando llega a 8 (o baja de 0) da
   la vuelta, y hace falta una línea nueva de baldosas.

**La línea nueva** (`SUB_80A18D` la fila de abajo, `SUB_80A308` la de
arriba, `SUB_80A481` la columna de la derecha, `SUB_80A617` la de la
izquierda):

- El mapa en la WRAM está guardado en bloques de 32 × 32 baldosas (una
  "pantalla"). Una fila o columna de la ventana suele cruzar de una
  pantalla a la siguiente, así que se copia en **dos tramos** que saltan a
  la pantalla siguiente al llegar a su borde.
- Las 32 + 32 baldosas van a dos búferes ($0746 y $07C6) y se programan
  dos DMA (canales 0 y 1) que las mandan a la memoria de video en $6000 +
  la posición. Para las columnas el DMA usa el modo "de 32 en 32", así las
  baldosas quedan una debajo de otra.
- Después la posición en la memoria de video ($14 filas, $10 columnas) y
  en la WRAM ($16 filas, $12 columnas) avanza, dando la vuelta dentro de
  la ventana.

| Variable | Qué guarda |
|---|---|
| $10 / $14 | dónde va la próxima columna / fila en la memoria de video |
| $12 / $16 | de dónde sale en el mapa de la WRAM |
| $1A | el largo de una fila del mapa |
| $1C | el desplazamiento de la cámara dentro de la fila |
| $1E | la velocidad de la cámara (píxeles por cuadro) |
| $20 / $21 | píxeles dentro de la baldosa (0-7), horizontal / vertical |

**Al entrar a un mapa** (`SUB_809EBC`): la cámara se pone de modo que el
jugador quede en el centro de la pantalla (128 píxeles), salvo que así se
viera más allá de un límite; entonces se queda en el límite y el jugador
queda corrido. $090B / $090D dicen dónde está el jugador en la pantalla.

**El segundo fondo, BG2** (`SUB_809F61`, cada cuadro):

- en el cobertizo de herramientas (mapa $26), nada;
- en los mapas desde $31 (la cima de la montaña y otros) un paralaje: a
  lo ancho, BG2 sigue a la cámara; arriba y abajo se mueve a la mitad de
  velocidad. Además duplica la velocidad de la cámara;
- en los otros mapas depende de $0196: bit 1 o bit 3, BG2 queda fijo a la
  cámara; bit 2, **las nubes**: se mueven con la cámara y cada 10 cuadros
  se corren un píxel a la derecha y uno hacia arriba.

### 5.19 Los colores del día

El SNES tiene 256 colores en pantalla a la vez (la **paleta**, guardada en
la CGRAM de la consola): los primeros 128 para los fondos y los otros 128
para los sprites. Un color son 15 bits: 5 de rojo, 5 de verde y 5 de azul
(de 0 a 31 cada uno). El juego guarda tres copias en la WRAM:

| Dónde | Qué |
|---|---|
| `$7F:0900` | lo que se manda a la consola |
| `$7F:0B00` | una paleta que se está preparando (la de la noche) |
| `$7F:0D00` | los colores que se ven ahora |

**Cada mapa tiene una paleta para cada parte del día** (la tabla
`$80:BB5C`, 6 bytes por mapa): antes de las 7:00, de 7:00 a 14:59, de
15:00 a 16:59, de 17:00 a 17:59 y de las 18:00 en adelante. $FF quiere
decir "deja la que hay" (dentro de las casas, por ejemplo, la paleta no
cambia).

**El cambio es lento**:

1. Cuando la hora entra en otra parte del día, `SetPaletteToLoad` ve que el
   mapa tiene otra paleta y llama a `SetTimeofDayPalette`, que apunta
   `$04-$06` a ella. Desde las 18:00 el destino es la paleta de noche ya
   preparada en `$7F:0B00`.
2. `TransitionTimeofDayPalettes` corre cada cuadro mientras el tiempo
   corre. Cada **32 cuadros** mueve cada color **un paso** en rojo, en verde
   y en azul hacia el destino, y manda la paleta a la consola por DMA.
   Como un componente puede estar a 31 pasos, un cambio completo puede
   tardar 31 × 32 cuadros, unos 16 segundos.
3. Cuando ningún color cambió, el cambio terminó: se suelta el destino y
   la paleta nueva queda anotada como cargada (`$017B`).

Antes de las 18:00 solo cambia la mitad de los fondos; desde las 18:00
también la de los sprites. Los colores 1-11 (1-15 cuando el bit 2 de $0196
está apagado; HM-Decomp llama a ese bit "soleado") nunca se tocan:
probablemente son los del cuadro de texto y la ventana, que no deben
oscurecerse.

**La paleta de noche** la prepara `UNK_CustomisePalette` al entrar a un
mapa: los fondos de la paleta "18:00" del mapa y los sprites de su paleta
de sprites de noche (tabla `UNK_Table11`, $80:BE44, dos por mapa: día y
noche).

**Algunos colores de sprites se hacen a medida** (`CustomiseSpritePalette`):

- Tres colores de la última paleta de sprites (fila 15, colores 10-12)
  cambian con la **estación** (en los mapas de la montaña, $10-$13, con el
  mapa) y con la noche.
- Si la esposa está embarazada o hay un hijo, tres colores de la fila 11
  (8-10) cambian con la **etapa** (el primer bit prendido de $7F1F66, bits
  0-4) y con la noche: el sprite del bebé.
- "Noche" para estos colores es: desde las 18:00 en un mapa de afuera
  (menor que $15, o $31 en adelante). Dentro, los sprites mantienen sus
  colores de día.

*Nota:* HM-Decomp pone `CustomiseSpritePalette` en $80:93A3, pero ese byte
es el `RTL` de la rutina anterior; el juego llama a $80:93A4.
`CustomiseSpritePalette` además lee el byte $81 sin haberlo puesto; en el
juego siempre vale 0, así que no cambia nada.

### 5.20 Chocar con las cosas

Antes de dar un paso, el juego hace tres preguntas. Las entradas están en la
página directa: $DF / $E1 la posición (x, y) de quien se mueve, $E3 cuánto
mirar hacia adelante, $E5 / $E7 el paso, y A la dirección (0 abajo, 1 arriba, 2 derecha,
3 izquierda). Los nombres de botones de HM-Decomp tienen izquierda y derecha
cambiadas: en el control del SNES el bit $0100 es Derecha y $0200
Izquierda, así que su `PlayerActionLeft` en realidad es "derecha".

- **El mapa** (`CEEEE`): prueba las dos esquinas del lado que avanza, a 6
  píxeles a cada lado del centro y $E3 + el paso + 6 píxeles adelante. Cada
  esquina (`CFFFF`) lee la propiedad de la baldosa:

  | Bit | Qué es sólido |
  |---|---|
  | 0 | toda la baldosa |
  | 1 / 2 | la mitad izquierda / derecha (x & 8) |
  | 3 / 4 | la mitad de arriba / de abajo (y & 8) |
  | ninguno | nada; pero cargando algo, las baldosas $C0-$CF bloquean |

  La respuesta es (primera esquina) × 2 + (segunda esquina). Cuando algo
  bloquea, la rutina también dice cuánto se mete la esquina en la baldosa,
  para que quien llama se detenga justo en el borde o se deslice por él.
- **Los objetos** (`CBBBB`): los 49 espacios de evento ($7E:B586). Uno
  estorba si su posición está a menos de 12 píxeles del punto mirado, en
  los dos ejes. Los primeros que encuentra quedan marcados (byte +$0C = 1).
  Los objetos con el bit 1 de su byte +0 no bloquean. Con $7F1F60 bit
  $4000 nada bloquea.
- **El jugador** (`CDDDD`): la misma prueba de 12 píxeles contra la
  posición del jugador; es la que usan los animales y las personas.

### 5.21 Apretar A

Cuando el jugador aprieta A frente a algo, `SUB_82AD0E` mira el tipo de la
baldosa de enfrente (0 si la baldosa no da al lado del jugador) y sus 4
bytes en la tabla de propiedades del mapa:

| Tipo de baldosa | Qué hace A |
|---|---|
| $F0-$FF | una puerta: si el byte +1 tiene el bit 0, va al lugar [+0] ($096E; $D2 bit 6 empieza el cambio de mapa) |
| $E0-$EF | nada |
| $D0-$DF | un letrero: muestra el texto [+0] (una palabra) |
| $C0-$CF | una puerta con horario: muestra el texto [+1] cuando el lugar está cerrado (ver abajo) |
| las demás | levanta la cosa si el byte +1 tiene el bit 7; si no, habla con quien esté enfrente |

**Horarios.** El byte +0 elige una regla en la tabla de entradas
(`Unk_Table13`, $80:B6F5, 8 bytes cada una; el byte 3 es la regla). Cada
bit cierra el lugar en un momento:

| Bit | Cerrado |
|---|---|
| $02 | desde las 17:00 |
| $04 | antes de las 17:00 |
| $08 | de lunes a viernes |
| $10 | sábado |
| $20 | domingo |
| $80 | cuando $0196 tiene el bit 4 |

Por ejemplo las tiendas tienen $32: cerradas el sábado, el domingo y desde
las 17:00.

**Levantar cosas:** el objeto en la mano pasa a ser
`DATA8_82CFB4[tipo × 4 + estación]` (la misma baldosa da algo distinto en
cada estación), el byte +0 de la baldosa va a $091F y la acción del jugador
pasa a 4 (cargándolo). En otoño e invierno las baldosas $38, $39, $52, $53,
$60, $61, $6E y $6F no dan nada.

**Hablar:** si no es nada de eso, la rutina le pregunta a `CBBBB` (5.20) si
hay un objeto de evento 16 píxeles adelante; el objeto encontrado queda con
su byte +$0C = 1, y su propio guion (una persona, un animal) reacciona.

**Acciones** ($096E). Una casilla tipo puerta puede llevar una acción en
vez de un sitio; `PlayerInteractionSelector` la ejecuta desde la tabla
$81:D593. De sus 14 entradas solo cuatro hacen algo: la 2 saca madera del
montón (`GetFenceFromStack`: 2 piezas de madera, y llevas el objeto $57),
la 4 es `CODE_81D670`, y la 6 / 7 arrancan las animaciones de mapa $11 /
$12. Las demás son un RTS suelto (`Unused01`-`Unused11`). Cada una borra
el bit 6 de $D2 al terminar.

### 5.22 Los botones al caminar

En el modo de entrada 1 (5.17), `PlayerMovementInputs` ($84:C6B7) lee el
control cada cuadro. HM-Decomp lo escribe como muchas etiquetas
(`PlayerActionKeysPressed`, `PlayerActionDown`, `PlayerKeyA`...) unidas con
saltos, pero es una sola rutina con muchas salidas.

**Primero, ¿puede actuar el jugador?** No mientras $D2 tenga el bit 2, 6 o
7, no en el aire (acción 3), no con $7F1F5C bit 5, y solo con $D2 bit
$4000. Pescando (acciones $11, $12) solo cuenta la Y. Y muchas acciones
(mostrar un objeto, comer...) no dejan moverse: la tabla
`PlayerActionCanInteract` dice cuáles (1 = ocupado).

**Las flechas** ponen la acción "caminar" ($D4 = 1) y la dirección ($DA:
0 abajo, 1 arriba, 2 derecha, 3 izquierda). Después se mira un botón, en
este orden:

| Botón | Qué hace |
|---|---|
| A | hablar, leer, abrir, levantar (`SUB_82AD0E`, 5.21); con el perro en brazos, soltarlo enfrente (acción $16; se guardan el lugar y el mapa del perro); con algo en brazos, dejarlo en un lado libre (acción $18); con un objeto en la mano, tirarlo (acción 5) |
| B (mantenido) | correr (acción 2); frente a una baldosa con el bit de propiedad $20 o $40, saltarla (acción 3, 28 o 33 píxeles) si hay lugar para caer |
| Select | mostrar el objeto en la mano (acción $0C) |
| X | acción $1C |
| R | silbarle al caballo, si lo tienes ($7F1F68 bit $100; acción $0D) |
| L | silbarle al perro, si lo tienes (bit $80; acción $1B) |
| Y | usar la herramienta (acción $0A, `UNK_ToolUsed`); con la caña: lanzar ($0F → $10), recoger vacía ($11 → $0F), o si picó ($12 → $13) una de cuatro animaciones de pesca al azar |

R, L, Select, X y Y no hacen nada mientras se carga algo ($D2 bits 1, 4,
11).

**Durante los eventos** ($D2 bit $1000) no se lee el control: el guion del
evento "aprieta" los botones en $08FD, con los mismos bits.

**Los bits del control** ($0124 mantenidos, $0128 nuevos): B $8000, Y
$4000, Select $2000, Start $1000, arriba $0800, abajo $0400, izquierda
$0200, derecha $0100, A $0080, X $0040, L $0020, R $0010. (HM-Decomp cambia
izquierda y derecha, ver 5.20.)

**Código muerto:** después del salto de cada flecha hay unas pocas
instrucciones a las que nada llega, y el bloque de $84:C852-$84:C934
(convertiría $0911 en las acciones 6 y 7) nunca se llama. Cuentan como
parte de la rutina.

### 5.23 Colores que se mueven

El agua que brilla, luces que parpadean: el juego cambia colores sueltos
de la paleta, paso a paso (ciclo de paleta, `src/coloranim.c`).

- Hay **16 canales**. Cada uno tiene un puntero a una lista ($42 + 3 n), el
  color que cambia ($015A + n: el color, $016A + n: la fila de la paleta) y
  cuántos cuadros le quedan al paso actual ($014A + n).
- Una lista son pasos de 3 bytes: el color nuevo (una palabra) y cuántos
  cuadros se queda (un byte). $FFFE seguido de un puntero salta (así la
  lista da vueltas); $FFFF la termina.
- `UNK_BigLoop` corre cada cuadro: avanza cada canal y, si alguno anduvo,
  manda la paleta a la consola por DMA. Los canales 0-3 solo corren
  mientras llueve o nieva. Desde el canal 4, el primer canal vacío termina
  el recorrido, así que siempre se usan en orden.
- `CallIndexedSubrutines` arranca las animaciones del mapa según la parte
  del día, con `Subrutines_Table` ($80:BEEC): 6 palabras por mapa (5 partes
  del día, y una palabra que, si no es $FFFF, significa "solo en otoño e
  invierno"). Cada palabra es una de 51 rutinas pequeñas, y cada una
  arranca unos canales (`UNK_SetPointer42`: lista, color, fila, canal).

*Rareza:* después de la marca de fin $FFFF el puntero del canal se borra,
pero el mismo cuadro sigue y todavía escribe el color $FFFF y mueve el
puntero borrado a $00:0003. Como todas las listas terminan en un salto
($FFFE), nunca pasa.

### 5.24 Leer el mando

Cada cuadro `ReadJoypad` ($80:872B, `src/joypad.c`) lee los dos mandos y
deja en la WRAM todo lo que el resto del juego pregunta sobre los botones:

| Dirección | Qué guarda (mando 1) |
|---|---|
| $0124 | los botones apretados ahora |
| $0126 | los que estaban apretados el cuadro anterior |
| $0128 | los que se apretaron en este cuadro (apretados ahora, antes no) |
| $012A | los que se soltaron en este cuadro |
| $012C | "autorrepetición": los recién apretados, y de vez en cuando otra vez todos los que se mantienen |
| $23 | cuántos cuadros lleva apretado algún botón |

La autorrepetición es lo que hace que el cursor de un menú siga moviéndose
si mantienes la cruceta: tras 30 cuadros (medio segundo) apretados, los
botones mantenidos cuentan como apretados otra vez, y el contador vuelve a
25, así que desde ahí se repite cada 5 cuadros. El mando 2 tiene las mismas
palabras ($0130-$0138), pero nada las lee: Harvest Moon es de un jugador.

Antes de leer, espera a que la consola termine de leer los mandos por su
cuenta ($4212 bit 0). Para la comprobación, la reescritura le pide esos
registros al emulador ($4212, $4218, $421A); el fuzzer además aprieta
botones al azar en los dos mandos en cada llamada y después devuelve los de
verdad.

### 5.25 Lo que recibe un mapa al cargarse

La forma de cada mapa está guardada una sola vez en el cartucho. Lo que
cambia durante el juego (las ampliaciones de tu casa, las herramientas que
compraste, el forraje que pusiste...) se añade encima cada vez que se carga
el mapa: `UNK_ExecuteFromPointers` ($81:A9E5, `src/maptiles.c`) llama a la
rutina de ese mapa desde una tabla (una palabra por mapa, $00-$5F). La
mayoría de los mapas no tienen nada que añadir y apuntan a un RTS suelto.

| Mapa | Qué añade |
|---|---|
| Granja ($00-$03) | la primera o la segunda ampliación de la casa, una flor de baya de poder por cada baya comida (10 sitios), la casa pintada (4 partes, $7F1F66 bits 9-12) y tres cosas más ($7F1F64 bits 0-1, $7F1F68 bit 12) |
| Montaña ($10-$13) | una cosa con lluvia o nieve, y otra cuando está el bit 1 de $7F1F64 |
| Casa ($15-$17) | tres cosas: $0196 bit 4, $7F1F6C bit 12 (HM-Decomp lo llama el caparazón de tortuga) y el reloj ($7F1F6E bit 14) |
| Floristería ($1C) | las semillas de los estantes: las de verano en verano, las de primavera en primavera, y dos más en ambas |
| Tienda general ($22) | cada herramienta a la venta que no has comprado y no llevas: la pintura (cuando la casa tiene su segunda ampliación), el ordeñador, el cepillo, las cuatro herramientas de oro (cuando ya se venden) y el aspersor (desde el día 20 del verano del primer año) |
| Tienda de animales ($24) | la poción milagrosa (solo con menos de 12 vacas y ninguna preñada), la medicina y los dos forrajes |
| Cobertizo ($26) | cada herramienta que tienes, en su sitio de la pared (24 sitios) |
| Establo ($27), gallinero ($28) | forraje en cada comedero que llenaste; en el gallinero además un huevo junto a cada gallina que puso uno |
| Cueva de la montaña ($29), túnel de los elfos ($2A), mapa $37 | cosas pequeñas detrás de banderas; la del mapa $37 solo la primera vez |

La misma bandera, el bit 1 de $7F1F64, cambia la granja, la montaña y el
túnel de los elfos: algo que haces y se nota en tres sitios.

Tres piezas hacen el trabajo:

- `SUB_81A7CE` estampa un cambio de casillas (dibujo y tipo de casilla) en
  una posición. Es `ChangeMapTile` sin mandar nada a la VRAM: mientras se
  carga un mapa, el dibujo entero se manda después.
- `SUB_82B0A7` escribe luego el tipo de cada casilla del cambio, desde una
  segunda hoja en $A7:F500 ordenada como los gráficos (para cambios hechos
  de casillas distintas, como una ampliación con su puerta).
- `SUB_81B2BB` muestra un objeto de una lista (el cobertizo, la
  floristería, la tienda general, la de animales; 8 bytes cada uno:
  posición, cambio y un bit de $7F1F00-) siempre, solo si su bit está
  puesto (lo tienes), o solo si no lo está (todavía se vende).

*Sobre HM-Decomp:* cuatro de estas rutinas aparecen allí como datos
(líneas `dw`): la del gallinero ($81:B121), la de la cueva ($81:B1CF), la
del túnel ($81:B21E) y, como etiqueta con sangría, la del mapa $37
($81:B26D). Se desensamblaron desde la ROM. Las tres primeras están
verificadas pero no cuentan en el porcentaje, que mide el código propio de
HM-Decomp. Además, `ReplaceTilesMoutain` empieza en $81:AC80, no en
$81:AC7F (ese byte es el RTS de `ReplaceTilesForkWinter`), y
`MapPointerTable` está en $82:B174.

### 5.26 Quién aparece en cada mapa

Al entrar a un mapa, `RunsFunctionbyIndex` ($83:CA98) llama a su rutina de
`UNK_TableFunctionSelect` ($83:CAA9, una palabra por mapa). Cada una
arranca los guiones de evento de la gente y las cosas de ese lugar, según
la fecha, la hora y las banderas de la historia, con `UNK_LoadCCDataLong`
(slot, grupo de guiones, guion). HM-Decomp las tiene todas como un solo
bloque; acá cada una es `MapEvent_xxxx` (su dirección). Están todas salvo
`DED3` (la casa del jugador, mapas $15-$17), que llama a rutinas de sonido
que esperan al cuadro siguiente; por eso `RunsFunctionbyIndex` también
queda en el original.

| Rutina | Qué arranca |
|---|---|
| `E5A2`, `E5CD`, `E5DF`, `E5F1`, `E6B2` | siempre un guion |
| `E586`, `E5B4` | un guion si hay una bandera ($7F1F68 bit 9, $7F1F6A bit 14) |
| `E603`, `E632`, `E661` | el 24 de invierno (la Noche de las Estrellas), según los bits 0 / 3 / 4 de $7F1F74 |
| `E690` | el 1 de primavera |
| `E6C4` | la familia: la esposa (Ann o Eve, según $7F1F66, salvo con el bit 1 de $7F1F6E) y cada hijo (bits 2 / 3) desde los 60 días |
| `E74C` | nada (la mayoría de los mapas desde el $3B) |
| `D4D7`/`D5E7`, `D74E`/`D818`, `D844`/`D929`, `D955`/`DA1F`, `DB8F`/`DC1E` | las casas de las cinco chicas (dos mapas cada una): la que se casó con el jugador (su bit de $7F1F66) aparece ahí sola (grupo $22) mientras esté el bit 1 de $7F1F6E; si no, el día normal de la familia |
| `D613`, `DA4B`, `DAED` | más gente con su día normal (mapas $1B, $25, $24) |
| `DC4A` | mapas $10-$13 (la montaña): primera visita, escenas de la historia, y algunos días de otoño e invierno desde el año 2 (2-7, 13-17, 25-28, días de semana comunes) una segunda persona (slot 9, grupo $21) |
| `DD95`, `DE64` | mapas $2B y $2A, con sus primeras visitas |
| `DE9B` | mapa $27: con el bit 3 de $7F1F64 una escena que detiene al jugador |
| `E407` | mapas $0C-$0F (el cruce): la primera vez (grupo $0A); desde el primer invierno una visita del grupo $0D, en dos partes |
| `CB69` | la granja (mapas 0-3): la esposa y los hijos en casa; a las 6:00:00 de un día de visita la primera chica soltera con 200 o más corazones viene de visita (una vez cada una); escenas según las banderas de la historia; los primeros días; las visitas de las mañanas de lluvia; eventos de una sola vez (30.000 G desde el año 2, un sábado desde el primer invierno, 6 o más gallinas); una vez de cada nueve una visita que da 2 de felicidad. Error: un evento de otoño pone el bit 7 de $7F1F6A pero mira el bit 8, así que se repite cada día así |
| `D2E0` | el pueblo (mapas 4-7): el 20 de otoño antes de las 3 el festival (los números 0-5 mezclados en $09A4, grupo $28), el 23 de primavera y el 12 de otoño antes de las 3 y el 10 de invierno antes de las 5 sus grupos ($0E, $26, $27), si no el día normal del guion 0 (nadie en los días especiales ni con lluvia); error 24 |
| `E76D` | mapa $3C, la introducción: el jugador se despierta a las 7:00 con la energía llena y las manos vacías; el paso `$7F1F49` (0-$1C) elige el guion del grupo $47 (paso + 1) y sube uno; los pasos 7 y $0D además llenan el galpón. HM-Decomp tiene los primeros $140 bytes de esta rutina como datos; se desensamblaron desde la ROM |
| `EBA6` | mapas $3D y $4B: la presentación del final (ver 5.13) |
| `E4CB` | mapa $29: un guion en una casilla hasta el bit 6 de $7F1F6A; al entrar desde abajo con el bit 11 de $D2 el perro vuelve a la granja (120, 424) |

Qué mapa usa cada rutina: $00-$03 `CB69`, $04-$07 `D2E0`, $08-$0B
`E5DF` `E5F1` `E603` `E6B2`, $0C-$0F `E407`, $10-$13 `DC4A`, $14 `E661`,
$15-$17 `DED3`, $18-$19 `D4D7`, $1A `D5E7`, $1B `D613`, $1C `D74E`, $1D
`D818`, $1E `DB8F`, $1F `DC1E`, $20 `D955`, $21 `DA1F`, $22 `D844`, $23
`D929`, $24 `DAED`, $25 `DA4B`, $26 `E6C4`, $27 `DE9B`, $28 `E5B4`, $29
`E4CB`, $2A `DE64`, $2B `DD95`, $2C `E5A2`, $2D-$30 `E5CD`, $31-$38
`E586`, $39 `E632`, $3A `E690`, $3C `E76D`, $3D y $4B `EBA6`, el resto
`E74C`.

### 5.27 Las cosas que se dejan

Cuando el jugador deja o tira lo que carga, eso se vuelve un **objeto del
mapa** (ver 5.12). `SUB_818000` lo crea delante de él en el estado 0, con
el sprite del objeto según hacia dónde mira (`SUB_8180B7`: `DATA8_8196AF`
tiene, por objeto, una lista de 3 bytes por caso: el sprite y un byte que
x 64 es el volteo), dos píxeles debajo de él. Si el objeto deja algo en el
suelo (`DATA8_81A2AD`, por objeto) ese tipo de casilla, sacado de la tabla
de propiedades del mapa, se dibuja delante; y si hace ruido
(`DATA8_81A308`) suena el efecto 8 del banco 6. Después, cada cuadro,
`SUB_81809A` corre la rutina propia del objeto (tabla `DATA8_8197C0`, 91
objetos) según su estado (`$0976`).

**Las rutinas de los objetos** (`items.c`). Casi todos los objetos solo
corren una de seis máquinas de estados (JSR), que elige un paso según el
estado (JMP):

| Máquina | Objetos | Estados |
|---|---|---|
| `CODE_8188E5` | la mayoría | 0 en la mano, 2 cargado, 3 soltado, 4 se va, 5-6 ofrecido en venta, 7 enviado, 8 puesto sobre un objeto |
| `CODE_818855` | comida (1-8) | como arriba, pero 0 pregunta "¿comerlo?" y 1 lo come (`ChangeStamina` con el valor de la comida, `DATA8_819FC6`) |
| `CODE_8188BC` | forraje ($1A) | 3: a un comedero del establo o del gallinero (casillas $E2-$EE: su bit en $0932 / $0934 y uno más en $0930 / $0931) |
| `CODE_81889A` | $0D-$0F, $57 | 2 se tira solo hacia suelo o un cajón, 3 cae (la casilla pasa a ser la del objeto, primer byte de `Items_Price_Table`) |
| `CODE_81883A` | animales ($25, $26, $29) | 2 se tira solo donde puede pararse (tres casillas libres delante), 3 se baja: el registro de la gallina guarda el mapa y la casilla, y empieza su paseo |
| `CODE_818923` | mercancía de tienda ($1B-$1D, $44-$56) | 0 "¿comprarlo?", 1 paga (`AddMoney` con el precio negativo) o "no te alcanza", 2 vuelve al mostrador, 3 comprado |

Dónde cae un objeto soltado (`CODE_819379`): en la granja, el establo o el
gallinero, en un cajón de envío (casilla $F2) un objeto con precio se
envía (se suma a $7F1F07 antes de las 5 de la tarde; después simplemente
se pierde) y se anima el cajón; un huevo en la incubadora del gallinero
($F7) da un pollito si no hay otro en camino (bit 13 de $7F1F6E, +5 de
felicidad); en la granja, cajones y cajas ($F0, $F9-$FD); en la montaña el
manantial ($F0) y el estanque ($F4). El objeto 7 tirado al manantial
antes de las 5 empieza un evento una vez al día (bit 13 de $7F1F66). Todo
lo demás solo cae en la casilla de enfrente.

Tirado hacia un objeto en el pueblo un domingo antes de las 5 en un puesto
($F0), un objeto que se puede vender se ofrece (`DATA8_81A094`: el texto y
el dinero; el texto $0313 quiere decir "no se puede vender"). Ya comprado,
un artículo de tienda se vuelve otro objeto ($1B-$1D pasan a $58-$5A, $49
a $19, $56 a 6) o va al cobertizo ($7F1F00-$7F1F03): una bolsa de semillas
cuenta una más (hasta 255), una herramienta de oro reemplaza a la normal
en el cobertizo y en las dos ranuras del jugador, el forraje llena su
contador (12). Los cultivos ($10-$13) cuentan uno más enviado en el cuadro
en que se envían. Las 26 herramientas ($2A-$43) siguen en el original:
usan las rutinas que esperan cuadros (`CODE_81A94A`).

**El día normal.** Cada persona tiene un guion en cada uno de varios
grupos, uno por tipo de día: $0B antes del primer evento de la historia
(bit 0 de $7F1F68); $07 con el bit 3 de $0196; $01 cuando el clima de
mañana es 6 o más; $06 con lluvia (clima 3) o el bit 5 de $7F1F64; $08 con
el bit 9 de $0196; $04 con el bit 1 de $0196; algunos tienen $02 / $03
para sábado / domingo; y $01 cualquier otro día. El 10 de invierno antes
de las 5 de la tarde algunos van al grupo $27, y el 24 de invierno al $0F.
La primera visita de una persona (grupo $0B) marca su bit en $7F1F68 para
que se vea una sola vez.


### 5.28 Las acciones del jugador

Apretar A en una casilla tipo puerta cuya propiedad tiene el bit 0 del byte
+1 pone en `$096E` el byte +0 de la casilla y el bit 6 de `$D2`; cada
cuadro `PlayerInteractionSelector` corre la acción `$096E` (tabla en
`$81:D593`, 88 entradas) hasta que la acción borra ese bit. `$096F` cuenta
sus pasos y `$0970` sus cuadros. HM-Decomp mete las acciones $10-$57 dentro
de `CODE_81D716` (la tele) y lee parte de ellas como datos; `actions.c` las
llama `PlayerAction_XX`:

| Acciones | Qué son |
|---|---|
| $0F, $12, $13 | las teles: el pronóstico del tiempo, luego el texto del canal para la estación (`DATA8_81D510`) y su imagen |
| $10, $11 | la cama (dormir: termina el día) y un paseo hasta el cuarto |
| $14 | el diario (el menú de guardar) |
| $15, $1D, $4A-$50 | forraje del silo; los lugares de los trozos de cerca (`$09A4`, `$7F1F78`) |
| $1E | el calendario (la página de la estación) |
| $26, $2D, $33, $36, $3F | los diarios de las chicas: la página según sus corazones, -1 de felicidad |
| $27-$2C, $2F-$32, $34, $37-$3E | estantes: el objeto del tipo de casilla, en la mano |
| $2E | el mostrador del rancho: comprar una vaca (5.000 G) o una gallina (1.000 G), venderlas |
| $40, $54, $55 | letreros con menú (un texto por respuesta) |
| $41 | las aguas termales (+12 de aguante) |
| $43, $44 | el tratante de vacas / gallinas, y la inscripción a las ferias de animales |
| $46 | la visita del que recoge los envíos |
| $47, $48 | el carpintero (la ampliación de la casa: 250 / 500 de madera, 5.000 / 10.000 G) y su final (+50 / +100 de felicidad) |
| $51, $52 | un trago en el bar (200 / 300 G, +1 / +3 de felicidad) |
| $53 | un huevo de un nido del gallinero (`$7F1F45`) |

La acción $35 toca el efecto $24 del banco 7 y espera 120 cuadros (`UNK_Audio24`); después recarga los ruidos del objeto (`ToolUsedSound2`).


### 5.29 El jugador cada cuadro

`SUB_81BFB7` corre una vez por cuadro. Sin el bit 0 de `$D2` solo termina
el cuadro; con el bit 6 corre una acción (5.28); si no, salta según la
acción del jugador (`$D4`, tabla `DATA16_81C027`) a su paso, en
`playerstate.c`. Casi todos ponen la animación del jugador (`$0901`, a
menudo + su dirección) y la reproducen (`CODE_81CFE6`); cuando termina
(`$0915 = $FFFF`) cambia la acción. Todos terminan por
`ResetIdleAnimatioTimer`:

| Acción | Paso |
|---|---|
| 0 | quieto (`CODE_81C556`): la animación de estar parado según el estado (cargando $14, en nieve $B0, $C0, $4F, $D0, $EA); con el bit 14 de `$D2` un contador de ocio (`$0925`) arranca una animación de espera (acción 9) al llegar a $F0 |
| 1, 2, $0E | caminar, correr (solo con el bit 12 de `$D2`, o el 14 sin el 13) y ser empujado: 1, 2 o 3 píxeles (nieve honda) con un sonido de paso cada 16 cuadros |
| 3 | un salto: efecto de sonido 6, 1 o 2 píxeles |
| 4, 5 | levantar (`SUB_818000` deja / toma el objeto) y tirar (efecto 7) |
| 8 | comer; el objeto 8 da además +10 de felicidad y 10 más de aguante máximo |
| 9 | una animación de espera |
| $0A | usar una herramienta: `SUB_829260` al final de la animación, su sonido en el cuadro $18 |
| $0C | el reloj: un texto según la hora ($046A-$0478) |
| $0B, $0D, $0F-$1B | otras animaciones y lo que sigue (eventos, banderas) |
| $1C | cambiar de herramienta: animación $90 + la herramienta de la bolsa; después se cambian de lugar y se cargan los ruidos de la nueva (ToolUsedSound2) |

`CODE_81CB77` mueve al jugador `$1E` píxeles hacia donde mira: los objetos
de evento (`CBBBB`) o una casilla sólida (`CEEEE`) lo paran; cuando solo
una de las esquinas del frente está bloqueada se desliza de lado para
rodearla (siempre en los tipos 0 y $A0-$AF; si no, según en qué mitad de
la casilla está); una puerta ($C0-$CF) lo para enseguida; luego la cámara
lo sigue y cada 8 movimientos deja una huella (`CODE_81A58F`).

### 5.30 Las puertas y la montaña

`SUB_809A64` corre cada cuadro con las dos casillas bajo el jugador
(`$0878` / `$087A`). Si las dos son salidas ($C0-$CF), lo hace salir:

- No con el bit 4 de `$D2` ni los bits 1-2 de `$7F1F60`; con el bit 0 de
  `$7F1F6C`, tampoco por las salidas que llevan a $1C.
- La entrada de la salida: el byte de propiedades de la casilla (`[$0D]` +
  4 x casilla) x 8 en la tabla de puertas `Unk_Table13` ($80:B6F5): +0 el
  mapa al que lleva, +2 una animación de mapa (0 = ninguna), +3 banderas,
  +4 / +6 dónde aparece (X, Y).
- `$098A` cuenta los cuadros. En el primero el destino (`$098B`) puede
  cambiar: + la estación (bandera 0); con la bandera 6, el día 1 de
  primavera da $3A, el día 24 de invierno $39 y los días 10-12 suman 4 (los
  festivales). La puerta está cerrada en el mapa $0B y según sus banderas:
  1 desde las 17:00, 2 antes de las 17:00, 3 de lunes a viernes (días 1-5),
  4 el día 10 de invierno o los sábados, 5 los domingos, 7 con el bit 4 de
  `$0196`. Cerrada, el jugador se queda y se olvidan las casillas bajo él.
  Abierta, el reloj se detiene (`$0973` = 0), el punto de llegada va a
  `$017D` / `$017F` y empieza la animación del mapa.
- El cuadro 1 espera mientras `$00CF`; luego, hasta $0D, sigue caminando
  (acción 1) hacia donde mira. En $0D: al salir de un mapa de la granja
  (menor que 4) se guarda la granja (`CopyCurrentMaptoFarmMap`); al salir
  de los mapas $0C-$0F pasa una hora (no más allá de las 18:00, con
  `HaveLunch`); después el bit 14 de `$0196` arranca la transición.

Un tipo de casilla desde $D0 sigue en `SUB_809D0B`, la montaña (mapas
$10-$13, no los días con los bits 1, 3, 4 de `$0196`):

- Casilla $F9 en primavera, $FA en otoño y $FB en invierno: una
  posibilidad en 17, una vez por día (bits 13-15 de `$7F1F5C`), de un
  hallazgo (evento $11/$2C, $13/$2B, $12/$2A).
- Las dos casillas $F8 (la orilla del agua), con la caña (bit 9 de
  `$7F1F5A`), las manos libres y sin nieve (bits 1, 4, 11 de `$D2`):
  empieza a pescar (acción $0F), salvo que ya esté pescando (acciones
  $10-$13).

### 5.31 El chip de imagen

`src/video.c`, comprobadas sobre la máquina real (3.4).

- **Los DMA preparados.** Una rutina que quiere copiar algo a la memoria
  de video prepara un canal de DMA y deja una nota: `$28` tiene un bit por
  canal en espera, `$2A` + canal el valor de VMAIN, `$32` + 2 x canal el
  destino. `StartProgramedDMA` (en el NMI) los lanza: un canal cuya
  dirección B es $18 va a la VRAM (VMAIN y luego la dirección de 16 bits),
  cualquier otro a la CGRAM (el byte bajo como número de color). Vuelve a
  leer `$28` en cada canal, así que una copia a la WRAM por $2180 puede
  cambiar qué canales siguen.
- **Pantalla en negro y brillo.** `$24` es la copia del juego de INIDISP
  ($2100). `ForceBlank` pone el bit 7, `ResetForceBlank` deja solo el
  brillo, `SetsBrightness` pone el brillo (A & $0F; 0 significa negro) y
  conserva el bit de negro; `InitialzieScreenStatusVar` borra la copia y
  pone la pantalla en negro.
- **Los 11 modos gráficos** (`ManageGraphicPresets`, A = el modo, guardado
  en `$19B6`): unos veinte registros del PPU (tamaño de sprites, modo de
  fondos, direcciones de mapas y caracteres, ventanas, pantallas principal
  y secundaria, mezcla de colores) reciben el valor del modo, cada uno de
  su tabla ($80:8B44-$80:8C4B), y cada valor se guarda también en la WRAM
  ($19B7-$19E1, casi nunca se vuelven a leer). Con la pantalla en negro los
  registros de desplazamiento van a 0; M7SEL = 0 y COLDATA = $E0 siempre.
- **Borrar.** `ZeroesVRAM` (los 64 KB), `ZeroesOAM`, `ZeroesCGRAM` y
  `ZeroesPartialVRAM` (desde la dirección en A, $FFF bytes) ponen la
  pantalla en negro, apagan las interrupciones y copian con un DMA una
  palabra fija de 0 de la ROM ($80:8424). Los tamaños son generosos: el
  doble de la OAM, y la CGRAM un byte menos que el doble. `UNUSED1`, que
  nadie llama, sacaría todos los sprites de la pantalla.
- **Un DMA preparado de inmediato** (`StartLastPreparedDMA`): lo mismo
  solo para el canal de `$27`, lanzado en el acto; `$28` se borra y se
  olvida cualquier otro canal en espera. `LoadsFromVRAMwithOffset` la usa
  para copiar Y bytes de `$7E:2000` + `$7E` a la dirección de VRAM X (pese
  a su nombre escribe en la VRAM).
- **Las piezas de sprite** (`CopiesWRAMtoOBJVGRAM`): la lista que deja el
  cargador en `$7E:A420` (5.12) va a la VRAM, cada pieza con dos DMA de
  $40 bytes: la mitad de arriba, y la de abajo desde origen + $200 a
  dirección + $100.
- **Cada NMI** (`UpdateGraphics`): salvo que el trabajo del cuadro ya esté
  hecho (`$00`), los DMA preparados, la tabla de sprites (canal 4, si lo
  dice `$9A`) y las piezas de sprite; después siempre los mandos, un
  número al azar, el desplazamiento de BG1-BG3 desde `$013C-$0147`, y
  `$00` = 1.
- **El desplazamiento automático** (`AutoMapScrolling`): mientras `$0880`
  cuenta hacia atrás, el mapa se mueve `$087C` / `$087E` en cada cuadro.
- **Cargas fijas.** `LoadDialogueBox`: el mapa de la caja de texto
  ($A5:CB05, descomprimido en `$7E:2000`) a la VRAM $7800 y los gráficos de
  las letras ($9C:BBA3) a la VRAM $5000. `SUB_82E5E7` (pantallas de título
  y de nombre) dibuja un bloque de 3 x 2 casillas desde $82:E666 en la
  dirección de una tabla ($82:E73E); `SUB_82EA80` dibuja cuatro letras $A8
  en la pantalla del nombre.
- **Barras de casillas** (los marcos de las pantallas de título y de
  partidas): `SUB_82EAB4` dibuja una, `SUB_82E742` ocho, una columna cada
  vez. Las cuatro casillas de la columna (casilla, + 1, + $10, + $11) se
  guardan en la página directa ($7E-$84) y se copian desde ahí por DMA
  ($80:007E y $80:0082) a la dirección de VRAM ($86) y a la fila de abajo;
  luego todo avanza 2.
- **La línea de una partida guardada** (`CODE_82E405`, pantalla de
  partidas): el resumen de la ranura (`LoadGameSlotResume`); vacía, un
  bloque en blanco (`SUB_82E5E7`); si no, el nombre del jugador, el año
  (+ 1) y su sufijo, el día y el nombre de la estación, cada letra enviada
  a la VRAM en el acto.
- **Herramientas de vuelta al estante** (`CODE_81A94A`): con algo en la
  bolsa, si la herramienta es de la tienda que se ve (herramientas 1-$18
  mapa $26, la $19 mapa $28, desde la $1A mapa $27), su lugar en el
  estante se vuelve a dibujar (`ChangeMapTile`) y se envía; después se
  marca su bit en las filas del cobertizo (`$7F:1F00`).
- **La tabla de sprites** (`PrepareOAMData`, en cada cuadro): se vacía la
  copia de la OAM en la WRAM y se ordenan los objetos para dibujarlos
  (5.12); después, por cada objeto en uso y cada una de sus piezas
  cargadas (de la última a la primera), la posición en pantalla (objeto +
  el desplazamiento de la pieza, reflejado por el volteo, - la cámara
  `$F5` / `$F7`); una pieza fuera de la pantalla (X fuera de -16..255, Y
  fuera de -16..239) se deja fuera. Los cuatro bytes van a `$7E:A000`: X,
  Y, la primera casilla del hueco (`$86:8000`) y los atributos (paleta,
  volteo, prioridad 2, el volteo del objeto, el modo gráfico `$0195` x 16);
  el bit alto de X a `$7E:A200`. Al final se prepara el canal 4 para
  copiar la tabla a la OAM, y `$9A` le pide al NMI que lo lance.
- **El dibujo de un mapa** (`BackgroundsManager`, al entrar en un mapa y
  para las pantallas de título): el registro del mapa
  (`Maps_Graphics_Table`, $80:AA7C) da, para los mapas por debajo de $57,
  el preset gráfico (`ManageGraphicPresets`) y banderas de `$0196`; luego
  el tamaño de las capas (`$0181`), cuántos tilemaps y cuántos dibujos de
  capa y (mapas por debajo de $57) los bordes de los que no salen la
  cámara ni los sprites (`$ED`-`$F3`). Cada tilemap se descomprime en
  `$7E:2000` y se copia entero a la VRAM; cada dibujo de capa también se
  descomprime ahí (o, con el bit 15 de `$0196`, se copia tal cual: los
  mapas grandes de exterior lo guardan sin comprimir, un banco entero de
  la ROM), recibe los campos dibujados en las
  granjas (`UNK_PartialMap`) y los añadidos del mapa
  (`UNK_ExecuteFromPointers`) y va a la VRAM en filas de $40 bytes. Al
  final el jugador se lleva de 8 en 8 píxeles desde 0 hasta su posición de
  entrada (`$017D`, `$017F`), dibujando y enviando en cada paso la columna
  o fila nueva: la pantalla se construye igual que al desplazarse.

### 5.32 Las interrupciones y esperar un cuadro

- **El NMI** (`NMI_Interrupt`, el vector de `$FFEA`, `$00:867B`): guarda
  A, X, Y, D y el banco de datos, deja el NMI encendido (`$4200` = `$A1`:
  NMI, la IRQ por línea y la lectura automática de los mandos), vuelve a
  dejar pasar interrupciones y corre `UpdateGraphics` (5.31) con la página
  directa y el banco de datos del código interrumpido; después RTI. Los
  vectores de COP y BRK apuntan a su último RTI.
- **La IRQ por línea** (`COP_Interrupt` en HM-Decomp, pero es el vector de
  IRQ, `$80:8699`): solo lee TIMEUP (`$4211`), que suelta la IRQ, y llama a
  una rutina que solo vuelve.
- **Esperar** (`WaitForNMI`): enciende el NMI, borra `$00` y da vueltas
  hasta que el NMI lo pone; A y los flags vuelven como estaban.
  `WaitForNMIATimes` lo hace A veces (A = 0 serían 65.536 cuadros).
- **Fundidos** (`ScreenFadein` / `ScreenFadeout`): un nivel de brillo
  cada `$93` cuadros desde `$92` (o, con `$FF`, desde donde esté, `$25`)
  hasta `$94`; después el bit 15 de `$7F1F5A` dice si la pantalla está
  encendida. Los comandos de guion `Event_ScreenFadein` /
  `Event_ScreenFadeout` funden de 3 a 15 y de 15 a 0, 3 cuadros por nivel.
- **Sonidos que esperan** (5.15): `UNK_Audio18` hace sitio para el
  instrumento de un efecto (quita los de después con `UNK_Audio10` y espera
  un cuadro) y lo manda; `UNK_Audio24` toca el efecto A del banco X y
  espera Y cuadros; `UNK_Audio20` cambia la canción (quita todos los
  instrumentos, lo que primero espera a que el chip no esté ocupado, y
  manda los de la canción); `UNK_Audio26` apaga la canción vieja, pone la
  nueva, espera 180 cuadros y elige la música del mapa; `ToolUsedSound2`
  carga los ruidos del objeto en la mano cuando cambia. El comando de
  guion `Event_AudioRelated3` toca un efecto y espera, y luego recarga los
  ruidos del objeto; la acción 4 (`CODE_81D670`) toca el efecto $19 y
  arranca la animación de mapa $44, y 32 cuadros después termina.
- **Los objetos en el mapa** (5.27): en cada cuadro `BAAAA` recorre los
  diez registros de `$7E:B4E6`: coloca el objeto de cada uno
  (`CODE_8581CB`), lo destruye si se salió del mapa y corre su rutina
  (`SUB_81809A`: la del objeto, tabla `$81:97C0`); las 26 herramientas
  que se toman de la pared del cobertizo se cambian por la de la mano
  (`CODE_81A94A` la devuelve a su estante, `CODE_8187C3` toma la nueva y
  carga sus ruidos).

### 5.33 Del encendido a la granja

- **RESET** (`$80:8428`): modo nativo, la pila en `$1F00`, cada registro de
  la CPU y del chip de imagen a su valor inicial, toda la WRAM borrada, el
  programa de sonido subido y arrancado (`UNK_Audio1`-`4`), la VRAM, la
  OAM y la CGRAM borradas, las partidas guardadas revisadas
  (`CheckSRAMIntegrity`), el NMI encendido, y el título (`IntroScreen`,
  JML).
- **La pantalla de título** (`IntroScreen`, `$82:D75E`): el logo de
  Natsume y luego, un cuadro a la vez (`$82:D80D`: el NMI, los ciclos de
  color, el control), la etapa de `$95`: 0 el logo (2 s, 120 cuadros); 1 la
  escena de los padres, un pequeño juego propio (el guion 0 del grupo 9)
  que corre en sus propios cuadros hasta que el guion acaba; 2 el título
  entra deslizándose; 3 sube; 6 y 9 el fondo de la pantalla de título,
  con fundido; 4 las marcas del menú; 5 el menú (tras 600 cuadros, 10 s,
  las demos); 7 la pantalla de partida nueva; 8 la de partida guardada
  (`SUB_82E1F1`); 10 la primera demo. Si la revisión de las partidas
  falla, el menú empieza en "continuar" (`$098D` = 2).
- **Las pantallas de partidas**: los marcos y las líneas de las dos
  partidas (nombre, año, estación, día), la elegida encendida, el fondo
  moviéndose un píxel cada dos cuadros; de vuelta al título (`$94` = 1) o
  al juego (`$94` = 2): `StartFromNewGame` (`NewGameSetup`) o
  `StartFromLoad` (`LoadGameSlot`), y luego `SpawnAfterLoad`.
- **Las demos** (`SUB_82D1C0`, `DemoPresentation`): una partida nueva en la
  granja jugada por el guion de evento del grupo $47 (el paso de la demo
  en `$7F1F48` elige la siguiente, 12 por turno) en los cuadros del propio
  juego, hasta que el guion acaba.
- **La primera mañana** (`SpawnAfterLoad`, `$80:8000`): la música de la
  casa, la pantalla borrada, el estado del día desde cero
  (`FirstNightReset`), el jugador en su silla (136, 120) de la casa
  (`UNK_ScreenTransition`); después del primer día, el desayuno. Luego el
  bucle del juego.
- **El bucle del juego** (`GameLoop`, `$80:8083`), un cuadro por vuelta:
  esperar el NMI; `$C7` = `$1800`; salvo que haya que escribir un nombre
  (bit 13 de `$0196`: la pantalla de los nombres); `SetScreenTransition`,
  `SUB_809A64`, `UpdateTime`, `PrintTextFrame`,
  `TransitionTimeofDayPalettes`, `UNK_BigLoop`, `InputTypeSelector`,
  `BAAAA`, `SUB_81BFB7`, `AutoMapScrolling`, `IterateCCStructures`,
  `CODE_81A600`, `CODE_8582C7`, `CODE_858CB2`, `PrepareOAMData`; `$00`
  borrado.
- **El reloj** (`UpdateTime`): un segundo por cuadro, 60 segundos por
  minuto y 15 minutos por hora, así que una hora del juego son 900 cuadros
  (15 s); se para a las 18:00 (también la música). Cada hora, el almuerzo
  y el envío; con el bit 10 de `$7F1F5A`, la charla de la caja de envíos.
- **La noche** (`NightReset`, cuando el jugador se acuesta): el
  calendario, los días especiales, las 6:00, el trabajo de la noche (los
  nombres de la fecha, el campo, los daños del clima, las vacas, el clima
  de mañana, la chica favorita, la esposa y los hijos), la energía llena,
  el dinero de lo enviado, los eventos de la historia de esa noche (la
  casa construida la 4.ª noche, las herramientas de oro), el diario y la
  mañana en la casa. Si se tienen L, R y Select apretados al acabar la
  noche, pasa a ser el verano 30 del tercer año, el último día (error 33).
- **Ir a otro mapa** (`SetScreenTransition`, cada cuadro): con el bit 14 de
  `$0196`, la música del mapa nuevo, el fundido de salida, la pantalla en
  blanco; `UNK_ScreenTransition` borra lo del mapa viejo, pone la gente
  del nuevo (`RunsFunctionbyIndex`, 5.26) y lo carga (`SUB_80972C`: el
  dibujo, los colores, el jugador y lo que lleva, los animales, la música,
  el fundido de entrada).
- **La casa** (`MapEvent_DED3`, mapas $15-$17): el final en el verano 30
  del tercer año; la música de la casa tras las ampliaciones; lo que dejó
  la noche (canciones, ruidos, las casillas nuevas de una ampliación); y
  luego la primera escena que encaja: una del grupo $1F, la de una vaca
  (su nombre en el texto), la de la esposa a las 6:00 o su propio grupo,
  la de los hijos, el grupo $0A en sus días, el grupo $28 en la primavera
  1 y el grupo $0F en el invierno 24.
- **La pantalla de los nombres** (`Unk_NamesInput`, `$82:E80C`): la
  cuadrícula de letras, el sprite del cursor, el marco del nombre; un
  cuadro a la vez el cursor parpadea y se mueve, hasta "fin"; entonces el
  nombre va adonde dice `$099F`: `SetPlayerName`, `SetCowNameBought` (una
  vaca nueva), `SetCowNameBorn` (un ternero), `SetDogName`,
  `SetHorseName`, `SetKid1Name`, `SetKid2Name`, y de vuelta al bucle del
  juego.

### 5.34 El juego solo con el C (la versión nativa)

Con todas las rutinas reescritas, el juego corre sin el código recompilado
(`native/`):

- **Quién llama a quién.** RESET y las rutinas que vienen después nunca
  vuelven: cada una termina saltando (JML / JMP) a la siguiente: la
  pantalla de título, un cuadro de ella, las demos, las pantallas de
  partidas, una partida nueva o cargada, el bucle del juego, la pantalla de
  los nombres. Sus reescrituras terminan en ese salto y dejan su destino en
  `g_hm_next`; el `main` nativo llama a la rutina de esa dirección, y así
  para siempre. Todo lo demás son las reescrituras llamándose entre sí.
- **Un cuadro.** Cada espera del cuadro siguiente pasa por `hm_nmi`, que
  primero llama a `g_hm_frame_hook`: el chip de imagen dibuja el cuadro con
  sus registros y su memoria (líneas 1-224, HDMA incluido), la ventana lo
  muestra, se leen los controles para el siguiente y el chip de sonido
  corre la parte de sus ciclos que le toca a un cuadro (1.024.000 por
  segundo); luego corre el manejador del NMI como en la consola.
- **Esperar al chip de sonido.** Un bucle que espera la respuesta del chip
  (`hm_apu_read`) corre el chip unos 4 ciclos por vuelta; cuando la espera
  suma un cuadro entero, el cuadro termina ahí mismo y el NMI interrumpe la
  espera, como en la consola (la subida del programa de sonido al
  encender tarda unos 160 cuadros, casi 3 segundos, como en la real).
- **Los chips.** El chip de imagen (PPU), la unidad de DMA y el chip de
  sonido (su SPC700 y su DSP) son los del runner de la recompilación,
  compilados sin cambios; `native/hw.c` es el mapa de memoria (LoROM: la
  ROM, la WRAM, la RAM con pila) y los registros del CPU (NMI,
  multiplicador y divisor, controles, DMA).
- **En qué difiere de la consola.** El C no tarda nada, así que un cuadro
  cuyo trabajo no cabría en 1/60 s en la consola (cargar un mapa, la
  noche) aquí no pierde cuadros; el NMI llega solo en las esperas. El NMI
  saca un número al azar en cada cuadro (`UpdateGraphics` llama a
  `GetRNG`), así que los números al azar se separan de los de la consola
  tras el primer cuadro lento; todo lo demás sigue igual. Medido contra la
  recompilación en los mismos puntos de la pantalla de título (el comienzo
  de cada etapa), toda la WRAM coincide salvo los números al azar, los
  bytes que deciden y la pila.

---

## 6. Errores y rarezas del juego original

Cosas que el juego hace mal o de forma curiosa, encontradas al descompilar:

1. **Un `BRK` escondido** en la noche de las vacas ($83:BDE1). Si una vaca
   quedó fuera y además hubo una noche de cerca rota, el registro X se
   queda en 8 bits. Entonces una instrucción se lee con un byte de menos y
   el procesador ejecuta un `BRK` (una interrupción de error).
2. **El puntaje del final cuenta mal**:
   - Los corazones y los cultivos enviados se **recortan** en vez de
     tener un tope: 512 nabos enviados valen 0 puntos y 600 corazones
     valen como 88.
   - Del dinero solo cuentan los 16 bits bajos: con 655.360 G tu dinero
     vale 0.
3. **Cuelgue con 10 efectos.** Si los 10 lugares de efectos están ocupados
   y hace falta otro, el juego se queda en un salto a sí mismo ($81:A528).
4. **El dado no es justo** (ver 5.1).
5. **"21th", "22th", "23th"** en la fecha.
6. **Cualquier objeto desconocido "funciona"** como herramienta.
7. **El hacha de oro** se salta la revisión de "hoy no hay hallazgos".
8. **La hoz de oro en invierno** no avanza su paso cuando le toca una
   casilla que sí podría cortar.
9. **El caballo** usa una regla de "¿está en este mapa?" distinta de la de
   los demás animales.
10. **El contador de pasto crecido** solo baja en las primeras 4 filas del
    campo.
11. **`ChangeStamina`** decide el signo con la bandera N de quien la llama.
12. **`ChangeMoney` lee dos veces la cantidad:** se queda con la segunda y
    los 3 primeros bytes del argumento se ignoran. Y si el resultado sería
    negativo, deja el dinero en 0 en vez de rechazar la operación.
13. **Comandos que no se usan:** la tabla de comandos apunta tres veces a
    `ResetFlag`, así que `ResetFlagD` y `ResetFlagDD` no se alcanzan nunca;
    `Event_UNKTools` (guarda las herramientas en la bodega) tampoco está en
    la tabla.
14. **`RemoveProgrammedDMA` usa todo A como índice.** En modo de 8 bits,
    `TAX` también copia el byte alto escondido de A, así que la tabla de
    canales se lee en (byte alto : canal). Funciona solo porque todos los
    que la llaman tienen 0 ahí.
15. **El ancho de las letras se ignora.** La impresora le pasa "letra
    ancha: sí / no" a `DrawLetter` y a `AdvanceLetterCursor`, pero las dos
    empiezan poniéndolo en 0. Todas las letras miden 8 píxeles de ancho.
    (Seguramente es un resto de la versión japonesa, con caracteres de 16.)
16. **Después de un número o un nombre la impresora salta dos códigos en
    8 bits** (solo el byte bajo de la posición), así que en un texto de más
    de 256 códigos saltaría hacia atrás (ninguno visto hasta ahora es tan
    largo).
17. **La vaca que pasa de la granja al establo queda en una posición
    equivocada.** La tabla de lugares del establo tiene x e y para cada
    vaca, pero este código lee dos veces la misma palabra (se olvida de
    pasar a la siguiente), así que la posición que se le escribe es (x, x).
18. **La bandera "se reinició una partida"** ($7F1F60 bit $0800) solo se
    pone cuando fallan *las dos* ranuras, no cuando falla una (el código
    mira el resultado de la ranura 1 solo en el camino de fallo de la 2).
19. **El tope de velocidad de la cámara mira el lado equivocado.** Al
    bajar (o ir a la derecha), la velocidad se recorta con la posición de
    la cámara, es decir, la distancia desde *arriba*, no lo que falta hasta
    abajo; al subir (o ir a la izquierda) se recorta con la distancia al
    límite de *abajo*. Como la cámara se mueve uno o dos píxeles por
    cuadro, solo se nota en el primer píxel o dos de un mapa.
20. **Una división con signo que nadie usa.** `DivisionSigned` ($83:80D0)
    divide con signos (el cociente negativo cuando uno de los dos lo es; el
    residuo siempre positivo), pero nada en el juego la llama. Se reescribió
    igual.
21. **El mando 2, leído con el mando 1.** `ReadJoypad` calcula los botones
    "soltados" del mando 2 ($0136) con el registro del mando 1 ($4218) en vez
    del suyo: una errata. No importa, el mando 2 nunca se usa. La misma
    rutina tiene además una rama que nunca corre (cuando $00 no es 0) que
    leería solo los bytes bajos de los mandos.

22. **Una suma que se tira.** Los bucles de los estantes de la floristería
    hacen `TYA; CLC; ADC #5` y luego otra vez `TYA`, así que la suma se
    pierde y el objeto es solo Y + 1. Quizá al principio los estantes
    empezaban en el objeto 5.
23. **Un bucle que nunca termina.** Cuando una pieza devuelve su hueco
    de gráficos, el juego espera mientras el contador del hueco sea 0 antes
    de bajarlo (`LDA; BEQ` a sí mismo, `CODE_858C9F`). Con 0 se colgaría
    para siempre; no pasa porque un hueco que se devuelve siempre está en
    uso.
24. **Una bandera que nunca se borra.** Al entrar al pueblo con el bit 10
    de `$7F1F60`, `MapEvent_D2E0` arranca un guion y quiere borrar el bit:
    `LDA.L $7F1F60; AND #$FBFF; STA.B $60`. La escritura va a la página
    directa ($60) en vez de a $7F1F60, así que el bit sigue puesto y el
    guion arranca cada vez que se entra al pueblo (hasta que otra cosa lo
    borre).
25. **El bit equivocado.** En la granja, un evento de otoño (grupo $18,
    en los días de visita) mira el bit 8 de `$7F1F6A` para saber si ya se
    vio, pero marca el bit 7. Así que vuelve cada día así hasta que otra
    cosa ponga el bit 8.
26. **Un texto leído con el ancho equivocado.** En la visita del que
    recoge los envíos (acción $46), cuando la parte baja del dinero de
    envíos del día no es 0 el salto al texto deja A en 16 bits, así que
    `LDA #$00; STA $0191` se lee como `LDA #$8D00; STA ($01),Y`: se
    escribe $8D00 donde apunte `($01),Y` y `$0191` conserva su valor.
27. **Una búsqueda sin fin.** Al tomar un huevo del gallinero (acción $53)
    se busca el nido de la columna de la casilla de enfrente entre 13; si
    no hay ninguno salta a sí mismo para siempre (`$81:F856`).
28. **Tres lugares donde gira para siempre.** El cargador de piezas
    (`CODE_858CB2`) salta a sí mismo cuando no hay ningún hueco de gráficos
    libre, cuando un número de hueco es $2000 o más, y en la pieza 32 de un
    cuadro (su lista de copias tiene sitio para 31). Como en el 23, son
    callejones sin salida de los que el propio juego se mantiene lejos: el
    presupuesto deja pasar menos piezas por cuadro.
29. **El HDMA apagado en cada cuadro.** En el NMI, después de las piezas de
    sprite (`CopiesWRAMtoOBJVGRAM` vuelve con A de 16 bits), `UpdateGraphics`
    borra `$9A` y `$420B` con escrituras de 16 bits: borran también `$9B` y
    `$420C`, HDMAEN, así que cada cuadro con trabajo termina con el HDMA
    apagado.
30. **Un JSL a una rutina que acaba en RTS.** La pantalla de los nombres
    (`Unk_NamesInput`, `$82:E8E7`) llama a `SUB_82EAB4` con JSL, pero esta
    vuelve con RTS: el byte de banco que empujó el JSL se queda en la
    pila. La pantalla de los nombres nunca vuelve, así que no hace daño.
31. **Un bucle de una instrucción.** `SUB_82878E` es un BRA a sí mismo.
    Nadie la llama.
32. **Código al que no se llega en la pantalla de título.** Tras el bucle
    de la escena de los padres (`$82:D99A`-`$82:D9B2`), una cuenta de
    `$168` cuadros y un fundido de salida a los que ningún salto llega.
33. **El último día a mano.** Tener L, R y Select apretados (`$0124` =
    `$2030`) cuando acaba la noche pone la fecha en el verano 30 del tercer
    año: la mañana siguiente es el final.
34. **Un nombre sin destino.** Si `$099F` no es 0-6 cuando se termina un
    nombre, la pantalla de los nombres da vueltas para siempre buscando su
    rutina.

## 7. Diferencias encontradas en la recompilación

- **Más sprites en pantalla (a propósito).** Para la pantalla ancha, la
  recompilación cambia la prueba de "está en pantalla" de
  `PrepareOAMData`: en vez de X en -16..255 consulta
  `hm_ws_oam_left_limit()` / `hm_ws_oam_right_limit()` (las sustituciones
  "oam left / oam right" del build). La reescritura conserva los 256
  píxeles del original, así que se comprueba contra el intérprete
  (HM_INTERP), que corre el código original. El propio intérprete además
  borra el byte alto de X e Y cuando los índices pasan a 8 bits y vuelven,
  como la consola, mientras que el juego compilado lo conserva (punto
  siguiente): las preparaciones de las rutinas comprobadas así ponen ese
  byte a 0.

- **`SEP #$10` no borra el byte alto de X e Y.** En el SNES real, pasar
  los índices a 8 bits borra su byte alto, y en la recompilación queda
  guardado. No parece cambiar nada visible del juego, pero el comprobador
  lo nota. Las reescrituras imitan a la recompilación, que es la
  referencia.
- **Una lectura de 16 bits en el último byte del banco $7E.** En el SNES,
  leer dos bytes en `$7E:FFFF` toma el segundo de `$7F:0000`; en la
  recompilación sale de `$7E:0000`. El juego nunca lo hace (solo apareció
  en las pruebas al azar de las rutinas de desplazamiento, que ahora lo
  evitan).
- **El intérprete se rinde sin avisar.** Las rutinas sin cuerpo compilado
  corren en el intérprete del recompilador, que detiene una rutina tras 2
  millones de pasos (para salir de bucles infinitos) y aun así dice que
  terminó. Las subidas de sonido tardan muchísimo más mientras el chip
  contesta, así que ahora el comprobador solo da una ejecución por
  terminada si la rutina volvió de verdad, y el fuzz sube ese límite.
  También se agrandó 64 veces el registro del tráfico con el chip de
  sonido (4 millones de lecturas y escrituras): cada espera lee el puerto
  miles de veces.

## 8. Glosario

| Palabra | Significado |
|---|---|
| ROM | el contenido del cartucho |
| WRAM | la memoria de trabajo de la consola |
| SRAM | la memoria del cartucho donde se guarda la partida |
| registro | casilla interna del procesador (A, X, Y...) |
| bandera | bit que dejan las operaciones (C, Z, N) |
| banco | bloque de 64 KB de memoria; `$7F:1F19` = banco $7F |
| pila | zona de memoria donde se guardan las direcciones de vuelta |
| JSL / JSR | llamar a una rutina (larga / corta) |
| página directa | 256 bytes de variables rápidas (`$72`, `$CC`...) |
| puntero largo | dirección de 3 bytes guardada en memoria |
| cuadro | cada imagen; el juego avanza 60 por segundo |
| NMI | aviso del hardware al terminar cada cuadro |
| DMA | copia de memoria hecha por el hardware |
| sprite | figura que se mueve sobre el fondo |
| slot (CC) | estructura donde corre un guion de evento |
| fuzz | probar una rutina con entradas al azar |
| intérprete | la parte de la recompilación que ejecuta instrucción por instrucción lo que no está compilado |
| bus abierto | leer una dirección donde nada responde: se obtiene el último valor que pasó por el bus |
| BG3 | la tercera capa de fondo; el juego la usa para el texto |
| puerto | un byte que comparten dos procesadores (la CPU y el chip de sonido) |
| apretón de manos | el intercambio "listo / ahí va / hecho" por los puertos |
| paleta | los 256 colores que la pantalla puede usar a la vez |
| CGRAM | la memoria de la consola donde se guarda la paleta |
| LZSS | una forma de comprimir: "copia este byte" o "copia tantos bytes de lo que ya escribiste" |

## 9. Avance

| Fecha | Avance | Qué se agregó |
|---|---|---|
| 19-oct-2026 | — | clima, daños a la granja |
| 20-oct-2026 | 7,94 % (medición vieja) | crecimiento, animales, la noche completa |
| 21-oct-2026 | 7,29 % | medición corregida |
| 21-oct-2026 | 7,69 % | DMA, cambiar casillas |
| 21-oct-2026 | 8,26 % | objetos con sprite |
| 21-oct-2026 | 12,10 % | las herramientas |
| 21-oct-2026 | 14,41 % | animales en el mapa, el final |
| 21-oct-2026 | 17,35 % | comandos de evento, perro, gallina |
| 21-oct-2026 | 20,45 % | 31 comandos de evento más (valores, esperas, levantar cosas) |
| 21-oct-2026 | 20,33 % | medición corregida (se separan rutinas escritas juntas) |
| 21-oct-2026 | 20,85 % | el cuadro de texto: abrir, cerrar, la flecha; dos ayudantes de DMA |
| 21-oct-2026 | 21,31 % | las letras: fuente, dibujo, cursor, números |
| 21-oct-2026 | 21,59 % | el comprobador graba el chip de sonido; 7 rutinas de sonido |
| 21-oct-2026 | 21,86 % | la regadera y el aspersor: las 28 herramientas listas |
| 21-oct-2026 | 21,91 % | la rutina que elige la rutina de cada herramienta |
| 22-oct-2026 | 22,49 % | la impresora de texto (comprobada en vivo: 2.424 cuadros de texto en una partida nueva) |
| 22-oct-2026 | 25,96 % | cada cuadro de la vaca (1.567 instrucciones) |
| 22-oct-2026 | 27,28 % | las partidas guardadas (el comprobador ya compara la SRAM) |
| 22-oct-2026 | 28,16 % | los botones en los menús |
| 22-oct-2026 | 28,72 % | los botones con un cuadro de texto abierto |
| 22-oct-2026 | 30,61 % | desplazar el mapa (probado en vivo: unas 1.200 llamadas en una partida nueva) |
| 22-oct-2026 | 32,17 % | los colores del día (probado en vivo: unas 13.000 llamadas) |
| 22-oct-2026 | 33,34 % | chocar con las cosas (probado en vivo: unas 2.900 llamadas) |
| 22-oct-2026 | 33,80 % | las animaciones de las herramientas (29 rutinas) |
| 22-oct-2026 | 34,31 % | apretar A; un guion nuevo (`scripts/actions.txt`) prueba las herramientas en vivo |
| 22-oct-2026 | 36,10 % | los botones al caminar (las 15 etiquetas de ese bloque ahora cuentan como una rutina: 519 rutinas en total) |
| 22-oct-2026 | 36,92 % | `InputTypeSelector`, la pantalla del nombre, el menú 1, el modo 6: todos los modos de entrada reescritos; probados en vivo cada cuadro (unas 21.000 llamadas) |
| 22-oct-2026 | 37,26 % | dibujar el mapa entero, con las cercas unidas a sus vecinas |
| 22-oct-2026 | 38,35 % | los comandos de evento 50-59 (los 9 sin etiqueta en HM-Decomp ahora cuentan por su dirección) |
| 22-oct-2026 | 39,25 % | el comando de evento 37: el objeto del grupo $17 que se carga (probablemente el hijo) |
| 22-oct-2026 | 39,37 % | la división con signo (sin uso) |
| 22-oct-2026 | 39,59 % | el descompresor (LZSS); el comprobador ahora también conoce todas las banderas de la entrada, para `PHP` |
| 22-oct-2026 | 40,09 % | poner la cámara al entrar a un mapa, y el segundo fondo (nubes, paralaje) |
| 22-oct-2026 | 40,54 % | cinco comandos de evento más (textos, apagar un bit, una baldosa, el cobertizo); ya está más de la mitad de las rutinas |
| 22-oct-2026 | 47,73 % | las animaciones de colores: 57 rutinas (51 de ellas, pequeñas, de una tabla) |
| 22-oct-2026 | 47,87 % | leer el mando (los botones apretados, soltados, y la autorrepetición de los menús) |
| 22-oct-2026 | 47,97 % | dos rutinas sin usar que cuentan las gallinas y las vacas |
| 22-oct-2026 | 48,04 % | el ruido de cada herramienta |
| 22-oct-2026 | 48,18 % | la copia del campo de la granja, liberar todos los slots de guion, la multiplicación |
| 22-oct-2026 | 50,23 % | lo que recibe cada mapa al cargarse (39 rutinas); ya pasa de la mitad del código |
| 22-oct-2026 | 52,99 % | la música y las subidas al chip de sonido (13 rutinas), el envío de las 5, las acciones del jugador, de puntos a corazones |
| 23-oct-2026 | 53,48 % | destruir un objeto con sprite, y los comandos de guion que lo hacen |
| 23-oct-2026 | 54,03 % | cuatro comandos de guion que eligen animaciones y hacen que una persona te mire |
| 23-oct-2026 | 54,42 % | mover un objeto con sprite, cambiarle el sprite, el paso de un slot que camina |
| 23-oct-2026 | 54,60 % | sprites volteados, vaciar las tablas de sprites, releer un objeto |
| 23-oct-2026 | 54,80 % | huellas en la nieve, el reproductor de animaciones de mapa |
| 23-oct-2026 | 55,98 % | los slots que pasean solos (la gente del pueblo, los animales) |
| 23-oct-2026 | 56,14 % | el sprite de cada slot, cada cuadro |
| 23-oct-2026 | 56,76 % | hablar con la gente (comandos 1E, 1F, 2D), animación de mapa y música desde un guion; corrección de la cuenta (−0,04) |
| 23-oct-2026 | 57,86 % | animar los objetos con sprite, el orden de dibujo, vaciar la tabla de sprites |
| 23-oct-2026 | 59,10 % | cargar una partida guardada y empezar una nueva |
| 23-oct-2026 | 59,36 % | cargar las casillas de un mapa, la música de cada mapa |
| 23-oct-2026 | 59,66 % | el objeto con sprite del jugador, el cursor de la pantalla del nombre |
| 23-oct-2026 | 59,85 % | el comienzo del final: la vaca preferida y el porcentaje de desarrollo del rancho |
| 23-oct-2026 | 60,23 % | quién aparece en cada mapa: las 13 primeras rutinas |
| 23-oct-2026 | 62,32 % | quién aparece en cada mapa: las casas (16 rutinas más) |
| 23-oct-2026 | 62,68 % | quién aparece en cada mapa: el cruce, los mapas $27 y $29 |
| 23-oct-2026 | 63,12 % | quién aparece en el pueblo |
| 23-oct-2026 | 63,74 % | la introducción (mapa $3C) |
| 23-oct-2026 | 65,20 % | la presentación del final |
| 23-oct-2026 | 66,73 % | quién aparece en la granja |
| 23-oct-2026 | 66,97 % | dejar en el suelo lo que se carga |
| 27-oct-2026 | 71,73 % | lo que hace cada objeto ya dejado: comer, tirar, enviar, comprar |
| 27-oct-2026 | 79,88 % | las acciones del jugador: teles, cama, tiendas, tratantes, carpintero, bar |
| 28-oct-2026 | 83,43 % | el jugador cada cuadro: caminar, correr, saltar, las animaciones, rodear esquinas |
| 28-oct-2026 | 85,06 % | las puertas y la montaña, el cargador de piezas de sprite, la subida del programa de sonido al encender |
| 28-oct-2026 | 85,91 % | el chip de imagen: DMA preparados, negro y brillo, modos gráficos, borrar la memoria de video |
| 28-oct-2026 | 86,59 % | el chip de imagen en el NMI: las piezas de sprite, un DMA de inmediato, la actualización del cuadro; el desplazamiento automático; los gráficos de la caja de texto |
| 28-oct-2026 | 88,12 % | las pantallas de título y de partidas (barras de casillas, la línea de una partida), herramientas de vuelta al estante, la tabla de sprites |
| 28-oct-2026 | 88,80 % | el dibujo de un mapa (BackgroundsManager: tilemaps, dibujos de capa y el paseo de la cámara); dos preparaciones del fuzz más realistas (el año de una partida, los pasos y la dirección de un slot que pasea) |
| 28-oct-2026 | 88,94 % | las interrupciones (NMI, IRQ por línea) y las esperas al cuadro siguiente, con el NMI adelantado por la prueba al azar (HM_WAIT) |
| 28-oct-2026 | 90,73 % | lo que espera cuadros: los fundidos, los sonidos que esperan (UNK_Audio18/20/24/26, ToolUsedSound2), dos comandos de guion, la acción 4, los objetos en el mapa (BAAAA, SUB_81809A) y las 26 herramientas del cobertizo, el intérprete de guiones y su bucle, el cambio de herramienta (estado $1C del jugador), el cuadro del jugador (SUB_81BFB7, PlayerInteractionSelector) y la acción $35; la prueba al azar ahora repone la máquina entera antes de cada llamada de una rutina de sonido |
| 29-oct-2026 | 100,00 % | todo lo demás: del encendido a la granja (RESET, la pantalla de título y su menú, las pantallas de partidas, las demos, la primera mañana, el bucle del juego), el reloj y la noche, ir a otro mapa y cargarlo, la casa (quién aparece ahí), la pantalla de los nombres y adónde va cada nombre; la prueba al azar para las rutinas que nunca vuelven donde entran a su bucle (HM_TOLOOP) |
| 29-oct-2026 | 100,00 % | la versión nativa: el juego se juega solo con el C reescrito (`native/`), con los chips de imagen y de sonido de la consola alrededor |
