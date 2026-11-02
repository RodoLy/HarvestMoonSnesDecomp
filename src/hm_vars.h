/*
 * hm_vars.h — the game's variables that more than one file uses.
 *
 * Each place in RAM that two or more routine files read or write has one
 * name here, so it is called the same everywhere. The names that the
 * recompilation project already gives (the clock, the money, the bags,
 * HM_GAME_STATE ...) are in hm_ram.h and are not repeated. A place only one
 * file uses is named at the top of that file.
 *
 * The values are ram[] offsets, as in hm_ram.h: $7E:xxxx is 0x0xxxx and
 * $7F:xxxx is 0x1xxxx. The routines that read through the data bank or the
 * direct page use them as they are; the bus helpers (hm_bus_rd8 ...) take
 * WRAM_LONG(name), the same place as a 24-bit address.
 *
 * Sizes: 8b one byte, 16b a word, 24b a long pointer or number.
 */
#pragma once

#define WRAM_LONG(a)  (0x7E0000 + (a))   /* a ram[] offset as a bus address */

/* ── Direct page ($00-$FF) ─────────────────────────────────────────────────────────────
 * The hm_dp_* helpers take these offsets. Most of the direct page is scratch
 * that each routine uses its own way (those bytes are named in the files);
 * these few keep one meaning everywhere. */
#define HM_TILE_PROPS            0x0D    /* 24b: a long pointer to the map's tile
                                            properties table (the bank in $0F) */
#define HM_TILE_PROPS_BANK       0x0F    /* 8b: its bank */
#define HM_ANIMAL_PTR            0x72    /* 24b $72-$74: an animal's record, as
                                            GetCowPointer / GetChickenPointer leave it */
#define HM_ANIMAL_PTR_BANK       0x74    /* 8b: its bank ($7E) */
#define HM_MENU_FRAMES           0x90    /* 16b: the title menu's frames, to the demo */
#define HM_FADE_FROM             0x92    /* 8b: the fade routines' arguments: the
                                            starting brightness, */
#define HM_FADE_FRAMES           0x93    /* 8b: frames per brightness level, */
#define HM_FADE_TO               0x94    /* 8b: and the brightness the fade ends at */
#define HM_INTRO_STAGE           0x95    /* 8b: the start screens' stage: the intro's
                                            step, then the menu shown (MenuInputs; 5 =
                                            the title screen) */
#define HM_MENU_REDRAW           0x97    /* 8b: 1 = draw the menu again (the choice
                                            changed) */
#define HM_OBJ_X                 0x9B    /* 16b: the game object routines' arguments
                                            (objects.c): the position, */
#define HM_OBJ_Y                 0x9D    /* 16b */
#define HM_OBJ_FLIP              0x9F    /* 16b: the flip (bit 6 across, bit 7 up-down), */
#define HM_OBJ_SPRITE            0xA1    /* 16b: the sprite, */
#define HM_OBJ_PARAM             0xA3    /* 16b: a value for the object's +06, */
#define HM_OBJ_NUMBER            0xA5    /* 16b: and the object's number */
#define HM_OBJ_RESULT            0xA7    /* 16b: their answer: 0 done, $FFFF no room, 1
                                            not in use; ReadGameObject: $FFFF on screen,
                                            $FFFD out of the map */
#define HM_LOAD_BUDGET           0xC7    /* 16b: bytes the sprite piece loader may still
                                            send this frame */
#define HM_SCRIPT_POS            0xC9    /* 24b $C9-$CB: the position in the running
                                            event script */
#define HM_SCRIPT_BANK           0xCB    /* 8b: its bank */
#define HM_EVENT_SLOT            0xCC    /* 24b $CC-$CE: the event slot (CC slot) being
                                            run (events.c) */
#define HM_EVENT_SLOT_BANK       0xCE    /* 8b: its bank */
#define HM_MAP_ANIM              0xCF    /* 24b $CF-$D1: the running map animation's next
                                            step (0 = none; mapedit.c) */
#define HM_MAP_ANIM_BANK         0xD1    /* 8b: its bank */
#define HM_PLAYER_X              0xD6    /* 16b: the player's position in the map, pixels */
#define HM_PLAYER_Y              0xD8    /* 16b */
#define HM_PLAYER_DIRECTION      0xDA    /* 16b: the way the player faces: 0 down, 1 up,
                                            2 right, 3 left */
#define HM_GOBJ_COUNT            0xDC    /* 16b: game objects handed out so far (only
                                            grows, to 40; objects.c) */
#define HM_COLLIDE_X             0xDF    /* 16b: CollideMap's inputs (collision.c): the
                                            position of who moves, */
