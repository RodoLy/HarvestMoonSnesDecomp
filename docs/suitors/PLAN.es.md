# Pretendientes hombres: investigación y plan

*(English: [PLAN.md](PLAN.md))*

Una mejora planeada para la versión nativa, después de la Diosa de la
Cosecha (`native/goddess.c`): hombres solteros del pueblo a los que el
jugador puede cortejar y con los que se puede casar, igual que con las
chicas.

**Hecho hasta ahora:** el paso 1 (corazones y regalos) para el fan de Eve,
en `native/suitors.c`, con su propia opción (`MaleSuitors` en `config.ini`,
"Pretendientes hombres" en el lanzador). Compila, pero **todavía no se ha
probado en el juego**: la primera prueba es ir al bar de noche, hablarle y
regalarle algo (ver "Para probarlo" al final).

## Lo que se encontró

Fuentes: los 1280 textos del juego (`tools/dump_texts.py rom.sfc salida.txt`
los saca; el texto $nnn es el índice de Text_Pointer_Table, $83:9BF6) y
las personas que pone cada mapa (los slots de eventos, $7E:B586 + 64 n,
leídos de volcados de RAM en cada mapa a las 11:00 y a las 19:00).

`townsfolk_sprites.png` muestra los sprites $250-$2E1. **Sus colores están
mal** (dibujados con la paleta de la Diosa); las formas y poses son las reales.

### Candidatos (solteros, con poses completas: de frente, de espalda, de lado)

| Quién | Sprites | Dónde | Por qué soltero |
|---|---|---|---|
| El fan de Eve: **el Visitor** (también llamado Reporter o "Lonely Bachelor") | $269-$26F (saco claro, corbata roja): todas las poses | el bar de noche, mapa $1E | le dicen "Lonely Bachelor"; texto $120, de noche en el bar: "Good evening. I'm a fan of Eve's, you know." |
| Los carpinteros (hermanos) | $25A-$264 (también cargando madera y con el martillo) | la montaña, mapa $10; su casa, mapa $2B | textos $128/$129: "We are carpenters"; viven juntos y no mencionan esposa (el juego no lo dice) |
| El papá de Ann | $296-$29C (pelo café, pechera azul): de frente, de espalda, de lado, caminando y hablando, todas | la tienda de herramientas | viudo (textos $1DC, $246) |
| Los repartidores (dos, solteros los dos) | uno es $270-$274 (rubio, camisa blanca, chaleco oscuro: todas las poses; de noche en el bar da consejos: "You can't use the tool when you lose your stamina..."); el otro (rubio, camisa blanca, gorra; de noche en el bar: "Even if you put shipping goods in the box at night...") no está entre $250 y $2E1: falta encontrar su sprite y ver sus poses. Uno de ellos es el aprendiz (texto $117/$118: "I'm a trainee of shipping too... With grit!") | la granja (recogen lo enviado); de noche, el bar | solteros |

**Quién es quién** se comparó con los nombres de las hojas de The Spriters
Resource (Harvest Moon, SNES: "Reporter", "Shipping Workers", "Tool Shop
Owner", "Mayor", "Parents", "Bartender", "Fisherman"), solo mirándolas: no se
copió nada de ellas. Así se corrigió lo de antes: $28F-$295 (bigote) es el
**alcalde**, no el repartidor; $2D9-$2DE y $2DF-$2E1 son **los papás del
protagonista**, no el papá de Ann; y $269-$274 no es una sola persona: $269-$26F es el Visitor (el fan de Eve) y $270-$274 un repartidor.

`native/suitors.c` ya usa solo $269-$26F para el fan de Eve.

Primer paso acordado: **el fan de Eve y uno de los carpinteros**; luego el
papá de Ann y los dos repartidores (el segundo cuando se encuentre su sprite).

### Descartados

- El cantinero ($285-$287): solo está dibujado de frente, detrás de la barra (limpiando un vaso); no tiene poses de espalda, de lado ni caminando, así que no podría caminar, casarse ni vivir en la granja sin dibujos nuevos.
- El papá de Ellen ($2A4, el borracho: "hic").
- El alcalde ($28F-$295, el del bigote; papá de María): casado.
- El pescador ($265-$268): solo está dibujado sentado pescando, de frente (lo que trae es una caña, no un bastón); no tiene poses de espalda, de lado ni caminando.
- El cazador ($27D): abuelo de Eve.
- El padre de la iglesia ($2A5); los vendedores ambulantes (no viven en el pueblo).

## Cómo se construiría (las mismas piezas que la Diosa)

1. Corazones y regalos: sus cosas favoritas, sus frases según los
   corazones, en la caja de texto del propio juego (`native/textbox.c`).
2. Sus eventos especiales, como los de las chicas: escenas propias que
   pasan al llegar a ciertos corazones (por ejemplo, ir a buscarte a la
   granja o encontrarse en la montaña), con sus frases y sus poses.
3. Bailar en los festivales: en los festivales donde se invita a una chica a
   bailar, poder invitarlo a él, con lo que eso suma de corazones.
4. Propuesta: la Blue Feather (`g_hm_feather_used`), con las condiciones
   del juego (corazones, la casa ampliada).
5. Boda: la escena del juego (grupo de eventos $1E, mapa $2D) con él
   vestido en el slot de la novia mediante `g_hm_slot_picture`, como la Diosa.
6. Vida de casados: el día de un esposo (en casa en la mañana, afuera desde
   las 9:00, en casa a las 18:00), caminando con sus propias poses (no flota).
7. Hijos: el juego solo tiene un parto; para dos hombres, una escena de
   **adopción** propia (por ejemplo, los duendes de la cosecha traen un
   bebé); después los hijos crecen con el sistema del juego
   (`hm_WifePregnancyAndChildren`).
8. El final, con él en lugar de la chica: prestar el bit de esposa el último día, como con la Diosa
   (`lend_for_ending`), y vestir las figuras de la esposa como él.

Apagado de fábrica, con su propia opción en `config.ini`, como `HarvestGoddess`.

## Para seguir en otra sesión

Abre la rama `male-suitors` (que sale de `harvest-goddess`) de
RodoLy/HarvestMoonSnesDecomp y pide que primero lea `docs/suitors/PLAN.es.md`.

## Para probarlo

1. Activa "Pretendientes hombres" en las mejoras del lanzador.
2. Ve al bar de noche. Presiona A frente al fan de Eve: su frase sale en
   la caja de texto del juego (ya no su "I'm a fan of Eve's").
3. Recoge unas Wild Grapes, un Fish o un Cake y presiona A frente a él: se
   va de tus manos y se pone muy contento. Un segundo regalo el mismo día
   se queda en tus manos.
4. Guarda y carga: su cariño se conserva (todavía no tiene fila de
   corazones en el menú de pausa; se puede agregar después, con su cara).

Si A frente a él no hace nada distinto, sus números de sprite o el número
de mapa del bar ($1E) no son los que se leyeron de los volcados de RAM:
están al principio de `native/suitors.c` (`kSuitors`).

