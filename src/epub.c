#define _POSIX_C_SOURCE 200809L
#include "epub.h"
#include "inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* ================================================================
   A) Minimal ZIP reader
   ================================================================ */

typedef struct {
    char    *name;
    uint16_t method;       /* 0=stored, 8=deflated */
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;
} ZipEntry;

typedef struct {
    FILE     *fp;
    ZipEntry *entries;
    int       entry_count;
} ZipArchive;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static ZipArchive *zip_open(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    /* Find EOCD signature scanning backwards */
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long file_size = ftell(fp);
    if (file_size < 22) { fclose(fp); return NULL; }

    long scan_start = file_size - 65557;
    if (scan_start < 0) scan_start = 0;

    long eocd_offset = -1;
    if (fseek(fp, scan_start, SEEK_SET) != 0) { fclose(fp); return NULL; }

    size_t scan_len = (size_t)(file_size - scan_start);
    uint8_t *scan_buf = malloc(scan_len);
    if (!scan_buf) { fclose(fp); return NULL; }
    if (fread(scan_buf, 1, scan_len, fp) != scan_len) {
        free(scan_buf);
        fclose(fp);
        return NULL;
    }

    for (long i = (long)scan_len - 22; i >= 0; i--) {
        if (scan_buf[i] == 0x50 && scan_buf[i+1] == 0x4b &&
            scan_buf[i+2] == 0x05 && scan_buf[i+3] == 0x06) {
            eocd_offset = scan_start + i;
            break;
        }
    }
    free(scan_buf);

    if (eocd_offset < 0) { fclose(fp); return NULL; }

    /* Read EOCD */
    uint8_t eocd[22];
    if (fseek(fp, eocd_offset, SEEK_SET) != 0) { fclose(fp); return NULL; }
    if (fread(eocd, 1, 22, fp) != 22) { fclose(fp); return NULL; }

    int entry_count = read16(eocd + 10);
    uint32_t cd_offset = read32(eocd + 16);

    /* Read central directory */
    if (fseek(fp, (long)cd_offset, SEEK_SET) != 0) { fclose(fp); return NULL; }

    ZipEntry *entries = calloc((size_t)entry_count, sizeof(ZipEntry));
    if (!entries) { fclose(fp); return NULL; }

    for (int i = 0; i < entry_count; i++) {
        uint8_t cdr[46];
        if (fread(cdr, 1, 46, fp) != 46) goto fail;
        if (read32(cdr) != 0x02014b50) goto fail;

        uint16_t name_len  = read16(cdr + 28);
        uint16_t extra_len = read16(cdr + 30);
        uint16_t comment_len = read16(cdr + 32);

        entries[i].method      = read16(cdr + 10);
        entries[i].comp_size   = read32(cdr + 20);
        entries[i].uncomp_size = read32(cdr + 24);
        entries[i].local_offset = read32(cdr + 42);

        entries[i].name = malloc((size_t)name_len + 1);
        if (!entries[i].name) goto fail;
        if (fread(entries[i].name, 1, name_len, fp) != name_len) goto fail;
        entries[i].name[name_len] = '\0';

        /* Skip extra + comment */
        if (fseek(fp, (long)(extra_len + comment_len), SEEK_CUR) != 0)
            goto fail;
    }

    ZipArchive *archive = malloc(sizeof(ZipArchive));
    if (!archive) goto fail;
    archive->fp = fp;
    archive->entries = entries;
    archive->entry_count = entry_count;
    return archive;

fail:
    for (int j = 0; j < entry_count; j++)
        free(entries[j].name);
    free(entries);
    fclose(fp);
    return NULL;
}