#define HM_COLLIDE_Y             0xE1    /* 16b */
#define HM_COLLIDE_AHEAD         0xE3    /* 16b: how far ahead the move looks, */
#define HM_COLLIDE_STEP_X        0xE5    /* 16b: and the step (its output: the step
                                            allowed) */
#define HM_COLLIDE_STEP_Y        0xE7    /* 16b */
#define HM_COLLIDE_TILE_1        0xE9    /* 16b: its outputs: the tile types under the
                                            two front corners */
#define HM_COLLIDE_TILE_2        0xEB    /* 16b */
#define HM_CAMERA_X              0xF5    /* 16b: the camera's position in the map */
#define HM_CAMERA_Y              0xF7    /* 16b */

/* ── Sound ($0103-$0119; audio.c) ────────────────────────────────────────────────────── */
#define HM_INSTRUMENT_COUNT      0x0103  /* 8b: how many instruments PrepareSoundEffect
                                            noted */
#define HM_WANTED_SONG           0x0110  /* 8b: the song asked for (audio.c sends the
                                            change) */
#define HM_SOUND_EFFECT          0x0114  /* 8b: the sound effect PlaySoundEffect0A plays, */
#define HM_SOUND_BANK            0x0115  /* 8b: and its bank */
#define HM_PLAYING_SONG          0x0117  /* 8b: the song playing */
#define HM_SONG_LOADED           0x0118  /* 8b: the song whose instruments are loaded (0
                                            for none); the sound effects borrow its
                                            instrument */
#define HM_LOADED_TOOL_SOUND     0x0119  /* 8b: the item whose noises are loaded (0 =
                                            load them again) */

/* ── The pads ($0124-$013B; joypad.c) ──────────────────────────────────────────────────
 * Bit 15 is B, then Y, Select, Start, up, down, left, right, A, X, L, R. */
#define HM_PAD1_HELD             0x0124  /* 16b: pad 1's buttons held */
#define HM_PAD1_LAST             0x0126  /* 16b: last frame's */
#define HM_PAD1_NEW              0x0128  /* 16b: pressed this frame */
#define HM_PAD1_UP               0x012A  /* 16b: let go this frame */
#define HM_PAD1_REPEAT           0x012C  /* 16b: the new presses, plus auto-repeat */
#define HM_PAD2_HELD             0x0130  /* 16b: the same for pad 2 */
#define HM_PAD2_LAST             0x0132  /* 16b */
#define HM_PAD2_NEW              0x0134  /* 16b */
#define HM_PAD2_UP               0x0136  /* 16b */

/* ── Screen ──────────────────────────────────────────────────────────────────────────── */
#define HM_BG2_X                 0x0140  /* 16b: BG2's scroll (the clouds' layer), across */
#define HM_BG2_Y                 0x0142  /* 16b: and down */
#define HM_BG3_Y                 0x0146  /* 16b: BG3's scroll down (the text box's layer) */
#define HM_UNKNOWN_0148          0x0148  /* 8b: cleared by reset.c and FirstNightReset
                                            (day.c); nothing found reads it */
#define HM_LOADED_PALETTE        0x017B  /* 8b: the palette in use (palette.c) */
#define HM_TRANSITION_X          0x017D  /* 16b: where the player appears after a map
                                            transition */
#define HM_TRANSITION_Y          0x017F  /* 16b */
#define HM_MAP_WIDTH             0x0181  /* 8b: the map's width: 1 = one screen (16
                                            tiles), 2 = two (32), else four (64); also an
                                            index of video.c's LAYER_SIZES */
#define HM_GRAPHIC_PRESET        0x0195  /* 8b: the map's graphics preset (x 16 it goes
                                            in the sprites' attributes) */
#define HM_WEATHER_FLAGS         0x0196  /* 16b: weather and day flags: bit 0 the map
                                            scrolls; 1 rain, 2 snow (BG2 clouds, colour
                                            animations); 1 / 3 / 9 special days (the
                                            ranch is closed, no Sunday market); 4 the TV
                                            stays off, some doors shut, song $16; 5 the
                                            clock was already stopped (text.c); 8 today's
                                            special day; 13 a name is to be written (the
                                            names screen); 14 a map transition is to
                                            start; 15 a picture stored as it is
                                            (video.c). Set by SetWeatherFlags (weather.c) */
#define HM_WEATHER_FLAGS_KEPT    0x0198  /* 16b: HM_WEATHER_FLAGS kept during the names
                                            screen */

/* ── Text boxes ($0183-$019B; text.c) ────────────────────────────────────────────────── */
#define HM_TEXT_NUMBER           0x0183  /* 16b: the text being shown (an index of
                                            TEXT_POINTERS) */
