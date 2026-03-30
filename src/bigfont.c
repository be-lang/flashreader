#include "bigfont.h"
#include <string.h>

/* Each glyph is 5 rows of ASCII: '#' = filled, ' ' = empty.
   We convert '#' to the UTF-8 block character at render time. */

struct glyph {
    int width;
    const char *rows[BIGFONT_ROWS];
};

/* Block: \xe2\x96\x88 (3 bytes) */
#define B "#"

static const struct glyph g_A = {5, {
    " ### ",
    "#   #",
    "#####",
    "#   #",
    "#   #"
}};

static const struct glyph g_B = {5, {
    "#### ",
    "#   #",
    "#### ",
    "#   #",
    "#### "
}};

static const struct glyph g_C = {5, {
    " ### ",
    "#   #",
    "#    ",
    "#   #",
    " ### "
}};

static const struct glyph g_D = {5, {
    "#### ",
    "#   #",
    "#   #",
    "#   #",
    "#### "
}};

static const struct glyph g_E = {5, {
    "#####",
    "#    ",
    "#### ",
    "#    ",
    "#####"
}};

static const struct glyph g_F = {5, {
    "#####",
    "#    ",
    "#### ",
    "#    ",
    "#    "
}};

static const struct glyph g_G = {5, {
    " ### ",
    "#    ",
    "# ###",
    "#   #",
    " ### "
}};

static const struct glyph g_H = {5, {
    "#   #",
    "#   #",
    "#####",
    "#   #",
    "#   #"
}};

static const struct glyph g_I = {3, {
    "###",
    " # ",
    " # ",
    " # ",
    "###"
}};

static const struct glyph g_J = {5, {
    "  ###",
    "    #",
    "    #",
    "#   #",
    " ### "
}};

static const struct glyph g_K = {5, {
    "#   #",
    "#  # ",
    "###  ",
    "#  # ",
    "#   #"
}};

static const struct glyph g_L = {5, {
    "#    ",
    "#    ",
    "#    ",
    "#    ",
    "#####"
}};

static const struct glyph g_M = {6, {
    "#    #",
    "##  ##",
    "# ## #",
    "#    #",
    "#    #"
}};

static const struct glyph g_N = {5, {
    "#   #",
    "##  #",
    "# # #",
    "#  ##",
    "#   #"
}};

static const struct glyph g_O = {5, {
    " ### ",
    "#   #",
    "#   #",
    "#   #",
    " ### "
}};

static const struct glyph g_P = {5, {
    "#### ",
    "#   #",
    "#### ",
    "#    ",
    "#    "
}};

static const struct glyph g_Q = {5, {
    " ### ",
    "#   #",
    "# # #",
    "#  # ",
    " ## #"
}};

static const struct glyph g_R = {5, {
    "#### ",
    "#   #",
    "#### ",
    "#  # ",
    "#   #"
}};

static const struct glyph g_S = {5, {
    " ####",
    "#    ",
    " ### ",
    "    #",
    "#### "
}};

static const struct glyph g_T = {5, {
    "#####",
    "  #  ",
    "  #  ",
    "  #  ",
    "  #  "
}};

static const struct glyph g_U = {5, {
    "#   #",
    "#   #",
    "#   #",
    "#   #",
    " ### "
}};

static const struct glyph g_V = {5, {
    "#   #",
    "#   #",
    "#   #",
    " # # ",
    "  #  "
}};

static const struct glyph g_W = {6, {
    "#    #",
    "#    #",
    "# ## #",
    "##  ##",
    "#    #"
}};

static const struct glyph g_X = {5, {
    "#   #",
    " # # ",
    "  #  ",
    " # # ",
    "#   #"
}};

static const struct glyph g_Y = {5, {
    "#   #",
    " # # ",
    "  #  ",
    "  #  ",
    "  #  "
}};

static const struct glyph g_Z = {5, {
    "#####",
    "   # ",
    "  #  ",
    " #   ",
    "#####"
}};