static int zip_extract(ZipArchive *archive, const char *entry_name,
                       uint8_t **out_buf, size_t *out_len)
{
    ZipEntry *entry = NULL;
    for (int i = 0; i < archive->entry_count; i++) {
        if (strcmp(archive->entries[i].name, entry_name) == 0) {
            entry = &archive->entries[i];
            break;
        }
    }
    if (!entry) return -1;

    /* Read local file header */
    if (fseek(archive->fp, (long)entry->local_offset, SEEK_SET) != 0)
        return -1;
    uint8_t lh[30];
    if (fread(lh, 1, 30, archive->fp) != 30) return -1;
    if (read32(lh) != 0x04034b50) return -1;

    uint16_t name_len  = read16(lh + 26);
    uint16_t extra_len = read16(lh + 28);
    if (fseek(archive->fp, (long)(name_len + extra_len), SEEK_CUR) != 0)
        return -1;

    if (entry->method == 0) {
        /* Stored */
        uint8_t *buf = malloc(entry->uncomp_size + 1);
        if (!buf) return -1;
        if (entry->uncomp_size > 0 &&
            fread(buf, 1, entry->uncomp_size, archive->fp) != entry->uncomp_size) {
            free(buf);
            return -1;
        }
        buf[entry->uncomp_size] = '\0';
        *out_buf = buf;
        *out_len = entry->uncomp_size;
        return 0;
    } else if (entry->method == 8) {
        /* Deflated */
        uint8_t *comp = malloc(entry->comp_size);
        if (!comp) return -1;
        if (fread(comp, 1, entry->comp_size, archive->fp) != entry->comp_size) {
            free(comp);
            return -1;
        }

        uint8_t *uncomp = malloc(entry->uncomp_size + 1);
        if (!uncomp) { free(comp); return -1; }

        int result = inflate_raw(comp, entry->comp_size,
                                 uncomp, entry->uncomp_size);
        free(comp);
        if (result < 0) { free(uncomp); return -1; }

        uncomp[entry->uncomp_size] = '\0';
        *out_buf = uncomp;
        *out_len = (size_t)result;
        return 0;
    }
    return -1;  /* unsupported method */
}

static void zip_close(ZipArchive *archive)
{
    if (!archive) return;
    for (int i = 0; i < archive->entry_count; i++)
        free(archive->entries[i].name);
    free(archive->entries);
    fclose(archive->fp);
    free(archive);
}

/* ================================================================
   B) OPF / container.xml parsing (simple string scanning)
   ================================================================ */

/* Find attribute value after a given attribute name in XML string.
   Writes into buf (up to buf_size-1 chars). Returns 0 on success. */