#define HM_TEXT_CODE_AT          0x0187  /* 16b: the code the printer is at (its number
                                            in the text) */
#define HM_ARROW_PLACE           0x018A  /* 8b: the choice arrow's place (0-11; the long
                                            lists 0-21) */
#define HM_ARROW_BLINK           0x018B  /* 8b: the arrow's (or the name cursor's) blink:
                                            bit 7 the phase, bits 0-6 a counter */
#define HM_PRINT_LEFT            0x018C  /* 8b: the digits or letters still to print
                                            (PrintDigit: the power of ten) */
#define HM_LAST_OPTION           0x018E  /* 8b: a choice's last option (its options - 1) */
#define HM_ANSWER                0x018F  /* 8b: the option chosen in the last text box (0
                                            = the first, "yes") */
#define HM_TEXT_BOX_LINES        0x0191  /* 8b: the lines of the next text box (0 = pick) */
#define HM_PRINT_NUMBER          0x0192  /* 24b $0192-$0194: the number PrintDigit prints */
#define HM_TEXT_BOX_STATE        0x019B  /* 8b: the text box: bit 0 printing, 1 a letter
                                            this frame, 2 the end, 3 waiting, 4 a choice,
                                            5 waiting at the box's end, 7 a digit was
                                            printed */

/* ── Sprites ─────────────────────────────────────────────────────────────────────────── */
#define HM_GAME_OBJECTS          0x019C  /* GOBJ_MAX records of GOBJ_SIZE bytes: the game
                                            objects (objects.c; their fields below) */
#define HM_OAM_SIZE_USED         0x084A  /* 16b: bytes of the sprite table filled this
                                            frame (objects.c) */
#define HM_PLAYER_GOBJ           0x0905  /* 16b: the player's game object */
#define HM_PLAYER_FLIP           0x090F  /* 16b: the player sprite's flip (a byte x 64) */
#define HM_PLAYER_TIMER          0x0915  /* 16b: the player's object's animation timer,
                                            sign-extended ($FFFF: the animation is over) */
#define HM_OAM_COPY              0xA000  /* the WRAM copy of the sprite table: 128 x 4
                                            bytes, */
#define HM_OAM_COPY_HIGH         0xA200  /* and its high table: 32 bytes */
#define HM_LOAD_COPIES           0xA420  /* the sprite loader's list: 6 bytes each
                                            (source, bank, VRAM) */

/* ── The player ($0878-$0926) ────────────────────────────────────────────────────────── */
#define HM_PLAYER_TILE_1         0x0878  /* 16b: the tile types under the player's front
                                            corners (HM_COLLIDE_TILE_1 / _2 of the last
                                            blocked move) */
#define HM_PLAYER_TILE_2         0x087A  /* 16b */
#define HM_CAMERA_SPEED_X        0x087C  /* 16b: the camera's own movement per frame
                                            (day.c) */
#define HM_CAMERA_SPEED_Y        0x087E  /* 16b */
#define HM_CAMERA_FRAMES         0x0880  /* 8b: for how many frames */
#define HM_FOOTPRINT_X           0x0907  /* 16b: where the player was 8 steps before (the
                                            last footprint) */
#define HM_FOOTPRINT_Y           0x0909  /* 16b */
#define HM_PLAYER_SCREEN_X       0x090B  /* 16b: where the player is on the screen */
#define HM_PLAYER_SCREEN_Y       0x090D  /* 16b */
#define HM_PLAYER_TURN           0x0911  /* 16b: the direction being tried
                                            (playerinput.c) */
#define HM_PLAYER_FACING         0x0913  /* 16b: the facing, copied from HM_PLAYER_TURN
                                            at the frame's end; the direction CollideMap
                                            is given */
#define HM_STEP_COUNT            0x0919  /* 8b: moves since the last footprint (to 8) */
#define HM_STEP_SOUND_TIMER      0x091A  /* 8b: frames to the next step sound (to 16) */
#define HM_TOOL_FRAMES           0x091B  /* 8b: frames of the tool's use (its sound at
                                            $18) */
#define HM_BG2_DRIFT_TIMER       0x091C  /* 8b: frames to the clouds' next drift (to 10;
                                            scroll.c) */
#define HM_SHOWN_ITEM            0x091E  /* 8b: the item the player holds up
                                            (TalkOrTakeShownItem) */
#define HM_PICKED_TILE_BYTE      0x091F  /* 8b: byte +0 of the tile picked up */
#define HM_CARRIED_SLOT          0x0920  /* 8b: the event slot (an animal) being carried */
#define HM_TOOL_SOUND_CHOICE     0x0922  /* 8b: what picks the tool's sound: 0 it lands
                                            on something, 1 it does not, 2 silent
                                            (CheckToolSuccessForSound) */
