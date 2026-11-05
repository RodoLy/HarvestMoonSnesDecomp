#!/usr/bin/env python3
"""dump_texts.py - writes out every text of the game, one per line.

    python3 tools/dump_texts.py rom.sfc texts.txt

Each line is "nnn: text", nnn the text's number in hex (the index of
Text_Pointer_Table, $83:9BF6, 3 bytes a text; see src/text.c). The codes
are the game's: 0-25 a-z, 26-51 A-Z, $BC-$C5 the digits, $34-$41 the
punctuation, $B1 a space, $A2 "wait for a button" (written " / "),
$FFFC a number, $FFFD a name, $FFFE a choice, $FFFF the end. Anything
else is written as [code].

Lo usa docs/suitors/PLAN.md (los pretendientes) para buscar quién dice qué.
"""
import sys

TEXT_POINTERS = 0x839BF6
PUNCTUATION = {0x34: "'", 0x35: '!', 0x36: '.', 0x37: '?', 0x38: '"', 0x39: '/', 0x3A: ',',
               0x3B: '(', 0x3C: ')', 0x3D: '%', 0x3E: '-', 0x3F: '…', 0x40: '&', 0x41: ':'}


def file_offset(address):
    """A LoROM address ($80-$FF banks) as an offset in a headerless ROM."""
    return ((address >> 16) & 0x7F) * 0x8000 + ((address & 0xFFFF) - 0x8000)


def text_at(rom, at):
    out = ''
    for _ in range(600):
        if at + 1 >= len(rom):
            break
        code = rom[at] | rom[at + 1] << 8
        at += 2
        if code == 0xFFFF:
            break
        if code == 0xFFFE:
            out += '{choice %d}' % (rom[at] | rom[at + 1] << 8); at += 2
        elif code == 0xFFFD:
            out += '{name %d}' % (rom[at + 2] | rom[at + 3] << 8); at += 4
        elif code == 0xFFFC:
            out += '{number}'; at += 4
        elif code == 0xA2:
            out += ' / '
        elif code == 0xB1:
            out += ' '
        elif code < 26:
            out += chr(ord('a') + code)
        elif code < 52:
            out += chr(ord('A') + code - 26)
        elif code in PUNCTUATION:
            out += PUNCTUATION[code]
        elif 0xBC <= code < 0xC6:
            out += chr(ord('0') + code - 0xBC)
        else:
            out += '[%X]' % code
    return out


def main():
    rom = open(sys.argv[1], 'rb').read()
    lines = []
    for n in range(0x500):
        p = file_offset(TEXT_POINTERS) + 3 * n
        address = rom[p] | rom[p + 1] << 8 | rom[p + 2] << 16
        if address < 0x800000 or file_offset(address) >= len(rom) - 2:
            lines.append('%03X: <%06X>' % (n, address))
            continue
        lines.append('%03X: %s' % (n, text_at(rom, file_offset(address))))
    open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
    print(len(lines), 'texts')


if __name__ == '__main__':
    main()
