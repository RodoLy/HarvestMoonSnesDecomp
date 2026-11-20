# Pretendientes hombres: investigación y plan

*(English: [PLAN.md](PLAN.md))*

Una mejora planeada para la versión nativa, después de la Diosa de la
Cosecha (`native/goddess.c`): hombres solteros del pueblo a los que el
jugador puede cortejar y con los que se puede casar, igual que con las
chicas.

**Hecho hasta ahora:** el paso 1 (corazones y regalos) para cuatro: el fan
de Eve (el Visitor), un carpintero, el papá de Ann y el repartidor de
chaleco, en `native/suitors.c`, con su propia opción (`MaleSuitors` en
`config.ini`, "Pretendientes hombres" en el lanzador). El diario guarda a
los cuatro (su cariño / 4, un byte cada uno; ver `src/save.c`). Compila,
pero **todavía no se ha probado en el juego** (ver "Para probarlo" al final).

| Quién | Le encanta | No le gusta |
|---|---|---|
| El fan de Eve (el Visitor) | Wild Grapes, Fish, Cake | la hierba, Poison Mushroom |
| Un carpintero | Large Milk, Corn, Mushroom | Flower, los perfumes, Poison Mushroom |
| El papá de Ann | Potato, Egg, Herb | la hierba, Poison Mushroom |
| El repartidor de chaleco | Tomato, Tropical Fruit, Large Milk | la hierba, Poison Mushroom |

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

## Cómo está hecho (las mismas piezas que la Diosa): todo listo

1. Corazones y regalos: sus cosas favoritas, sus frases según los
   corazones, en la caja de texto del propio juego (`native/textbox.c`).
2. Su evento de corazón (aprobado), como los de las chicas: al llegar a
   cierto cariño, un día de buen clima y sin festival, **alguien llega a tu
   granja a avisarte**; vas a ayudar, y al resolverlo el día termina (así son
   los del juego: Maria se escapa, Ellen pierde a P-Chan, el abuelo de Eve se
   lastima en la cueva, Ann pierde su veleta, Nina no vuelve de la cueva).
   Una vez cada uno:
   - **El fan de Eve:** Eve llega preocupada: "Mi fan de siempre no vino
     anoche, y él nunca falta." Está en la montaña, junto al lago, con un
     cuaderno: escribía una canción, no para Eve, para ti.
   - **El carpintero:** su hermano llega corriendo: "¡Mi hermano subió a
     cortar madera y no ha bajado!" Está en el bosque de la montaña, con un
     tobillo torcido bajo unos troncos; lo ayudas a bajar. Al día siguiente
     te deja un banquito de roble.
   - **El papá de Ann:** Ann llega: "Mi papá salió en la mañana y no ha
     vuelto." Está en la tumba de su esposa, junto a la iglesia: te cuenta de
     ella, y si llevas un Flower lo dejas junto a las suyas (lo que más
     sube); "quedarte un rato" también sube; "irte" no sube ni baja. Al día
     siguiente Ann: "Mi papá llegó contento ayer... ¿sabes por qué?". Si el
     juego no tiene tumbas, una lápida pequeña dibujada por nosotros junto a
     la iglesia.
   - **El repartidor de chaleco:** el de gorra llega: "¡Mi compañero perdió
     la bolsa con los pagos del día, y si no aparece lo corren!" Está en algún
     lugar de su ruta (en el pueblo, detrás de una cerca); él te da las
     gracias viendo el atardecer.

   **Un solo carpintero:** los hermanos tienen el mismo dibujo; solo uno es
   pretendiente (dice las frases nuevas) y los demás siguen con las suyas
   (cómo se distingue: más abajo).
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

## El quinto: el aprendiz

El repartidor de gorra ($276-$27B, con todas sus poses) es el aprendiz del
de chaleco, y también soltero. Su evento de corazón: el de chaleco llega a
la granja ("mi aprendiz fue a entregar a la montaña y no ha vuelto"); está
perdido en el camino de la montaña, y lo acompañas de regreso (o te burlas
de él).

**Un solo carpintero:** es el que tiene su propio guion (banco $B3,
$EE81-$EEA5: el que dice el texto $128), en `kSuitors` como `script_from` /
`script_to`. Los otros dos conservan lo suyo.

## Dónde pasa cada evento de corazón

| Pretendiente | Quién viene | Dónde está él |
|---|---|---|
| Fan de Eve | Eve | el lago de la montaña, junto a la cerca, mirando el agua |
| Carpintero | su hermano | el bosque de la montaña, junto a los troncos |
| Papá de Ann | Ann | las tumbas junto a la iglesia, mirándolas |
| Repartidor (chaleco) | el aprendiz | el pueblo, junto a la cerca del camino |
| Aprendiz (gorra) | el repartidor | el camino de la montaña, pasando el árbol grande |

## Para seguir en otra sesión

Abre la rama `male-suitors` (que sale de `harvest-goddess`) de
RodoLy/HarvestMoonSnesDecomp y pide que primero lea `docs/suitors/PLAN.es.md`.

## Para probarlo

1. Activa "Pretendientes hombres" en las mejoras del lanzador.
2. **Corazones y regalos.** Presiona A frente a cada uno: el fan de Eve (el
   bar, de noche), el carpintero (la montaña, junto a su casa), el papá de
   Ann (la tienda de herramientas), los dos repartidores (el bar, de
   noche). Con uno de sus favoritos en las manos, presiona A: se va de tus
   manos. En el menú de pausa, la página de Corazones muestra los suyos con
   Arriba / Abajo.
3. **Evento de corazón.** Con cinco corazones (500), en un día de visitas
   (2-7, 13-17, 25-28, como las de las chicas en el juego) de buen clima y
   sin festival, la primera vez que sales de la casa (6:00): te quedas en
   la puerta y alguien sube por el camino hacia ti. Ve a donde te dice y
   acércate a él: su escena, una pregunta, y el día termina. A la mañana siguiente hay algo en la granja. Para el papá de
   Ann, lleva una flor.
4. **Baile.** En el Festival de las Flores o el de la Cosecha, presiona A
   frente a él.
5. **Propuesta.** Diez corazones (1000), la casa ampliada una vez y nadie
   más: usa la Pluma Azul frente a él. La boda es una semana después, la
   próxima vez que pises la granja.
6. **Vida de casados.** Está en casa en la mañana y en la noche, en la
   granja de 9:00 a 18:00 (en casa si llueve), caminando con sus propias
   poses.
7. **El bebé.** Como en el juego: su cariño (450), 20 días de casados y la
   casa ampliada dos veces. La mañana después de la noche en que el juego
   cuenta un hijo en camino, él habla de adoptar; 60 días después los
   Duendes de la Cosecha entran por la puerta con el bebé, y desde entonces
   es el propio hijo del juego, en la casa.
8. **El final** (verano 30 del tercer año): lo recorre contigo.