static const struct glyph g_0 = {5, {
    " ### ",
    "#  ##",
    "# # #",
    "##  #",
    " ### "
}};

static const struct glyph g_1 = {3, {
    " # ",
    "## ",
    " # ",
    " # ",
    "###"
}};

static const struct glyph g_2 = {5, {
    " ### ",
    "#   #",
    "  ## ",
    " #   ",
    "#####"
}};

static const struct glyph g_3 = {5, {
    " ### ",
    "#   #",
    "  ## ",
    "#   #",
    " ### "
}};

static const struct glyph g_4 = {5, {
    "#  # ",
    "#  # ",
    "#####",
    "   # ",
    "   # "
}};

static const struct glyph g_5 = {5, {
    "#####",
    "#    ",
    "#### ",
    "    #",
    "#### "
}};

static const struct glyph g_6 = {5, {
    " ### ",
    "#    ",
    "#### ",
    "#   #",
    " ### "
}};

static const struct glyph g_7 = {5, {
    "#####",
    "   # ",
    "  #  ",
    " #   ",
    "#    "
}};

static const struct glyph g_8 = {5, {
    " ### ",
    "#   #",
    " ### ",
    "#   #",
    " ### "
}};

static const struct glyph g_9 = {5, {
    " ### ",
    "#   #",
    " ####",
    "    #",
    " ### "
}};

static const struct glyph g_dot = {2, {
    "  ",
    "  ",
    "  ",
    "  ",
    "##"
}};

static const struct glyph g_comma = {2, {
    "  ",
    "  ",
    "  ",
    " #",
    "# "
}};

static const struct glyph g_exclam = {2, {
    "##",
    "##",
    "##",
    "  ",
    "##"
}};

static const struct glyph g_question = {5, {
    " ### ",
    "#   #",
    "  ## ",
    "     ",
    "  #  "
}};

static const struct glyph g_apos = {2, {
    "##",
    "# ",
    "  ",
    "  ",
    "  "
}};

static const struct glyph g_dquote = {5, {
    "## ##",
    "## ##",
    "     ",
    "     ",
    "     "
}};

static const struct glyph g_dash = {4, {
    "    ",
    "    ",
    "####",
    "    ",
    "    "
}};

static const struct glyph g_colon = {2, {
    "  ",
    "##",
    "  ",
    "##",
    "  "
}};

static const struct glyph g_semicolon = {2, {
    "  ",
    "##",
    "  ",
    " #",
    "# "
}};

static const struct glyph g_lparen = {3, {
    " ##",
    "#  ",
    "#  ",
    "#  ",
    " ##"
}};

static const struct glyph g_rparen = {3, {
    "## ",
    "  #",
    "  #",
    "  #",
    "## "
}};

static const struct glyph g_slash = {5, {
    "    #",
    "   # ",
    "  #  ",
    " #   ",
    "#    "
}};

static const struct glyph g_space = {3, {
    "   ",
    "   ",
    "   ",
    "   ",
    "   "
}};

static const struct glyph *lookup(char c)
{
    /* Uppercase and lowercase map to the same glyphs */
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    switch (c) {
    case 'A': return &g_A;  case 'B': return &g_B;
    case 'C': return &g_C;  case 'D': return &g_D;
    case 'E': return &g_E;  case 'F': return &g_F;
    case 'G': return &g_G;  case 'H': return &g_H;
    case 'I': return &g_I;  case 'J': return &g_J;
    case 'K': return &g_K;  case 'L': return &g_L;
    case 'M': return &g_M;  case 'N': return &g_N;
    case 'O': return &g_O;  case 'P': return &g_P;
    case 'Q': return &g_Q;  case 'R': return &g_R;
    case 'S': return &g_S;  case 'T': return &g_T;
    case 'U': return &g_U;  case 'V': return &g_V;
    case 'W': return &g_W;  case 'X': return &g_X;
    case 'Y': return &g_Y;  case 'Z': return &g_Z;
    case '0': return &g_0;  case '1': return &g_1;
    case '2': return &g_2;  case '3': return &g_3;
    case '4': return &g_4;  case '5': return &g_5;
    case '6': return &g_6;  case '7': return &g_7;
    case '8': return &g_8;  case '9': return &g_9;
    case '.': return &g_dot;
    case ',': return &g_comma;
    case '!': return &g_exclam;
    case '?': return &g_question;
    case '\'': return &g_apos;
    case '"': return &g_dquote;
    case '-': return &g_dash;
    case ':': return &g_colon;
    case ';': return &g_semicolon;
    case '(': return &g_lparen;
    case ')': return &g_rparen;
    case '/': return &g_slash;
    case ' ': return &g_space;
    default:  return &g_space;  /* unknown -> blank */
    }
}

