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
- **El hardware de video**: sus registros no son memoria. Lo que el juego
  escribe ahí no se compara, salvo los registros de DMA.
- **El chip de sonido, resuelto grabando.** Sus respuestas vienen de otro
  procesador y no se pueden calcular. Entonces, mientras corre el
  original, el comprobador anota cada lectura y escritura en los puertos
  de sonido, en orden. Después, cuando corre la reescritura, cada lectura
  de un puerto recibe el valor que recibió el original en ese mismo punto,
  y cada escritura tiene que ser exactamente la que hizo el original
  (mismo puerto, mismo valor, mismo orden). Una diferencia y la prueba
  falla. En la prueba al azar, estas rutinas se prueban en un momento más
  tranquilo del juego (900 cuadros antes), porque a veces el chip está
  ocupado cargando música mucho tiempo y nunca dice "listo". Y como el
  reloj del chip no puede volver atrás, en estas rutinas el reloj de la CPU
  tampoco vuelve: cada llamada empieza donde terminó la anterior, como si
  el juego llamara a la rutina una y otra vez.
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
| `src/audio.c` | hablar con el chip de sonido |
| `src/save.c` | las partidas guardadas |
| `src/menus.c` | botones en los menús |
| `src/scroll.c` | desplazar el mapa |
| `src/palette.c` | los colores del día |
| `src/collision.c` | chocar con el mapa, los objetos y el jugador |
| `src/toolanim.c` | la animación de la herramienta al apretar Y |
| `src/interact.c` | apretar A frente a algo |
| `src/playerinput.c` | los botones al caminar |
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
- **Dibujar el mapa entero** (`UNK_PartialMap`): para cada una de las 64 ×
  64 baldosas toma el dibujo del byte +0 de la propiedad de su tipo (0:
  nada). Los tipos $73-$79 se unen con sus vecinos de la misma clase
  (cercas y parecidos, "autotiling"): los cuatro vecinos forman una máscara
  (izquierda 8, derecha 2, arriba 4, abajo 1) y la tabla `$82:B02A` dice
  cuál de los dibujos sumar. Es larga: no termina en un cuadro.

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

## 7. Diferencias encontradas en la recompilación

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