#define HM_MEAL_CHOICE           0x0924  /* 8b: the meal rolled when the player eats
                                            (0-2, stamina.c / day.c); the eating
                                            animation is $30 + it */
#define HM_IDLE_TIMER            0x0925  /* 8b: frames standing idle (to $F0) */
#define HM_CAN_WATER             0x0926  /* 8b: water left in the watering can */

/* ── The farm ($092E-$0953) ──────────────────────────────────────────────────────────── */
#define HM_GROWN_GRASS_N         0x092E  /* 16b: grass tiles fully grown (one less for
                                            each tuft cut or eaten) */
#define HM_COWS_FED              0x0930  /* 8b: the barn's feed boxes filled today */
#define HM_CHICKENS_FED          0x0931  /* 8b: the coop's feed boxes filled today */
#define HM_COW_TROUGHS           0x0932  /* 16b: one bit per barn feed box with food */
#define HM_CHICKEN_TROUGHS       0x0934  /* 16b: one bit per coop feed box with food */
#define HM_DEAD_COW              0x0937  /* 8b: the cow of the cow's scene (the one that
                                            died) */
#define HM_DOG_TIMER             0x0938  /* 8b: the dog's frames between moods (to $78) */
#define HM_HORSE_TIMER           0x0939  /* 8b: the horse's frames between idle
                                            animations (to $78) */
#define HM_MOLE_TIMER            0x093A  /* 8b: the mole's frames (to $B4; the hoe's mole
                                            clears it) */
#define HM_CHICKEN_TIMERS        0x093B  /* 16b per chicken */
#define HM_COW_TIMERS            0x0953  /* 16b per cow */

/* ── Actions and tools ($096B-$0984) ─────────────────────────────────────────────────── */
#define HM_SOW_STEP              0x096B  /* 8b: which tile of a multi-tile use (the
                                            seeds, the watering can) */
#define HM_TOOL_HITS             0x096D  /* 8b: the hammer's and the axe's hits on a big
                                            stone or a stump */
#define HM_INTERACTION           0x096E  /* 8b: the action PlayerInteractionSelector runs
                                            (with game state bit $40): $43 / $44 sell a
                                            cow / chicken, $56 talk... */
#define HM_INTERACTION_STEP      0x096F  /* 8b: its step */
#define HM_INTERACTION_FRAMES    0x0970  /* 8b: its frame counter */
#define HM_ITEM_FLAGS            0x0974  /* 8b: the map object to spawn or work on
                                            (objects.c): its flags (bit 2: the jump's
                                            sprite), */
#define HM_ITEM_ROUTINE          0x0975  /* 8b: its routine (1 = RunItemRoutine), */
#define HM_ITEM_STATE            0x0976  /* 8b: its state, */
#define HM_ITEM_SPRITE           0x097A  /* 16b: its sprite, */
#define HM_ITEM_FLIP             0x097E  /* 16b: its flip, */
#define HM_ITEM_X                0x0980  /* 16b: its position */
#define HM_ITEM_Y                0x0982  /* 16b */
#define HM_ITEM                  0x0984  /* 8b: the item being worked on (or eaten) */
#define HM_TILE_IN_FRONT_X       0x0985  /* 16b: the tile the player faces
                                            (CalculateTileInFront); where a tool lands,
                                            where StartScriptAtTileInFront starts */
#define HM_TILE_IN_FRONT_Y       0x0987  /* 16b */
#define HM_DOOR_STEP             0x098A  /* 8b: frames of the walk out of a door (to $0D) */
#define HM_TRANSITION_MAP        0x098B  /* 8b: the map a transition goes to (for the
                                            house: its size) */

/* ── Names and menus ($0881-$09A1) ───────────────────────────────────────────────────── */
#define HM_PLAYER_NAME           0x0881  /* 4 bytes: the player's name */
#define HM_NAME_TYPED            0x0885  /* 4 bytes: the name being typed ($B1 = empty) */
#define HM_TEXT_NAME             0x0889  /* 4 words $0889-$088F: a name the text prints
                                            (the cow's) */
#define HM_DOG_NAME              0x0899  /* 4 bytes */
#define HM_HORSE_NAME            0x089D  /* 4 bytes */
#define HM_SEASON_TEXT           0x08B3  /* 6 x 16b: the season's name, as text codes
                                            (LoadDateNames) */
#define HM_PLAYER_NAME_WORDS     0x08D5  /* 4 x 16b: 16-bit copies of the names: the
                                            player's, */
#define HM_DOG_NAME_WORDS        0x08DD  /* the dog's, */
#define HM_HORSE_NAME_WORDS      0x08E5  /* the horse's, */
#define HM_KID1_NAME_WORDS       0x08ED  /* and the children's */
#define HM_KID2_NAME_WORDS       0x08F5
#define HM_SCRIPT_BUTTONS        0x08FD  /* 16b: the buttons an event's script waits for,
                                            or presses for the player */
#define HM_WAIT_FRAMES           0x08FF  /* 16b: frames left of an event's wait */
#define HM_TV_FORECAST           0x0990  /* 8b: what the TV shows (its channel;
                                            weather.c) */
