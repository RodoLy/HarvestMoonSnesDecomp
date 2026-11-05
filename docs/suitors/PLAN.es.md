# Pretendientes hombres: investigación y plan

*(English: [PLAN.md](PLAN.md))*

Una mejora planeada para la versión nativa, después de la Diosa de la
Cosecha (`native/goddess.c`): hombres solteros del pueblo a los que el
jugador puede cortejar y con los que se puede casar, igual que con las
chicas. Todavía no hay nada construido; aquí empieza el trabajo.

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
| El fan de Eve | $269-$274 (joven de pelo corto) | el bar, mapa $1E (slot 4) | texto $120: "Good evening. I'm a fan of Eve's, you know." |
| Los carpinteros (hermanos) | $25A-$264 (también cargando madera y con el martillo) | la montaña, mapa $10; su casa, mapa $2B | textos $128/$129: "We are carpenters"; viven juntos y no mencionan esposa (el juego no lo dice) |
| El aprendiz de repartidor | sin confirmar; lo más probable $28F-$299 (joven de camisa azul, el de más poses) | — | texto $117/$118: "I'm a trainee of shipping too... With grit!" |

Primer paso acordado: **el fan de Eve y uno de los carpinteros**; luego el
aprendiz, cuando se confirme su sprite.

### Descartados

- El cantinero ($285): tío de Eve (texto $11E: "she is my niece"), no es joven.
- El papá de Ann (tienda de herramientas): viudo (textos $1DC, $246).
- El papá de Ellen ($2A4, el borracho: "hic").
- El alcalde (papá de María): casado.
- El pescador ($265): un señor mayor con bastón (texto $12B).
- El cazador ($27D): abuelo de Eve.
- El padre de la iglesia ($2A5); los vendedores ambulantes (no viven en el pueblo).

## Cómo se construiría (las mismas piezas que la Diosa)

1. Corazones y regalos: sus cosas favoritas, sus frases según los
   corazones, en la caja de texto del propio juego (`native/textbox.c`).
2. Propuesta: la Blue Feather (`g_hm_feather_used`), con las condiciones
   del juego (corazones, la casa ampliada).
3. Boda: la escena del juego (grupo de eventos $1E, mapa $2D) con él
   vestido en el slot de la novia mediante `g_hm_slot_picture`, como la Diosa.
4. Vida de casados: el día de un esposo (en casa en la mañana, afuera desde
   las 9:00, en casa a las 18:00), caminando con sus propias poses (no flota).
5. Hijos: el juego solo tiene un parto; para dos hombres, una escena de
   **adopción** propia (por ejemplo, los duendes de la cosecha traen un
   bebé); después los hijos crecen con el sistema del juego
   (`hm_WifePregnancyAndChildren`).
6. El final: prestar el bit de esposa el último día, como con la Diosa
   (`lend_for_ending`), y vestir las figuras de la esposa como él.

Apagado de fábrica, con su propia opción en `config.ini`, como `HarvestGoddess`.

## Para seguir en otra sesión

Abre la rama `male-suitors` (que sale de `harvest-goddess`) de
RodoLy/HarvestMoonSnesDecomp y pide que primero lea `docs/suitors/PLAN.es.md`.
