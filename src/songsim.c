#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- CRC-32 ---- */

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void build_crc_table(void) {
    for (int n = 0; n < 256; n++) {
        uint32_t c = (uint32_t)n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

static uint32_t crc32_buf(const uint8_t *data, size_t len) {
    if (!crc_table_ready) build_crc_table();
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

/* ---- Adler-32 ---- */

static uint32_t adler32_buf(const uint8_t *data, size_t len) {
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < len; i++) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1)      % 65521u;
    }
    return (s2 << 16) | s1;
}

/* ---- Growable byte buffer ---- */

typedef struct { uint8_t *data; size_t len, cap; } Buf;

static int buf_reserve(Buf *b, size_t extra) {
    size_t need = b->len + extra;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    uint8_t *p = realloc(b->data, cap);
    if (!p) return 0;
    b->data = p;
    b->cap  = cap;
    return 1;
}

static void buf_u8(Buf *b, uint8_t v) {
    buf_reserve(b, 1);
    b->data[b->len++] = v;
}

static void buf_bytes(Buf *b, const uint8_t *src, size_t n) {
    if (!n) return;
    buf_reserve(b, n);
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_u32be(Buf *b, uint32_t v) {
    buf_u8(b, (v >> 24) & 0xff);
    buf_u8(b, (v >> 16) & 0xff);
    buf_u8(b, (v >>  8) & 0xff);
    buf_u8(b,  v        & 0xff);
}

/* ---- PNG helpers ---- */

/* Write a PNG chunk: [4-len][4-type][data][4-crc] */
static void png_chunk(Buf *out, const char type[4], const uint8_t *data, uint32_t len) {
    buf_u32be(out, len);
    size_t crc_start = out->len;
    buf_bytes(out, (const uint8_t *)type, 4);
    buf_bytes(out, data, len);
    uint32_t crc = crc32_buf(out->data + crc_start, 4 + len);
    buf_u32be(out, crc);
}

/*
 * Encode RGBA pixels as an uncompressed PNG.
 *
 * Uncompressed PNG uses zlib "store" blocks (BTYPE=00) so the IDAT payload
 * is just: [zlib-hdr] [deflate blocks] [adler32].  Each deflate block:
 *   1 byte  BFINAL|BTYPE  (0x01 for last block, else 0x00)
 *   2 bytes LEN  (little-endian, ≤ 65535)
 *   2 bytes NLEN (bitwise complement of LEN, little-endian)
 *   LEN bytes of literal data
 *
 * Image data = per-scanline: 0x00 (filter None) | row of RGBA bytes.
 */
static uint8_t *encode_png_rgba(const uint8_t *rgba, uint32_t w, uint32_t h,
                                size_t *out_len) {
    /* Build raw scanline data */
    size_t row  = 1 + (size_t)w * 4;   /* filter byte + RGBA pixels */
    size_t rlen = row * h;

    uint8_t *raw = malloc(rlen);
    if (!raw) return NULL;
    for (uint32_t y = 0; y < h; y++) {
        raw[y * row] = 0x00;            /* filter: None */
        memcpy(raw + y * row + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }

    /* Build zlib/deflate payload in a temporary buffer */
    const size_t BMAX = 65535;
    size_t nblocks = (rlen + BMAX - 1) / BMAX;
    /* 2 (zlib hdr) + nblocks*(5 hdr + up to BMAX data) + 4 (adler32) */
    size_t idat_cap = 2 + nblocks * (5 + BMAX) + 4;
    uint8_t *idat = malloc(idat_cap);
    if (!idat) { free(raw); return NULL; }
    size_t ip = 0;

    /* zlib header: CMF=0x78 CINFO=7, FLG=0x01 (no dict; 0x7801 % 31 == 0) */
    idat[ip++] = 0x78;
    idat[ip++] = 0x01;

    for (size_t b = 0; b < nblocks; b++) {
        size_t   start = b * BMAX;
        size_t   end   = start + BMAX < rlen ? start + BMAX : rlen;
        uint16_t len   = (uint16_t)(end - start);
        uint16_t nlen  = ~len;
        idat[ip++] = (b == nblocks - 1) ? 0x01 : 0x00; /* BFINAL | BTYPE=00 */
        idat[ip++] =  len        & 0xff;
        idat[ip++] = (len >> 8)  & 0xff;
        idat[ip++] =  nlen       & 0xff;
        idat[ip++] = (nlen >> 8) & 0xff;
        memcpy(idat + ip, raw + start, len);
        ip += len;
    }

    /* Adler-32 of raw scanline data, big-endian */
    uint32_t adler = adler32_buf(raw, rlen);
    idat[ip++] = (adler >> 24) & 0xff;
    idat[ip++] = (adler >> 16) & 0xff;
    idat[ip++] = (adler >>  8) & 0xff;
    idat[ip++] =  adler        & 0xff;

    free(raw);

    /* Assemble PNG */
    Buf out = {0};
    /* Pre-allocate: 8 sig + 25 IHDR + (12+ip) IDAT + 12 IEND */
    buf_reserve(&out, 8 + 25 + 12 + ip + 12);

    static const uint8_t sig[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};
    buf_bytes(&out, sig, 8);

    uint8_t ihdr[13];
    ihdr[0] = (w >> 24) & 0xff; ihdr[1] = (w >> 16) & 0xff;
    ihdr[2] = (w >>  8) & 0xff; ihdr[3] =  w        & 0xff;
    ihdr[4] = (h >> 24) & 0xff; ihdr[5] = (h >> 16) & 0xff;
    ihdr[6] = (h >>  8) & 0xff; ihdr[7] =  h        & 0xff;
    ihdr[8]  = 8;   /* bit depth */
    ihdr[9]  = 6;   /* colour type: RGBA */
    ihdr[10] = 0;   /* compression: deflate */
    ihdr[11] = 0;   /* filter method */
    ihdr[12] = 0;   /* interlace: none */
    png_chunk(&out, "IHDR", ihdr, 13);

    png_chunk(&out, "IDAT", idat, (uint32_t)ip);
    free(idat);

    png_chunk(&out, "IEND", NULL, 0);

    *out_len = out.len;
    return out.data;
}

/* ---- Word → position map (chained hash table) ---- */

#define BUCKETS 1024

typedef struct Entry { char *word; size_t *pos; size_t plen, pcap; struct Entry *next; } Entry;

static uint32_t hash_word(const char *s) {
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h;
}

static Entry *map_get(Entry **buckets, const char *word) {
    uint32_t i = hash_word(word) % BUCKETS;
    for (Entry *e = buckets[i]; e; e = e->next)
        if (strcmp(e->word, word) == 0) return e;
    Entry *e = calloc(1, sizeof *e);
    e->word = strdup(word);
    e->next = buckets[i];
    buckets[i] = e;
    return e;
}

static void entry_push(Entry *e, size_t p) {
    if (e->plen == e->pcap) {
        e->pcap = e->pcap ? e->pcap * 2 : 4;
        e->pos  = realloc(e->pos, e->pcap * sizeof *e->pos);
    }
    e->pos[e->plen++] = p;
}

static void map_free(Entry **buckets) {
    for (int i = 0; i < BUCKETS; i++)
        for (Entry *e = buckets[i]; e; ) {
            Entry *nx = e->next;
            free(e->word); free(e->pos); free(e);
            e = nx;
        }
}

/* ---- Tokenizer ---- */

static const char *next_word(const char **p, size_t *wlen) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
    if (!**p) return NULL;
    const char *start = *p;
    while (**p && **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n') (*p)++;
    *wlen = (size_t)(*p - start);
    return start;
}

/* ---- Public API ---- */

uint8_t *songsim_c(const char *text, size_t *out_len) {
    if (!text) return NULL;

    Entry *buckets[BUCKETS] = {0};
    size_t word_count = 0;

    const char *p = text;
    size_t wlen;
    const char *tok;
    while ((tok = next_word(&p, &wlen)) != NULL) {
        /* copy word to a null-terminated temp buffer */
        char tmp[4096];
        if (wlen >= sizeof tmp) wlen = sizeof tmp - 1;
        memcpy(tmp, tok, wlen);
        tmp[wlen] = '\0';

        entry_push(map_get(buckets, tmp), word_count++);
    }

    if (word_count == 0) { map_free(buckets); return NULL; }

    uint32_t n = (uint32_t)word_count;
    uint8_t *px = malloc((size_t)n * n * 4);
    if (!px) { map_free(buckets); return NULL; }
    memset(px, 0xff, (size_t)n * n * 4);   /* white fill */

    for (int i = 0; i < BUCKETS; i++) {
        for (Entry *e = buckets[i]; e; e = e->next) {
            for (size_t a = 0; a < e->plen; a++) {
                /* diagonal */
                size_t idx = (e->pos[a] * n + e->pos[a]) * 4;
                px[idx] = 0x55; px[idx+1] = 0x55; px[idx+2] = 0x55; px[idx+3] = 0xff;
                for (size_t b = a + 1; b < e->plen; b++) {
                    size_t x = e->pos[a], y = e->pos[b];
                    idx = (y * n + x) * 4;
                    px[idx] = 0x55; px[idx+1] = 0x55; px[idx+2] = 0x55; px[idx+3] = 0xff;
                    idx = (x * n + y) * 4;
                    px[idx] = 0x55; px[idx+1] = 0x55; px[idx+2] = 0x55; px[idx+3] = 0xff;
                }
            }
        }
    }

    uint8_t *png = encode_png_rgba(px, n, n, out_len);
    free(px);
    map_free(buckets);
    return png;
}

void songsim_c_free(uint8_t *ptr, size_t len) {
    (void)len;
    free(ptr);
}