#define HM_NAME_CELL             0x0991  /* 16b: the names screen's cell under the cursor
                                            (menus.c) */
#define HM_NAME_PAGE             0x0993  /* 8b: the page shown (0-2; 3 = done) */
#define HM_NAME_COUNT            0x0994  /* 8b: how many letters are typed */
#define HM_NAME_CURSOR_X         0x099B  /* 16b: the cursor's position, across */
#define HM_NAME_CURSOR_Y         0x099D  /* 16b */
#define HM_NAME_FOR              0x099F  /* 8b: whose name the names screen writes: 0 the
                                            player, 1 a cow bought, 2 a calf, 3 the dog,
                                            4 the horse, 5 / 6 the children */
#define HM_SOLD_COW_SLOT         0x09A0  /* 8b: the event slot of the cow being sold */
#define HM_SOLD_CHICKEN_SLOT     0x09A1  /* 8b: the event slot of the chicken being sold */

/* ── Other ($09A3-$B4E6) ─────────────────────────────────────────────────────────────── */
#define HM_VISIT_NIGHTS          0x09A3  /* 8b: nights counted for a visit (to 3; day.c) */
#define HM_FENCE_PIECES          0x09A4  /* 8 bytes $09A4-$09AB: the piece (1-5, 0 none)
                                            at each place; shuffled in town on fall 20
                                            (mapevents.c), taken at the places $1D,
                                            $4A-$50 (actions.c) */
#define HM_EDIT_X                0x09AD  /* 16b: the tile being changed (ChangeMapTile),
                                            pixels */
#define HM_EDIT_Y                0x09AF  /* 16b */
#define HM_TEXT_AUTO_CLOSE_TIMER 0x09B1  /* 16b: frames a mode-6 text box has been open */
#define HM_FESTIVAL_BEST         0x09B5  /* 8b: the festival cow's affection, the best so
                                            far (animals.c) */
#define HM_MAP_TILES             0x09B6  /* 64 x 64 bytes: the current map's tile types */
#define HM_FARM_TILES            0xA4E6  /* 64 x 64 bytes: the farm's own copy of its
                                            tile types (saved) */
#define HM_MAP_OBJECTS           0xB4E6  /* 10 records of 16 bytes: the map objects
                                            (objects.c, items.c) */

/* ── The saved state ($7F:1Fxx) ──────────────────────────────────────────────────────── */
#define HM_MONEY_HIGH            (HM_MONEY + 2) /* 8b: the money's third byte */
#define HM_SHIPPING_MONEY_HIGH   (HM_SHIPPING_MONEY + 2) /* 8b: the shipping money's
                                                            third byte */
#define HM_CHICKEN_N             0x11F0B /* 8b: chickens owned */
#define HM_LUMBER_NEEDED         0x11F0E /* 16b: the lumber the next house extension
                                            needs (250 / 500) */
#define HM_PREGNANCY_DAYS        0x11F12 /* 8b: days until the pregnant cow gives birth */
#define HM_SALE_PRICE            0x11F15 /* 24b: what a sale pays */
#define HM_SALE_PRICE_HIGH       (HM_SALE_PRICE + 2) /* 8b: its third byte */
#define HM_PLANTED_GRASS         0x11F29 /* 16b: grass tiles planted (one more per seed,
                                            one less when one goes); buying an animal
                                            checks it against 16 per animal (actions.c) */
#define HM_NEWBORN_HAPPINESS     0x11F2B /* 8b: the happiness the calf will start with */
#define HM_DOG_X                 0x11F2C /* 16b: the dog's position */
#define HM_DOG_Y                 0x11F2E /* 16b */
#define HM_DOG_MAP               0x11F30 /* 8b: the map the dog is on */
#define HM_HORSE_MAP             0x11F31 /* 8b: the map the horse is on */
#define HM_HORSE_AGE             0x11F32 /* 8b: the horse's days; $15 = grown */
#define HM_BUILD_NIGHTS          0x11F35 /* 8b: nights the house has been building
                                            (day.c) */