/* Map a UTF-8 character to an ASCII char for glyph lookup.
   Handles curly quotes, em/en dashes, and other common Unicode.
   Returns 0 to skip the character (unknown multi-byte). */
static char utf8_to_ascii(const char *p, int *advance)
{
    unsigned char c = (unsigned char)*p;

    /* ASCII: single byte */
    if (c < 0x80) {
        *advance = 1;
        return (char)c;
    }

    /* 3-byte UTF-8 sequences (0xE0..0xEF) */
    if (c >= 0xE0 && c < 0xF0 && p[1] && p[2]) {
        unsigned char b1 = (unsigned char)p[1];
        unsigned char b2 = (unsigned char)p[2];
        *advance = 3;

        /* U+2018 ' and U+2019 ' (curly single quotes / apostrophe) */
        if (c == 0xE2 && b1 == 0x80 && (b2 == 0x98 || b2 == 0x99))
            return '\'';
        /* U+201C " and U+201D " (curly double quotes) */
        if (c == 0xE2 && b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D))
            return '"';
        /* U+2013 – (en dash) and U+2014 — (em dash) */
        if (c == 0xE2 && b1 == 0x80 && (b2 == 0x93 || b2 == 0x94))
            return '-';
        /* U+2026 … (ellipsis) */
        if (c == 0xE2 && b1 == 0x80 && b2 == 0xA6)
            return '.';

        return 0;  /* unknown 3-byte char */
    }

    /* 2-byte UTF-8 sequences (0xC0..0xDF) */
    if (c >= 0xC0 && c < 0xE0 && p[1]) {
        *advance = 2;
        return 0;  /* unknown 2-byte char */
    }

    /* 4-byte UTF-8 sequences (0xF0..0xF7) */
    if (c >= 0xF0 && c < 0xF8 && p[1] && p[2] && p[3]) {
        *advance = 4;
        return 0;
    }

    /* Continuation byte or invalid — skip */
    *advance = 1;
    return 0;
}

int bigfont_render(const char *word, char rows_out[BIGFONT_ROWS][512])
{
    /* UTF-8 block character */
    static const char block[] = "\xe2\x96\x88";

    for (int r = 0; r < BIGFONT_ROWS; r++)
        rows_out[r][0] = '\0';

    int first = 1;
    const char *p = word;
    while (*p) {
        int advance = 1;
        char ch = utf8_to_ascii(p, &advance);
        p += advance;

        if (ch == 0) continue;  /* skip unknown multi-byte chars */

        const struct glyph *g = lookup(ch);

        for (int r = 0; r < BIGFONT_ROWS; r++) {
            char *out = rows_out[r] + strlen(rows_out[r]);

            /* 1-space gap between characters */
            if (!first) {
                *out++ = ' ';
            }

            const char *src = g->rows[r];
            for (int c = 0; c < g->width; c++) {
                if (src[c] == '#') {
                    out[0] = block[0];
                    out[1] = block[1];
                    out[2] = block[2];
                    out += 3;
                } else {
                    *out++ = ' ';
                }
            }
            *out = '\0';
        }
        first = 0;
    }

    return BIGFONT_ROWS;
}
