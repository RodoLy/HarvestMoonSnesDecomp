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
**espera** a que el chip responda. Esas esperas son las que todavía no
podemos verificar (ver 3.4).

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

- **El hardware** (video, sonido): sus registros no son memoria y sus
  respuestas no se pueden repetir. Por eso la regadera y el aspersor, que
  esperan al chip de sonido, quedan para después.
- **Lo que corre en el intérprete.** Algunas rutinas no tienen versión
  compilada: la recompilación las corre en un intérprete. El comprobador
  en vivo no ve esas llamadas, y entonces la prueba al azar es la
  verificación (llama al original por el intérprete).
- **Lecturas fuera del cartucho.** Si una prueba al azar hace que una
  rutina lea una tabla tan lejos que se sale de la ROM, cae en direcciones
  donde no hay nada (el "bus abierto": se lee lo último que pasó por los
  cables). En el juego eso nunca pasa, así que las preparaciones dejan las
  entradas dentro de la ROM.

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
| `src/ending.c` | el puntaje del final |
| `src/hm_decomp.c` | el comprobador y las preparaciones |

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

### 5.5 Las herramientas

**¿Funciona aquí?** Cada mapa tiene una tabla de propiedades: 4 bytes por
tipo de casilla, que dicen qué herramientas funcionan y desde qué lados y en
qué estaciones. Bits: hoz 01, azada 02, martillo 04, hacha 08, agua 10,
semillas 20. `CheckToolSuccess` responde: 1 funciona, 0 no, 2 se llena la
regadera.

**Rareza:** cualquier objeto que no esté en la lista "funciona".

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

### 5.8 Las personas

- **Corazones** de las cinco chicas: `$7F:1F1F + 2·n` (0-999), en el orden
  María, Ann, Nina, Ellen, Eve. Hablarles suma +8.
- **La favorita** (`FindMostLovedName`): en caso de empate gana la
  última.
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
n.

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
- **El dibujo del mapa** está completo en la WRAM (`$7E:2000`); la memoria
  de video solo tiene una ventana de 64 × 64 que se va rellenando desde
  ahí.
- **Cambiar una casilla** (`ChangeMapTile`) hace tres cosas: escribe el
  dibujo nuevo en ese mapa en memoria, deja en la cola de DMA la copia a
  la memoria de video y, si hace falta, cambia el tipo de casilla. Lo que
  hay que poner sale de la tabla `$81:B363`.

### 5.12 Objetos con sprite

- **Objetos del juego** (`$019C`, 40 de 36 bytes): todo lo que tiene
  sprite en pantalla. `CreateGameObject` toma uno libre.
- **Sprites:** cada uno se arma con piezas de 16 × 16 (hasta 12). Los
  números 0-$261 se describen en el banco $86 y $262-$44F en el $87.
- **Gráficos de las piezas:** si el dibujo de una pieza ya está en video
  se reutiliza; si no, va a una cola que vacía el NMI.
- **Objetos del mapa** (`$7E:B4E6`, 10): los efectos que dejan las
  herramientas, como la hierba cortada o el golpe en la piedra.

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

El texto en sí, letra por letra, lo imprime la rutina que sigue
(`ADDDDFFFF` en HM-Decomp, 261 instrucciones), que falta descompilar.

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

## 7. Diferencias encontradas en la recompilación

- **`SEP #$10` no borra el byte alto de X e Y.** En el SNES real, pasar
  los índices a 8 bits borra su byte alto, y en la recompilación queda
  guardado. No parece cambiar nada visible del juego, pero el comprobador
  lo nota. Las reescrituras imitan a la recompilación, que es la
  referencia.

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