#define HM_POWER_BERRIES         0x11F36 /* 8b: power berries eaten */
#define HM_KID1_AGE              0x11F37 /* 16b: the first child's days, from conception */
#define HM_KID2_AGE              0x11F39 /* 16b: the second child's */
#define HM_DAYS_MARRIED          0x11F3B /* 16b: days married (people.c; every 120 the
                                            anniversary) */
#define HM_KID1_NAME             0x11F3D /* 4 bytes: the first child's name */
#define HM_KID2_NAME             0x11F41 /* 4 bytes: the second child's */
#define HM_HEN_EGGS              0x11F45 /* 16b: one bit per hen: she lays an egg */
#define HM_ENDING_STEP           0x11F47 /* 8b: the ending's next part */
#define HM_DEMO_STEP             0x11F48 /* 8b: the next demo of DemoPresentation (0-$0B) */
#define HM_INTRO_STEP            0x11F49 /* 8b: the days of the introduction (0-$1C;
                                            mapevents.c) */
#define HM_SHIPPED_CORN          0x11F4A /* 16b each: the crops shipped */
#define HM_SHIPPED_TOMATOES      0x11F4C /* 16b */
#define HM_SHIPPED_TURNIPS       0x11F4E /* 16b */
#define HM_SHIPPED_POTATOES      0x11F50 /* 16b */
#define HM_RANCH_DEVELOPMENT     0x11F56 /* 16b: the ranch's development (%; farm.c) */
#define HM_DOG_MOOD              0x11F58 /* 16b: one mood bit: 1, 2, 4, 8 or $10 */

/* ── Flag words ($7F:1F5A-$7F:1F7B) ────────────────────────────────────────────────────
 * The bits known so far, with the file that uses them. $5A-$62 and $74-$76
 * are cleared every day; $64-$72 are the story's and are saved. */
#define HM_FLAGS_5A              0x11F5A /* 16b: bit 0 map $37 seen; 1 a cow is being
                                            bought; $02 a cow comes tomorrow; $04 / $08 a
                                            cow / chicken is sold; $10 the bell calls the
                                            cows; $20 an animal bought today; $40 a tool
                                            was used; $80 the medicine; $0100 the miracle
                                            potion; $0200 the fishing rod; $0400 the
                                            shipping scene runs; $0800 its text; $1000 a
                                            cow was left out; $4000 a text box is open;
                                            $8000 the screen is faded in, a scene can be
                                            skipped */
#define HM_FLAGS_5C              0x11F5C /* 16b: $01 time stopped (and stays so); $02 no
                                            weather layer; $04 the event script is over;
                                            $08 no tile checks; $10 shipping at an
                                            object; $20 no control; $40 shipped; the
                                            horse's requests ($10, $20, $40, $0400,
                                            $0800); the mountain's finds of the day
                                            ($2000-$8000) */
#define HM_FLAGS_5E              0x11F5E /* 16b: bit 0 an item shipped this frame; $02 no
                                            animals, the ending, no map extras; $04 no
                                            breakfast; $40 lets a slot face the player;
                                            $80 / $0100 / $0200 sprite palette $71-$73;
                                            $0800 the long walk to bed; $1000 what it
                                            waits for (a map event); $8000 the music goes
                                            on after 18:00, the ending's slide show */
#define HM_FLAGS_60              0x11F60 /* 16b: bit 0 no feeding (set with
                                            HM_WEATHER_FLAGS bit 4); $02 / $04 block the
                                            player's actions (commands 52 / 53), a child
                                            carried; $08 energy frozen, go to map $3C;
                                            $10 calls the dog; $20 sent home; $40
                                            (without $0100) holds a transition; $80 the
                                            demo is over; $0200 the watch shows text
                                            $046A; $0400 the town's scene; $0800 both
                                            diaries were cleared or failed their check;
                                            $1000 / $2000 a chicken / cow at the
                                            festival; $4000 nothing blocks; $8000 no
                                            tools */
#define HM_FLAGS_62              0x11F62 /* 16b day flags */
#define HM_FLAGS_64              0x11F64 /* 16b story flags: bit 0 / 1 the summer /
                                            winter storm seen; $04 a cow was given the
                                            potion; $08 a calf was born; $10 / $20
                                            today's special days; $40 / $80 the house's
                                            first / second extension */