static int xml_attr(const char *xml, const char *attr_name,
                    char *buf, size_t buf_size)
{
    const char *p = xml;
    size_t alen = strlen(attr_name);
    while ((p = strstr(p, attr_name)) != NULL) {
        p += alen;
        /* skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        char quote = *p;
        if (quote != '"' && quote != '\'') continue;
        p++;
        const char *end = strchr(p, quote);
        if (!end) return -1;
        size_t len = (size_t)(end - p);
        if (len >= buf_size) len = buf_size - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
        return 0;
    }
    return -1;
}

/* Match an XML element name, ignoring namespace prefix.
   e.g., matches "rootfile" in "ns:rootfile" or "rootfile". */
static const char *find_element(const char *xml, const char *local_name)
{
    const char *p = xml;
    size_t nlen = strlen(local_name);
    while (*p) {
        const char *lt = strchr(p, '<');
        if (!lt) break;
        lt++;
        /* Skip '/' for closing tags */
        const char *tag_start = lt;
        if (*tag_start == '/') tag_start++;
        /* Skip namespace prefix */
        const char *colon = NULL;
        const char *scan = tag_start;
        while (*scan && *scan != '>' && *scan != ' ' && *scan != '\t' &&
               *scan != '\n' && *scan != '\r' && *scan != '/') {
            if (*scan == ':') colon = scan;
            scan++;
        }
        const char *name_start = colon ? colon + 1 : tag_start;
        size_t name_len = (size_t)(scan - name_start);
        if (name_len == nlen && strncmp(name_start, local_name, nlen) == 0) {
            return lt - 1;  /* return pointer to '<' */
        }
        p = scan;
    }
    return NULL;
}

/* Get directory part of a path (result includes trailing '/').
   Returns empty string if no directory. Caller frees. */
static char *path_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t len = (size_t)(slash - path + 1);
    char *dir = malloc(len + 1);
    if (!dir) return NULL;
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

/* Manifest item */
typedef struct {
    char id[256];
    char href[1024];
    int  is_html;
} ManifestItem;

/* ================================================================
   C) HTML stripper
   ================================================================ */

static int ci_prefix(const char *s, const char *prefix, size_t remaining)
{
    size_t plen = strlen(prefix);
    if (remaining < plen) return 0;
    for (size_t i = 0; i < plen; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}

/* Encode a Unicode codepoint as UTF-8 into buf. Returns bytes written (1-4). */
static int utf8_encode(uint32_t cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* Count words in a string (whitespace-delimited) */
static int count_words(const char *s, size_t len)
{
    int count = 0;
    int in_word = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    return count;
}

enum {
    STATE_NORMAL,
    STATE_TAG,
    STATE_SCRIPT,
    STATE_STYLE,
    STATE_COMMENT
};

char *html_strip(const char *html, size_t len,
                 Chapter **chapters_out, int *chapter_count_out,
                 int word_offset)
{
    size_t cap = len + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t out_len = 0;

    int ch_cap = 16;
    int ch_count = 0;
    Chapter *chapters = malloc((size_t)ch_cap * sizeof(Chapter));
    if (!chapters) { free(out); return NULL; }

    int state = STATE_NORMAL;
    int in_heading = 0;
    int heading_level = 0;
    char heading_buf[FR_MAX_CHAPTER_NAME];
    int heading_len = 0;
    size_t heading_start_out_len = 0;

    size_t i = 0;
    while (i < len) {
        if (state == STATE_NORMAL) {
            if (html[i] == '<') {
                size_t remaining = len - i;
                /* Check for comment */
                if (remaining >= 4 && strncmp(html + i, "<!--", 4) == 0) {
                    state = STATE_COMMENT;
                    i += 4;
                    continue;
                }
                /* Check for script */
                if (ci_prefix(html + i + 1, "script", remaining - 1) &&
                    (i + 7 >= len || html[i+7] == ' ' || html[i+7] == '>' ||
                     html[i+7] == '\t' || html[i+7] == '\n' || html[i+7] == '/')) {
                    state = STATE_SCRIPT;
                    i++;
                    continue;
                }
                /* Check for style */
                if (ci_prefix(html + i + 1, "style", remaining - 1) &&
                    (i + 6 >= len || html[i+6] == ' ' || html[i+6] == '>' ||
                     html[i+6] == '\t' || html[i+6] == '\n' || html[i+6] == '/')) {
                    state = STATE_STYLE;
                    i++;
                    continue;
                }
                /* Check for heading */
                if (remaining >= 3 && (html[i+1] == 'h' || html[i+1] == 'H') &&
                    html[i+2] >= '1' && html[i+2] <= '3' &&
                    (i + 3 >= len || html[i+3] == ' ' || html[i+3] == '>' ||
                     html[i+3] == '\t' || html[i+3] == '\n' || html[i+3] == '/')) {
                    in_heading = 1;
                    heading_level = html[i+2] - '0';
                    heading_len = 0;
                    heading_start_out_len = out_len;
                    state = STATE_TAG;
                    i++;
                    continue;
                }
                /* Check for closing heading */
                if (remaining >= 4 && html[i+1] == '/' &&
                    (html[i+2] == 'h' || html[i+2] == 'H') &&
                    html[i+3] >= '1' && html[i+3] <= '3') {
                    if (in_heading && (html[i+3] - '0') == heading_level) {
                        /* End of heading — create chapter */
                        in_heading = 0;
                        if (heading_len > 0) {
                            /* Trim trailing whitespace */
                            while (heading_len > 0 &&
                                   ((unsigned char)heading_buf[heading_len-1] <= ' '))
                                heading_len--;
                            if (heading_len >= FR_MAX_CHAPTER_NAME)
                                heading_len = FR_MAX_CHAPTER_NAME - 1;
                            heading_buf[heading_len] = '\0';

                            /* Count words up to heading start */
                            int wc = count_words(out, heading_start_out_len);

                            if (ch_count >= ch_cap) {
                                ch_cap *= 2;
                                Chapter *tmp = realloc(chapters,
                                    (size_t)ch_cap * sizeof(Chapter));
                                if (!tmp) goto done;
                                chapters = tmp;
                            }
                            memset(&chapters[ch_count], 0, sizeof(Chapter));
                            snprintf(chapters[ch_count].name,
                                     FR_MAX_CHAPTER_NAME, "%s", heading_buf);
                            chapters[ch_count].start_word = word_offset + wc;
                            ch_count++;
                        }
                    }
                    state = STATE_TAG;
                    i++;
                    continue;
                }
                /* Block elements emit space */
                if (ci_prefix(html + i + 1, "br", remaining - 1) ||
                    ci_prefix(html + i + 1, "p", remaining - 1) ||
                    ci_prefix(html + i + 1, "div", remaining - 1) ||
                    ci_prefix(html + i + 1, "/p", remaining - 1) ||
                    ci_prefix(html + i + 1, "/div", remaining - 1) ||
                    ci_prefix(html + i + 1, "li", remaining - 1) ||
                    ci_prefix(html + i + 1, "/li", remaining - 1)) {
                    if (out_len > 0 && out[out_len - 1] != ' ')
                        out[out_len++] = ' ';
                }
                state = STATE_TAG;
                i++;
                continue;
            }

            /* Entity decoding */
            if (html[i] == '&') {
                size_t remaining = len - i;
                if (remaining >= 5 && strncmp(html + i, "&amp;", 5) == 0) {
                    out[out_len++] = '&';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = '&';
                    i += 5; continue;
                }
                if (remaining >= 4 && strncmp(html + i, "&lt;", 4) == 0) {
                    out[out_len++] = '<';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = '<';
                    i += 4; continue;
                }
                if (remaining >= 4 && strncmp(html + i, "&gt;", 4) == 0) {
                    out[out_len++] = '>';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = '>';
                    i += 4; continue;
                }
                if (remaining >= 6 && strncmp(html + i, "&quot;", 6) == 0) {
                    out[out_len++] = '"';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = '"';
                    i += 6; continue;
                }
                if (remaining >= 6 && strncmp(html + i, "&nbsp;", 6) == 0) {
                    out[out_len++] = ' ';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = ' ';
                    i += 6; continue;
                }
                if (remaining >= 4 && strncmp(html + i, "&apos;", 6) == 0) {
                    out[out_len++] = '\'';
                    if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                        heading_buf[heading_len++] = '\'';
                    i += 6; continue;
                }
                /* Numeric entities */
                if (remaining >= 3 && html[i+1] == '#') {
                    uint32_t cp = 0;
                    size_t j = i + 2;
                    if (j < len && (html[j] == 'x' || html[j] == 'X')) {
                        /* Hex */
                        j++;
                        while (j < len && html[j] != ';') {
                            char c = html[j];
                            if (c >= '0' && c <= '9') cp = cp * 16 + (uint32_t)(c - '0');
                            else if (c >= 'a' && c <= 'f') cp = cp * 16 + 10 + (uint32_t)(c - 'a');
                            else if (c >= 'A' && c <= 'F') cp = cp * 16 + 10 + (uint32_t)(c - 'A');
                            else break;
                            j++;
                        }
                    } else {
                        /* Decimal */
                        while (j < len && html[j] != ';') {
                            if (html[j] >= '0' && html[j] <= '9')
                                cp = cp * 10 + (uint32_t)(html[j] - '0');
                            else break;
                            j++;
                        }
                    }
                    if (j < len && html[j] == ';' && cp > 0) {
                        char utf8[4];
                        int nb = utf8_encode(cp, utf8);
                        for (int k = 0; k < nb; k++) {
                            out[out_len++] = utf8[k];
                            if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                                heading_buf[heading_len++] = utf8[k];
                        }
                        i = j + 1;
                        continue;
                    }
                }
                /* Unknown entity — pass through */
                out[out_len++] = html[i];
                if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                    heading_buf[heading_len++] = html[i];
                i++;
                continue;
            }

            /* Normal character */
            out[out_len++] = html[i];
            if (in_heading && heading_len < FR_MAX_CHAPTER_NAME - 1)
                heading_buf[heading_len++] = html[i];
            i++;
        } else if (state == STATE_TAG) {
            if (html[i] == '>')
                state = STATE_NORMAL;
            i++;
        } else if (state == STATE_SCRIPT) {
            /* Look for </script> */
            if (html[i] == '<' && (len - i) >= 9 &&
                ci_prefix(html + i + 1, "/script", len - i - 1) &&
                html[i + 8] == '>') {
                i += 9;
                state = STATE_NORMAL;
            } else {
                i++;
            }
        } else if (state == STATE_STYLE) {
            /* Look for </style> */
            if (html[i] == '<' && (len - i) >= 8 &&
                ci_prefix(html + i + 1, "/style", len - i - 1) &&
                html[i + 7] == '>') {
                i += 8;
                state = STATE_NORMAL;
            } else {
                i++;
            }
        } else if (state == STATE_COMMENT) {
            /* Look for --> */
            if ((len - i) >= 3 && html[i] == '-' &&
                html[i+1] == '-' && html[i+2] == '>') {
                i += 3;
                state = STATE_NORMAL;
            } else {
                i++;
            }
        }
    }

done:
    out[out_len] = '\0';

    if (chapters_out) *chapters_out = chapters;
    else free(chapters);
    if (chapter_count_out) *chapter_count_out = ch_count;

    return out;
}

/* ================================================================
   D) epub_load() orchestration
   ================================================================ */

int epub_load(const char *path, ReaderText *out)
{
    memset(out, 0, sizeof(*out));

    ZipArchive *archive = zip_open(path);
    if (!archive) return -1;

    /* 1. Extract container.xml */
    uint8_t *container_data = NULL;
    size_t container_len = 0;
    if (zip_extract(archive, "META-INF/container.xml",
                    &container_data, &container_len) < 0) {
        zip_close(archive);
        return -1;
    }

    /* Find OPF path */
    const char *rootfile = find_element((char *)container_data, "rootfile");
    if (!rootfile) {
        free(container_data);
        zip_close(archive);
        return -1;
    }
    char opf_path[1024];
    if (xml_attr(rootfile, "full-path", opf_path, sizeof(opf_path)) < 0) {
        free(container_data);
        zip_close(archive);
        return -1;
    }
    free(container_data);

    /* 2. Extract OPF file */
    uint8_t *opf_data = NULL;
    size_t opf_len = 0;
    if (zip_extract(archive, opf_path, &opf_data, &opf_len) < 0) {
        zip_close(archive);
        return -1;
    }

    char *opf_dir = path_dir(opf_path);
    if (!opf_dir) {
        free(opf_data);
        zip_close(archive);
        return -1;
    }

    /* 3. Parse manifest */
    int manifest_cap = 64;
    int manifest_count = 0;
    ManifestItem *manifest = calloc((size_t)manifest_cap, sizeof(ManifestItem));
    if (!manifest) {
        free(opf_dir);
        free(opf_data);
        zip_close(archive);
        return -1;
    }

    {
        const char *p = (char *)opf_data;
        const char *item;
        while ((item = find_element(p, "item")) != NULL) {
            /* Make sure this is an opening tag, not </item> */
            if (item[1] == '/') { p = item + 2; continue; }

            /* Find the end of this tag */
            const char *tag_end = strchr(item, '>');
            if (!tag_end) break;
            size_t tag_len = (size_t)(tag_end - item + 1);

            /* Extract into temporary buffer for attribute parsing */
            char *tag_buf = malloc(tag_len + 1);
            if (!tag_buf) break;
            memcpy(tag_buf, item, tag_len);
            tag_buf[tag_len] = '\0';

            char id[256] = {0};
            char href[1024] = {0};
            char media[256] = {0};
            xml_attr(tag_buf, "id", id, sizeof(id));
            xml_attr(tag_buf, "href", href, sizeof(href));
            xml_attr(tag_buf, "media-type", media, sizeof(media));
            free(tag_buf);

            if (id[0] && href[0] &&
                (strstr(media, "html") || strstr(media, "xhtml"))) {
                if (manifest_count >= manifest_cap) {
                    manifest_cap *= 2;
                    ManifestItem *tmp = realloc(manifest,
                        (size_t)manifest_cap * sizeof(ManifestItem));
                    if (!tmp) break;
                    manifest = tmp;
                }
                snprintf(manifest[manifest_count].id, sizeof(manifest[0].id),
                         "%s", id);
                snprintf(manifest[manifest_count].href, sizeof(manifest[0].href),
                         "%s", href);
                manifest[manifest_count].is_html = 1;
                manifest_count++;
            }
            p = tag_end + 1;
        }
    }

    /* 4. Parse spine */
    int spine_cap = 64;
    int spine_count = 0;
    char (*spine_idrefs)[256] = calloc((size_t)spine_cap, 256);
    if (!spine_idrefs) {
        free(manifest);
        free(opf_dir);
        free(opf_data);
        zip_close(archive);
        return -1;
    }

    {
        const char *p = (char *)opf_data;
        const char *itemref;
        while ((itemref = find_element(p, "itemref")) != NULL) {
            if (itemref[1] == '/') { p = itemref + 2; continue; }
            const char *tag_end = strchr(itemref, '>');
            if (!tag_end) break;
            size_t tag_len = (size_t)(tag_end - itemref + 1);
            char *tag_buf = malloc(tag_len + 1);
            if (!tag_buf) break;
            memcpy(tag_buf, itemref, tag_len);
            tag_buf[tag_len] = '\0';

            char idref[256] = {0};
            xml_attr(tag_buf, "idref", idref, sizeof(idref));
            free(tag_buf);

            if (idref[0]) {
                if (spine_count >= spine_cap) {
                    spine_cap *= 2;
                    char (*tmp)[256] = realloc(spine_idrefs,
                                               (size_t)spine_cap * 256);
                    if (!tmp) break;
                    spine_idrefs = tmp;
                }
                snprintf(spine_idrefs[spine_count], 256, "%s", idref);
                spine_count++;
            }
            p = tag_end + 1;
        }
    }
    free(opf_data);

    /* 5. Build ordered content file list and extract/strip */
    size_t text_cap = 65536;
    size_t text_len = 0;
    char *text = malloc(text_cap);
    if (!text) {
        free(spine_idrefs);
        free(manifest);
        free(opf_dir);
        zip_close(archive);
        return -1;
    }

    int all_ch_cap = 64;
    int all_ch_count = 0;
    Chapter *all_chapters = malloc((size_t)all_ch_cap * sizeof(Chapter));
    if (!all_chapters) {
        free(text);
        free(spine_idrefs);
        free(manifest);
        free(opf_dir);
        zip_close(archive);
        return -1;
    }

    int total_words = 0;

    for (int s = 0; s < spine_count; s++) {
        /* Find manifest item by idref */
        const char *href = NULL;
        for (int m = 0; m < manifest_count; m++) {
            if (strcmp(manifest[m].id, spine_idrefs[s]) == 0) {
                href = manifest[m].href;
                break;
            }
        }
        if (!href) continue;

        /* Build full ZIP path */
        char zip_path[2048];
        snprintf(zip_path, sizeof(zip_path), "%s%s", opf_dir, href);

        /* Extract content file */
        uint8_t *content_data = NULL;
        size_t content_len = 0;
        if (zip_extract(archive, zip_path, &content_data, &content_len) < 0)
            continue;  /* skip missing files */

        /* Strip HTML */
        Chapter *file_chapters = NULL;
        int file_ch_count = 0;
        char *stripped = html_strip((char *)content_data, content_len,
                                    &file_chapters, &file_ch_count,
                                    total_words);
        free(content_data);
        if (!stripped) { free(file_chapters); continue; }

        size_t slen = strlen(stripped);
        /* Ensure capacity */
        while (text_len + slen + 2 > text_cap) {
            text_cap *= 2;
            char *tmp = realloc(text, text_cap);
            if (!tmp) {
                free(stripped);
                free(file_chapters);
                goto finish;
            }
            text = tmp;
        }

        /* Add separator space if needed */
        if (text_len > 0 && slen > 0) {
            text[text_len++] = ' ';
        }
        memcpy(text + text_len, stripped, slen);
        text_len += slen;

        /* Accumulate chapters */
        for (int c = 0; c < file_ch_count; c++) {
            if (all_ch_count >= all_ch_cap) {
                all_ch_cap *= 2;
                Chapter *tmp = realloc(all_chapters,
                    (size_t)all_ch_cap * sizeof(Chapter));
                if (!tmp) break;
                all_chapters = tmp;
            }
            all_chapters[all_ch_count++] = file_chapters[c];
        }
        free(file_chapters);

        total_words += count_words(stripped, slen);
        free(stripped);
    }

finish:
    text[text_len] = '\0';

    free(spine_idrefs);
    free(manifest);
    free(opf_dir);
    zip_close(archive);

    /* Load into ReaderText */
    int ret = reader_load_buffer(text, text_len, "EPUB", out);
    free(text);
    if (ret < 0) {
        free(all_chapters);
        return -1;
    }

    /* Set chapters if we found any */
    if (all_ch_count > 0) {
        reader_set_chapters(out, all_chapters, all_ch_count);
    } else {
        free(all_chapters);
    }

    return 0;
}
