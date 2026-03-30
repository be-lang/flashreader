#include "inflate.h"
#include <string.h>

/* ---- bit reader ---- */

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         byte_pos;
    int            bit_pos;   /* 0..7 within current byte */
} bitreader_t;

static void br_init(bitreader_t *br, const uint8_t *data, size_t len)
{
    br->data     = data;
    br->len      = len;
    br->byte_pos = 0;
    br->bit_pos  = 0;
}

/* Read up to 25 bits, LSB-first.  Returns -1 on overread. */
static int br_bits(bitreader_t *br, int n)
{
    int val = 0;
    for (int i = 0; i < n; i++) {
        if (br->byte_pos >= br->len) return -1;
        val |= ((br->data[br->byte_pos] >> br->bit_pos) & 1) << i;
        br->bit_pos++;
        if (br->bit_pos == 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return val;
}

/* Align to next byte boundary. */
static void br_align(bitreader_t *br)
{
    if (br->bit_pos > 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

/* ---- Huffman table ---- */

#define HUFF_MAX_BITS  15
#define HUFF_MAX_SYM  288

typedef struct {
    uint16_t counts[HUFF_MAX_BITS + 1];   /* number of codes of each length */
    uint16_t symbols[HUFF_MAX_SYM];       /* symbols sorted by code */
} hufftab_t;

/* Build canonical-Huffman decode table from an array of code lengths.
   Returns 0 on success, -1 on error. */
static int huff_build(hufftab_t *h, const uint8_t *lengths, int n)
{
    memset(h->counts, 0, sizeof(h->counts));
    for (int i = 0; i < n; i++) {
        if (lengths[i] > HUFF_MAX_BITS) return -1;
        h->counts[lengths[i]]++;
    }

    /* Compute offsets (first index in symbols[] for each bit length).
       Skip length 0 — those symbols are unused and don't go in the table.
       huff_decode starts at len=1, so symbols must be packed starting there. */
    uint16_t offsets[HUFF_MAX_BITS + 1];
    offsets[0] = 0;
    offsets[1] = 0;
    for (int i = 2; i <= HUFF_MAX_BITS; i++)
        offsets[i] = offsets[i - 1] + h->counts[i - 1];

    for (int i = 0; i < n; i++) {
        if (lengths[i] > 0)
            h->symbols[offsets[lengths[i]]++] = (uint16_t)i;
    }
    return 0;
}

/* Decode one symbol.  Returns symbol value or -1 on error. */
static int huff_decode(bitreader_t *br, const hufftab_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= HUFF_MAX_BITS; len++) {
        int bit = br_bits(br, 1);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        int count = h->counts[len];
        if (code - first < count)
            return h->symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
    }
    return -1;  /* invalid code */
}

/* ---- RFC 1951 tables ---- */

/* Length codes 257..285 */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
    35,43,51,59, 67,83,99,115, 131,163,195,227, 258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
    3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};

/* Distance codes 0..29 */
static const uint16_t dist_base[30] = {
    1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193,
    257,385,513,769, 1025,1537,2049,3073, 4097,6145,8193,12289,
    16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6,
    7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
};

/* ---- fixed Huffman tables (RFC 1951 section 3.2.6) ---- */

static int fixed_tables_built = 0;
static hufftab_t fixed_lit;
static hufftab_t fixed_dist;

static void build_fixed_tables(void)
{
    if (fixed_tables_built) return;
    uint8_t lengths[288];
    int i;
    for (i =   0; i <= 143; i++) lengths[i] = 8;
    for (     ; i <= 255; i++) lengths[i] = 9;
    for (     ; i <= 279; i++) lengths[i] = 7;
    for (     ; i <= 287; i++) lengths[i] = 8;
    huff_build(&fixed_lit, lengths, 288);

    uint8_t dlens[32];
    for (i = 0; i < 32; i++) dlens[i] = 5;
    huff_build(&fixed_dist, dlens, 32);
    fixed_tables_built = 1;
}

/* ---- inflate engine ---- */

/* Decode a block compressed with the given literal/length and distance
   Huffman tables.  Returns 0 on success, -1 on error. */
static int inflate_codes(bitreader_t *br, const hufftab_t *hlit,
                         const hufftab_t *hdist,
                         uint8_t *out, size_t out_max, size_t *pos)
{
    for (;;) {
        int sym = huff_decode(br, hlit);
        if (sym < 0) return -1;

        if (sym < 256) {
            /* literal byte */
            if (*pos >= out_max) return -1;
            out[(*pos)++] = (uint8_t)sym;
        } else if (sym == 256) {
            /* end of block */
            return 0;
        } else {
            /* length/distance pair */
            int li = sym - 257;
            if (li < 0 || li > 28) return -1;
            int extra = br_bits(br, len_extra[li]);
            if (extra < 0 && len_extra[li] > 0) return -1;
            if (len_extra[li] == 0) extra = 0;
            int length = len_base[li] + extra;

            int dsym = huff_decode(br, hdist);
            if (dsym < 0 || dsym > 29) return -1;
            extra = br_bits(br, dist_extra[dsym]);
            if (extra < 0 && dist_extra[dsym] > 0) return -1;
            if (dist_extra[dsym] == 0) extra = 0;
            int distance = dist_base[dsym] + extra;

            if ((size_t)distance > *pos) return -1;
            for (int j = 0; j < length; j++) {
                if (*pos >= out_max) return -1;
                out[*pos] = out[*pos - (size_t)distance];
                (*pos)++;
            }
        }
    }
}

/* Decode dynamic Huffman tables (BTYPE=10). */
static int decode_dynamic(bitreader_t *br, hufftab_t *hlit_out,
                          hufftab_t *hdist_out)
{
    int hlit  = br_bits(br, 5); if (hlit  < 0) return -1; hlit  += 257;
    int hdist = br_bits(br, 5); if (hdist < 0) return -1; hdist += 1;
    int hclen = br_bits(br, 4); if (hclen < 0) return -1; hclen += 4;

    static const int clen_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    uint8_t clen_lengths[19];
    memset(clen_lengths, 0, sizeof(clen_lengths));
    for (int i = 0; i < hclen; i++) {
        int v = br_bits(br, 3);
        if (v < 0) return -1;
        clen_lengths[clen_order[i]] = (uint8_t)v;
    }

    hufftab_t clen_tab;
    if (huff_build(&clen_tab, clen_lengths, 19) < 0) return -1;

    int total = hlit + hdist;
    uint8_t lengths[288 + 32];
    memset(lengths, 0, sizeof(lengths));
    int idx = 0;
    while (idx < total) {
        int sym = huff_decode(br, &clen_tab);
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            int rep = br_bits(br, 2);
            if (rep < 0) return -1;
            rep += 3;
            if (idx == 0) return -1;
            uint8_t prev = lengths[idx - 1];
            for (int i = 0; i < rep; i++) {
                if (idx >= total) return -1;
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int rep = br_bits(br, 3);
            if (rep < 0) return -1;
            rep += 3;
            for (int i = 0; i < rep; i++) {
                if (idx >= total) return -1;
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int rep = br_bits(br, 7);
            if (rep < 0) return -1;
            rep += 11;
            for (int i = 0; i < rep; i++) {
                if (idx >= total) return -1;
                lengths[idx++] = 0;
            }
        } else {
            return -1;
        }
    }

    if (huff_build(hlit_out, lengths, hlit) < 0) return -1;
    if (huff_build(hdist_out, lengths + hlit, hdist) < 0) return -1;
    return 0;
}

int inflate_raw(const uint8_t *in, size_t in_len,
                uint8_t *out, size_t out_max)
{
    bitreader_t br;
    br_init(&br, in, in_len);
    size_t pos = 0;
    int bfinal;

    do {
        int bf = br_bits(&br, 1);
        if (bf < 0) return -1;
        bfinal = bf;

        int btype = br_bits(&br, 2);
        if (btype < 0) return -1;

        if (btype == 0) {
            /* stored (no compression) */
            br_align(&br);
            if (br.byte_pos + 4 > br.len) return -1;
            uint16_t len  = (uint16_t)(br.data[br.byte_pos] |
                            (br.data[br.byte_pos + 1] << 8));
            uint16_t nlen = (uint16_t)(br.data[br.byte_pos + 2] |
                            (br.data[br.byte_pos + 3] << 8));
            br.byte_pos += 4;
            if ((uint16_t)(len ^ 0xFFFF) != nlen) return -1;
            if (br.byte_pos + len > br.len) return -1;
            if (pos + len > out_max) return -1;
            memcpy(out + pos, br.data + br.byte_pos, len);
            br.byte_pos += len;
            pos += len;
        } else if (btype == 1) {
            /* fixed Huffman */
            build_fixed_tables();
            if (inflate_codes(&br, &fixed_lit, &fixed_dist,
                              out, out_max, &pos) < 0)
                return -1;
        } else if (btype == 2) {
            /* dynamic Huffman */
            hufftab_t dyn_lit, dyn_dist;
            if (decode_dynamic(&br, &dyn_lit, &dyn_dist) < 0) return -1;
            if (inflate_codes(&br, &dyn_lit, &dyn_dist,
                              out, out_max, &pos) < 0)
                return -1;
        } else {
            return -1;  /* reserved */
        }
    } while (!bfinal);

    return (int)pos;
}