#define HM_FLAGS_66              0x11F66 /* 16b story flags: bits 0-4 the girl married
                                            (Maria ... Eve, a bit each; palette.c: the
                                            child's stage); $20 -> $40; $80 the house is
                                            being built; $0100 it is done; bits 9-12 the
                                            house painted; $2000 the spring's event seen
                                            today; $4000 / $8000 the festival entries */
#define HM_FLAGS_68              0x11F68 /* 16b story flags: $01 a day went by (the ranch
                                            and the shops are open); $80 a dog; $0100 a
                                            horse; $0800 -> $1000; $8000 the golden
                                            hammer */
#define HM_FLAGS_6A              0x11F6A /* 16b story flags: bits 0-3 the gold tools on
                                            sale; $40 a power berry eaten on map $29; $80
                                            the golden hoe; $0400 -> $0800; visits $1000
                                            / $4000 */
#define HM_FLAGS_6C              0x11F6C /* 16b story flags: visits $01, $04, $10 ($01
                                            also: the exits to map $1C closed); $0080 the
                                            anniversary; $0100 / $0200 the children's
                                            birthdays (they last a day); $1000 the turtle
                                            shell; $4000 a power berry eaten at home */
#define HM_FLAGS_6E              0x11F6E /* 16b story flags: $04 / $08 the second / first
                                            child; $10 no bed; $0200 a fence broke; $2000
                                            an egg is hatching; $4000 the clock; $8000 a
                                            tool back to the shed */
#define HM_FLAGS_70              0x11F70 /* 16b: $02 today's special day
                                            (HM_WEATHER_FLAGS bit 8); $04 the first
                                            spring or summer */
#define HM_FLAGS_72              0x11F72 /* 16b: the last of the story's words */
#define HM_FLAGS_74              0x11F74 /* 16b day flags: bit 5 the festival eggs
                                            vanish, bit 6 one was placed; events seen
                                            (the Starry Night) */
#define HM_FLAGS_76              0x11F76 /* 16b day flags */
#define HM_FLAGS_78              0x11F78 /* 16b: the fence pieces put up, a bit each */
#define HM_FLAGS_7A              0x11F7A /* 16b: the scene flags (SetSceneFlag) */

/* ── Records ───────────────────────────────────────────────────────────────────────────
 * Offsets inside a record (read through a pointer such as HM_ANIMAL_PTR or
 * HM_EVENT_SLOT), so they carry the record's name instead of HM_. */
#define ANIMAL_STATUS            0x00    /* 8b: the first byte of a cow's or a chicken's
                                            record. Cows: bit 0 it exists, 1 newborn, 2
                                            calf, 3 adult, 4 cranky, 5 sick, 6 pregnant,
                                            7 gave birth. Chickens: bit 0 it exists, 1
                                            egg, 2 chick, 3 adult, 4 hungry */
#define COW_DAILY                0x01    /* 8b: a cow's (HM_COW_DATA, 16 bytes each)
                                            flags of the day (bit 7 the potion) */
#define COW_MAP                  0x02    /* 8b: the map it is on */
#define COW_DAYS                 0x03    /* 8b: days as a calf, pregnant, sick or cranky */
#define COW_HAPPINESS            0x04    /* 8b */
#define COW_HITS                 0x05    /* 8b: times hit with a tool (to 10); at 10 it
                                            turns cranky after its meal (animals.c) */
#define COW_X                    0x08    /* 16b: its position */
#define COW_Y                    0x0A    /* 16b */
#define COW_NAME                 0x0C    /* 4 bytes: its name */
#define CHICKEN_MAP              0x01    /* 8b: a chicken's (HM_CHICKEN_DATA, 8 bytes
                                            each) map */
#define CHICKEN_DAYS             0x02    /* 8b: a counter: nights as an egg, as a chick,
                                            hungry */
#define CHICKEN_X                0x04    /* 16b: its position */
#define CHICKEN_Y                0x06    /* 16b */
#define MOBJ_FLAGS               0x00    /* 8b: a map object's (HM_MAP_OBJECTS, 16 bytes
                                            each) flags: bit 0 in use, 1 drawn on the
                                            map, 2 kept off screen */
#define MOBJ_ROUTINE             0x01    /* 8b: its routine (table $81:A58B) */
#define MOBJ_STATE               0x02    /* 8b: its state */
#define MOBJ_ITEM                0x03    /* 8b: the item it is */
#define MOBJ_GOBJ                0x04    /* 16b: its game object */
#define MOBJ_SPRITE              0x06    /* 16b: its sprite */
#define MOBJ_SPRITE_SHOWN        0x08    /* 16b: the sprite shown now */
#define MOBJ_FLIP                0x0A    /* 16b: its flip */
#define MOBJ_X                   0x0C    /* 16b: its position */
#define MOBJ_Y                   0x0E    /* 16b */
#define SLOT_IN_USE              0x00    /* 8b: an event slot's (CC slot, 64 bytes;
                                            events.c) first byte: bit 0 in use, bit 1
                                            passable */
#define SLOT_FLAGS               0x01    /* 8b: bit 0 has a sprite, 1 create the object,
                                            2 the sprite changed, 3 wanders, 4 frozen */
#define SLOT_ANIM_ENTRY          0x02    /* 8b: the entry in its animation table (the way
                                            it faces) */
#define SLOT_STEP_X              0x03    /* 8b signed: its step */
#define SLOT_STEP_Y              0x04    /* 8b signed */
#define SLOT_STEP_DELAY          0x05    /* 8b: frames between steps (StepSlot reloads
                                            SLOT_STEP_TIMER from it) */
#define SLOT_STEP_TIMER          0x06    /* 8b: frames until the next step */
#define SLOT_WALK_STEPS          0x07    /* 8b: steps of a walk */
#define SLOT_STEPS_LEFT          0x08    /* 8b: steps left of this walk */
#define SLOT_RANGE_X             0x09    /* 8b: how far it may wander from home, across */
#define SLOT_RANGE_Y             0x0A    /* 8b: and down */
#define SLOT_WALK_DELAY          0x0B    /* 8b: a walk's frames between steps */
#define SLOT_PRESSED             0x0C    /* 8b: the player pressed A on it / talked to it
                                            (CollideEventObjects) */
#define SLOT_ROLL                0x0D    /* 8b: the last die roll (GetRNG) */
#define SLOT_STATE               0x0E    /* 8b: $E0 being sold, $E1 off the farm, $C2 in
                                            the barn; also the tile type ahead */
#define SLOT_FORCED_DIR          0x0F    /* 8b: the direction the script forces: 1 down,
                                            2 up, 4 right, 8 left */
#define SLOT_FRAMES              0x10    /* 16b: a frame counter (the wait) */
#define SLOT_OBJECT              0x12    /* 16b: its game object */
#define SLOT_ANIMATION           0x14    /* 16b: the object's animation (a byte x 64) */
#define SLOT_SPRITE              0x16    /* 16b: the object's sprite */
#define SLOT_LAST_SPRITE         0x18    /* 16b: the sprite last given to the object */
#define SLOT_X                   0x1A    /* 16b: its position */
#define SLOT_Y                   0x1C    /* 16b */
#define SLOT_HOME_X              0x1E    /* 16b: where its walks start (the middle of its
                                            area) */
#define SLOT_HOME_Y              0x20    /* 16b */
#define SLOT_SCRIPT              0x30    /* 16b: where its script is, */
#define SLOT_SCRIPT_BANK         0x32    /* 8b: and its bank */
#define SLOT_ANIM_TABLE          0x33    /* 16b: its standing animation table, $B3:<word> */
#define SLOT_WALK_TABLE          0x36    /* 16b: its walking animation table */
#define SLOT_NUMBER              0x3F    /* 8b: its own number */
#define COW_SLOT0                0x18    /* the event slot of cow n is $18 + n, */
#define CHICKEN_SLOT0            0x24    /* and chicken n's $24 + n (PlaceAnimalsOnMap,
                                            animals.c) */
#define GOBJ_STATUS              0x00    /* 16b: a game object's (HM_GAME_OBJECTS)
                                            status: GOBJ_IN_USE, or 0 = free */
#define GOBJ_SPRITE              0x02    /* its sprite number */
#define GOBJ_FLIP                0x04    /* its flip: bit 6 across, bit 7 up-down */
#define GOBJ_PARAM               0x06    /* copied from HM_OBJ_PARAM */
#define GOBJ_X                   0x08    /* 16b: its position */
#define GOBJ_Y                   0x0A    /* 16b */
#define GOBJ_FIRST_FRAME         0x0C    /* the sprite's entry: its first frame */
#define GOBJ_NEXT_FRAME          0x0E    /* the frame to show after this one */
#define GOBJ_PIECE_LIST          0x10    /* the address of the frame's piece list */
#define GOBJ_TIMER               0x12    /* 8b: frames left on this frame ($FE free, $FF
                                            for ever) */
#define GOBJ_PIECE_COUNT         0x13    /* 8b: how many pieces */
#define GOBJ_PIECE_SLOTS         0x14    /* 16 bytes: one per piece, its graphics slot
                                            ($FF = queued) */
#define GOBJ_SIZE                0x24    /* the size of a game object's record (36 bytes) */
#define GOBJ_MAX                 0x28    /* how many there are (40) */
#define GOBJ_IN_USE              0x7777  /* GOBJ_STATUS's value for an object in use */
