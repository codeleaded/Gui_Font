/*
#if defined(__linux__) && !defined(_WIN32)
    #include "/home/codeleaded/System/Static/Library/WindowEngine.h"
    #include "/home/codeleaded/System/Static/Library/StdFont.h"
#elif defined(_WIN32) || defined(_WIN64)
    #include "/home/codeleaded/System/Static/Library/WindowEngine.h"
    #include "/home/codeleaded/System/Static/Library/StdFont.h"
    //#include "F:/home/codeleaded/System/Static/Library/OMML.h"
    //#include "F:/home/codeleaded/System/Static/Library/WindowEngine.h"
#elif defined(__APPLE__)
    #error "Apple not supported!"
#else
    #error "Platform not supported!"
#endif



StdFont stdfont;

void Setup(AlxWindow* w){
    stdfont = StdFont_New();
}
void Update(AlxWindow* w){
    

    Clear(BLACK);

    StdFont_Render_CStr(WINDOW_STD_ARGS,&stdfont,"Hello World",0.0f,0.0f,WHITE);
}
void Delete(AlxWindow* w){
    StdFont_Free(&stdfont);
}

int main(){
    if(Create("Font Renderer",1900,1000,1,1,Setup,Update,Delete))
       Start();
    return 0;
}
*/

// otf_parser_render.c
// Minimaler SFNT (OTF/TTF) Parser in C, glyf/simple glyph parsing, cmap format 4,
// und einfache Bitmap-Ausgabe (BMP).
//
// Limitierungen:
// - Unterstützt cmap format 4 (nicht 12/14/...)
// - Unterstützt glyf simple glyphs (keine composite glyphs)
// - Keine hinting/subpixel AA
// - Wenn CFF vorhanden: wird ausgelesen, aber Type2-Interpreter ist nicht implementiert.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int16_t  i16;

// Utility: big-endian reads
static u16 read_u16_be(const u8 *b) { return (b[0]<<8) | b[1]; }
static u32 read_u32_be(const u8 *b) { return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]; }
static i16 read_s16_be(const u8 *b) { return (i16)read_u16_be(b); }

// Table record
typedef struct {
    char tag[5];
    u32 checksum;
    u32 offset;
    u32 length;
} TableRecord;

// Simple memory file
typedef struct {
    u8 *data;
    size_t size;
} MemFile;

static MemFile load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = malloc(sz);
    if (fread(buf,1,sz,f) != sz) { perror("fread"); exit(1); }
    fclose(f);
    MemFile mf = {buf, sz};
    return mf;
}

static const u8* table_ptr(const MemFile *mf, u32 offset, u32 length) {
    if (offset + length > mf->size) {
        fprintf(stderr, "table pointer out of range\n"); exit(1);
    }
    return mf->data + offset;
}

// Structure to store extracted glyph outline (TrueType simple)
typedef struct {
    int n_points;
    int *x; // scaled integer coordinates in font units
    int *y;
    u8 *oncurve; // 1 = on-curve
    int n_contours;
    int *contours_end; // end index for each contour
} GlyphOutline;

static void free_outline(GlyphOutline *g) {
    if (!g) return;
    free(g->x); free(g->y); free(g->oncurve); free(g->contours_end);
    memset(g,0,sizeof(*g));
}

// ---------- SFNT header + table directory -----------
typedef struct {
    u32 sfntVersion;
    u16 numTables;
    TableRecord *records;
} SFNT;

static int find_table_index(const SFNT *sfnt, const char *tag) {
    for (int i=0;i<sfnt->numTables;i++) if (strncmp(sfnt->records[i].tag, tag,4)==0) return i;
    return -1;
}

// ---------- cmap format 4 parsing (Unicode->glyphIndex) ----------
typedef struct {
    // simple mapping: store up to some entries
    u32 count;
    // for simplicity we store a mapping array for BMP (0..0xFFFF) -> glyphIndex; 0 = missing
    u16 *map; // allocated 65536 entries
} CmapMap;

static CmapMap parse_cmap_format4(const u8 *ptr, u32 length) {
    CmapMap cm; cm.map = calloc(65536, sizeof(u16)); cm.count = 0;
    if (length < 8) return cm;
    u16 format = read_u16_be(ptr);
    if (format != 4) { fprintf(stderr,"cmap format %d not supported in this parser\n", format); return cm; }
    u16 segCountX2 = read_u16_be(ptr+6);
    u16 segCount = segCountX2 / 2;
    const u8 *p = ptr;
    u16 endCountOffset = 14;
    const u8 *endCount = p + endCountOffset;
    const u8 *startCount = endCount + 2 + segCount*2; // skip reservedPad (2) + endCount array
    const u8 *idDelta = startCount + segCount*2;
    const u8 *idRangeOffset = idDelta + segCount*2;
    const u8 *glyphIdArray = idRangeOffset + segCount*2;
    for (int i=0;i<segCount;i++) {
        u16 endc = read_u16_be(endCount + i*2);
        u16 startc = read_u16_be(startCount + i*2);
        i16 delta = (i16)read_u16_be(idDelta + i*2);
        u16 rangeOffset = read_u16_be(idRangeOffset + i*2);
        for (u32 ch = startc; ch <= endc; ch++) {
            u16 gid = 0;
            if (rangeOffset == 0) {
                gid = (u16)((ch + delta) & 0xFFFF);
            } else {
                // glyphIdArray index: calculate offset to glyphId for this char
                u32 idx = ( (idRangeOffset + i*2) - p ) + rangeOffset + 2*(ch - startc);
                const u8 *gptr = p + idx;
                if ((u32)(gptr - p + 2) <= length) {
                    gid = read_u16_be(gptr);
                    if (gid != 0) gid = (u16)((gid + delta) & 0xFFFF);
                } else gid = 0;
            }
            cm.map[ch] = gid;
            if (gid) cm.count++;
        }
    }
    return cm;
}

// ---------- head + maxp + loca parsing -------------
typedef struct {
    u16 unitsPerEm;
    i16 xMin, yMin, xMax, yMax;
    u16 indexToLocFormat; // 0 = short (u16/2), 1 = long (u32)
} HeadTable;

static HeadTable parse_head(const u8 *ptr, u32 len) {
    HeadTable h = {0};
    if (len < 54) return h;
    h.unitsPerEm = read_u16_be(ptr + 18);
    h.xMin = read_s16_be(ptr + 36);
    h.yMin = read_s16_be(ptr + 38);
    h.xMax = read_s16_be(ptr + 40);
    h.yMax = read_s16_be(ptr + 42);
    h.indexToLocFormat = read_u16_be(ptr + 50);
    return h;
}

typedef struct {
    u16 numGlyphs;
} MaxpTable;

static MaxpTable parse_maxp(const u8 *ptr, u32 len) {
    MaxpTable m = {0};
    if (len < 6) return m;
    m.numGlyphs = read_u16_be(ptr + 4);
    return m;
}

// ---------- glyf parsing (simple glyphs) ------------
// We need loca table to get offsets.
static int read_u16_at(const u8 *d, u32 offset, u32 size, u16 *out) {
    if (offset + 2 > size) return -1;
    *out = read_u16_be(d + offset); return 0;
}

static int parse_glyf_simple(const u8 *glyf_buf, u32 glyf_len, u32 glyf_offset,
                             GlyphOutline *out) {
    if (glyf_offset + 10 > glyf_len) return -1;
    const u8 *p = glyf_buf + glyf_offset;
    i16 numberOfContours = read_s16_be(p);
    if (numberOfContours < 0) {
        // composite - not supported in this minimal parser
        return -2;
    }
    int nContours = numberOfContours;
    u16 *endPts = malloc(nContours * sizeof(u16));
    int i;
    for (i=0;i<nContours;i++) endPts[i] = read_u16_be(p + 10 + i*2);
    int instructionLengthOffset = 10 + nContours*2;
    u16 instructionLength = read_u16_be(p + instructionLengthOffset);
    u8 *instructions = NULL;
    if (instructionLength) {
        instructions = malloc(instructionLength);
        memcpy(instructions, p + instructionLengthOffset + 2, instructionLength);
    }
    // after instructions comes flags and coordinate arrays
    const u8 *flagsPtr = p + instructionLengthOffset + 2 + instructionLength;
    // Count total points:
    int totalPoints = endPts[nContours-1] + 1;
    u8 *flags = malloc(totalPoints);
    int fidx = 0;
    const u8 *fp = flagsPtr;
    while (fidx < totalPoints) {
        u8 flag = *fp++;
        flags[fidx++] = flag;
        if (flag & 0x08) { // repeat flag
            u8 rep = *fp++;
            for (int r=0;r<rep;r++) flags[fidx++] = flag;
        }
    }
    // read X coordinates (they are encoded relative)
    int *xs = malloc(totalPoints * sizeof(int));
    int *ys = malloc(totalPoints * sizeof(int));
    int x = 0, y = 0;
    const u8 *xp = fp;
    for (i=0;i<totalPoints;i++) {
        u8 flag = flags[i];
        if (flag & 0x02) { // x-short vector
            u8 bx = *xp++;
            if (flag & 0x10) x += bx; else x -= bx;
        } else {
            if (!(flag & 0x10)) { // signed 16-bit
                i16 v = read_s16_be(xp);
                xp += 2;
                x += v;
            } // else same x (no data)
        }
        xs[i] = x;
    }
    // y
    for (i=0;i<totalPoints;i++) {
        u8 flag = flags[i];
        if (flag & 0x04) { // y-short vector
            u8 by = *xp++;
            if (flag & 0x20) y += by; else y -= by;
        } else {
            if (!(flag & 0x20)) {
                i16 v = read_s16_be(xp);
                xp += 2;
                y += v;
            }
        }
        ys[i] = y;
    }
    // Fill GlyphOutline
    out->n_points = totalPoints;
    out->x = malloc(totalPoints * sizeof(int));
    out->y = malloc(totalPoints * sizeof(int));
    out->oncurve = malloc(totalPoints * sizeof(u8));
    out->n_contours = nContours;
    out->contours_end = malloc(nContours * sizeof(int));
    for (i=0;i<totalPoints;i++) {
        out->x[i] = xs[i];
        out->y[i] = ys[i];
        out->oncurve[i] = (flags[i] & 0x01) ? 1 : 0;
    }
    for (i=0;i<nContours;i++) out->contours_end[i] = endPts[i];
    // cleanup
    free(endPts); free(instructions); free(flags); free(xs); free(ys);
    return 0;
}

// ---------- Simple rendering to bitmap (stroke contours) ----------
typedef struct {
    int w,h;
    u8 *rgb; // 3 bytes per pixel
} Bitmap;

static Bitmap *bmp_create(int w,int h) {
    Bitmap *b = malloc(sizeof(Bitmap));
    b->w = w; b->h = h;
    b->rgb = calloc(w*h*3,1);
    // background white
    for (int i=0;i<w*h;i++) { b->rgb[3*i+0]=255; b->rgb[3*i+1]=255; b->rgb[3*i+2]=255; }
    return b;
}
static void bmp_set_pixel(Bitmap *b, int x,int y, u8 r,u8 g,u8 bl) {
    if (x<0||x>=b->w||y<0||y>=b->h) return;
    int idx = (y*b->w + x)*3;
    b->rgb[idx+0]=r; b->rgb[idx+1]=g; b->rgb[idx+2]=bl;
}
static void bmp_line(Bitmap *b, int x0,int y0,int x1,int y1) {
    int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        bmp_set_pixel(b,x0,y0,0,0,0);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Approximate quadratic Bezier by subdividing
static void draw_quad(Bitmap *b, int x0,int y0,int cx,int cy,int x1,int y1) {
    const int steps = 16;
    double px = x0, py = y0;
    for (int i=1;i<=steps;i++) {
        double t = (double)i/steps;
        double it = 1.0 - t;
        double x = it*it*x0 + 2*it*t*cx + t*t*x1;
        double y = it*it*y0 + 2*it*t*cy + t*t*y1;
        bmp_line(b, (int)round(px),(int)round(py), (int)round(x),(int)round(y));
        px = x; py = y;
    }
}

// convert font units -> pixel coords (simple scale)
static void render_glyph_to_bitmap(const GlyphOutline *g, Bitmap *b, HeadTable head, int pxSize, int tx, int ty) {
    if (!g->n_points) return;
    double scale = (double)pxSize / (double)head.unitsPerEm;
    int contourStart = 0;
    for (int ci=0; ci<g->n_contours; ci++) {
        int end = g->contours_end[ci];
        // traverse points from contourStart..end (inclusive)
        // TrueType quadratic handling: off-curve points are control points.
        // Algorithm: iterate points with wrap-around, handle on/off curve per spec.
        int n = end - contourStart + 1;
        // collect points in array for wrap-around
        for (int i=0;i<n;i++) {
            int idx = contourStart + i;
            // nothing needed pre-store
        }
        // find first point index that is on-curve (if none, create implied)
        int start_idx = -1;
        for (int i=0;i<n;i++) if (g->oncurve[contourStart+i]) { start_idx = contourStart+i; break; }
        // if none on-curve, implied on-curve midpoint between last and first off-curve
        int implied_first_x=0, implied_first_y=0;
        int has_implied = 0;
        if (start_idx == -1) {
            int idxA = contourStart + n-1;
            int idxB = contourStart + 0;
            implied_first_x = (g->x[idxA] + g->x[idxB]) / 2;
            implied_first_y = (g->y[idxA] + g->y[idxB]) / 2;
            has_implied = 1;
            start_idx = contourStart; // we'll start iteration at contourStart
        }
        // iterate segments
        int cur = start_idx;
        int next_i = ( (cur - contourStart) + 1 ) % n + contourStart;
        int firstX, firstY;
        if (has_implied) {
            firstX = implied_first_x; firstY = implied_first_y;
        } else {
            firstX = g->x[cur]; firstY = g->y[cur];
        }
        int penX = firstX, penY = firstY;
        int i = (cur - contourStart + 1) % n + contourStart;
        while (1) {
            int idx = i;
            int prev = ( (i - contourStart -1 + n) % n ) + contourStart;
            // find next on-curve point possibly with controls
            if (g->oncurve[idx]) {
                // straight or quad from pen -> idx directly if previous off-curve?
                // if previous was off-curve control point, then quad from pen->prev->idx
                if (!g->oncurve[prev]) {
                    // prev is control
                    int cx = g->x[prev], cy = g->y[prev];
                    // draw quad from pen -> control -> idx
                    int x0 = (int)round(penX * scale) + tx;
                    int y0 = (int)round(-penY * scale) + ty;
                    int cpx = (int)round(cx * scale) + tx;
                    int cpy = (int)round(-cy * scale) + ty;
                    int x1 = (int)round(g->x[idx] * scale) + tx;
                    int y1 = (int)round(-g->y[idx] * scale) + ty;
                    draw_quad(b, x0,y0, cpx,cpy, x1,y1);
                } else {
                    // both on-curve -> straight line
                    int x0 = (int)round(penX * scale) + tx;
                    int y0 = (int)round(-penY * scale) + ty;
                    int x1 = (int)round(g->x[idx] * scale) + tx;
                    int y1 = (int)round(-g->y[idx] * scale) + ty;
                    bmp_line(b, x0,y0, x1,y1);
                }
                penX = g->x[idx]; penY = g->y[idx];
            } else {
                // off-curve: need to look ahead to find next on-curve
                int nextIdx = ( (idx - contourStart + 1) % n ) + contourStart;
                if (g->oncurve[nextIdx]) {
                    // control then oncurve -> quad from pen -> idx -> nextIdx
                    int cx = g->x[idx], cy = g->y[idx];
                    int x0 = (int)round(penX * scale) + tx;
                    int y0 = (int)round(-penY * scale) + ty;
                    int cpx = (int)round(cx * scale) + tx;
                    int cpy = (int)round(-cy * scale) + ty;
                    int x1 = (int)round(g->x[nextIdx] * scale) + tx;
                    int y1 = (int)round(-g->y[nextIdx] * scale) + ty;
                    draw_quad(b, x0,y0, cpx,cpy, x1,y1);
                    penX = g->x[nextIdx]; penY = g->y[nextIdx];
                    i = nextIdx; // skip ahead
                } else {
                    // two consecutive off-curve: implied on-curve midpoint between them
                    int cx1 = g->x[idx], cy1 = g->y[idx];
                    int cx2 = g->x[nextIdx], cy2 = g->y[nextIdx];
                    int midx = (cx1 + cx2) / 2;
                    int midy = (cy1 + cy2) / 2;
                    int x0 = (int)round(penX * scale) + tx;
                    int y0 = (int)round(-penY * scale) + ty;
                    int cpx = (int)round(cx1 * scale) + tx;
                    int cpy = (int)round(-cy1 * scale) + ty;
                    int x1 = (int)round(midx * scale) + tx;
                    int y1 = (int)round(-midy * scale) + ty;
                    draw_quad(b, x0,y0, cpx,cpy, x1,y1);
                    penX = midx; penY = midy;
                    i = nextIdx; // continue from nextIdx
                }
            }
            // advance i
            i = ( (i - contourStart + 1) % n ) + contourStart;
            if (i == start_idx) break;
        }
        // close path: draw line/quads back to first if needed
        int x0 = (int)round(penX * scale) + tx;
        int y0 = (int)round(-penY * scale) + ty;
        int x1 = (int)round(firstX * scale) + tx;
        int y1 = (int)round(-firstY * scale) + ty;
        bmp_line(b, x0,y0, x1,y1);
        contourStart = end + 1;
    }
}

// ---------- BMP writer ----------
static void write_bmp(const Bitmap *b, const char *path) {
    FILE *f = fopen(path,"wb");
    if (!f) { perror("fopen"); return; }
    int w = b->w, h = b->h;
    int rowpad = (4 - (w*3)%4) %4;
    int imgSize = (w*3 + rowpad) * h;
    u8 header[54] = {0};
    // BMP header
    header[0] = 'B'; header[1] = 'M';
    u32 fileSize = 54 + imgSize;
    header[2] = (u8)(fileSize & 0xFF); header[3] = (u8)((fileSize>>8)&0xFF);
    header[4] = (u8)((fileSize>>16)&0xFF); header[5] = (u8)((fileSize>>24)&0xFF);
    header[10] = 54;
    header[14] = 40; // DIB header size
    header[18] = (u8)(w & 0xFF); header[19] = (u8)((w>>8)&0xFF);
    header[20] = (u8)((w>>16)&0xFF); header[21] = (u8)((w>>24)&0xFF);
    header[22] = (u8)(h & 0xFF); header[23] = (u8)((h>>8)&0xFF);
    header[24] = (u8)((h>>16)&0xFF); header[25] = (u8)((h>>24)&0xFF);
    header[26] = 1; header[28] = 24;
    fwrite(header,1,54,f);
    // write pixel data bottom-up BGR
    u8 *row = malloc(w*3 + rowpad);
    for (int y=h-1;y>=0;y--) {
        for (int x=0;x<w;x++) {
            int idx = (y*w + x)*3;
            row[x*3+0] = b->rgb[idx+2]; // B
            row[x*3+1] = b->rgb[idx+1];
            row[x*3+2] = b->rgb[idx+0];
        }
        for (int p=0;p<rowpad;p++) row[w*3 + p] = 0;
        fwrite(row,1,w*3 + rowpad,f);
    }
    free(row); fclose(f);
}

// ---------------- Main ----------------
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,"Usage: %s font.otf U+XXXX\n", argv[0]);
        return 1;
    }
    const char *fontpath = argv[1];
    const char *codepoint_arg = argv[2];
    // parse codepoint like U+0041 or hex
    unsigned int codepoint = 0;
    if (sscanf(codepoint_arg, "U+%x", &codepoint) != 1) {
        if (sscanf(codepoint_arg, "%x", &codepoint) != 1) {
            fprintf(stderr,"Can't parse codepoint\n"); return 1;
        }
    }

    MemFile mf = load_file(fontpath);
    const u8 *data = mf.data;
    if (mf.size < 12) { fprintf(stderr,"file too small\n"); return 1; }
    u32 sfntVersion = read_u32_be(data);
    u16 numTables = read_u16_be(data+4);
    SFNT sfnt; sfnt.sfntVersion = sfntVersion; sfnt.numTables = numTables;
    sfnt.records = malloc(numTables * sizeof(TableRecord));
    // read table records at offset 12
    const u8 *recp = data + 12;
    for (int i=0;i<numTables;i++) {
        TableRecord *r = &sfnt.records[i];
        memcpy(r->tag, recp + i*16, 4); r->tag[4]=0;
        r->checksum = read_u32_be(recp + i*16 + 4);
        r->offset = read_u32_be(recp + i*16 + 8);
        r->length = read_u32_be(recp + i*16 + 12);
    }

    // find cmap
    int cmap_idx = find_table_index(&sfnt, "cmap");
    if (cmap_idx < 0) { fprintf(stderr,"no cmap table\n"); return 1; }
    const u8 *cmap_ptr = table_ptr(&mf, sfnt.records[cmap_idx].offset, sfnt.records[cmap_idx].length);
    // parse cmap directory: version (2) + numberSubtables
    u16 cmap_version = read_u16_be(cmap_ptr);
    u16 cmap_sub = read_u16_be(cmap_ptr+2);
    const u8 *sub = cmap_ptr + 4;
    CmapMap cmap_map = {0};
    int found_format4 = 0;
    for (int i=0;i<cmap_sub;i++) {
        u16 platform = read_u16_be(sub + i*8);
        u16 platEnc = read_u16_be(sub + i*8 + 2);
        u32 offset = read_u32_be(sub + i*8 + 4);
        const u8 *fmtPtr = cmap_ptr + offset;
        u16 fmt = read_u16_be(fmtPtr);
        if (fmt == 4) {
            cmap_map = parse_cmap_format4(fmtPtr, sfnt.records[cmap_idx].length - offset);
            found_format4 = 1; break;
        }
    }
    if (!found_format4) { fprintf(stderr,"no cmap format 4 found\n"); }

    u16 glyphIndex = 0;
    if (codepoint <= 0xFFFF) glyphIndex = cmap_map.map[codepoint];
    else { fprintf(stderr,"codepoint > BMP not supported in this demo\n"); }

    printf("Unicode U+%04X -> glyphIndex %d\n", codepoint, glyphIndex);

    // parse head & maxp
    int head_idx = find_table_index(&sfnt, "head");
    int maxp_idx = find_table_index(&sfnt, "maxp");
    HeadTable head = {0}; MaxpTable maxp = {0};
    if (head_idx >= 0) head = parse_head(table_ptr(&mf, sfnt.records[head_idx].offset, sfnt.records[head_idx].length), sfnt.records[head_idx].length);
    if (maxp_idx >= 0) maxp = parse_maxp(table_ptr(&mf, sfnt.records[maxp_idx].offset, sfnt.records[maxp_idx].length), sfnt.records[maxp_idx].length);

    printf("unitsPerEm = %d, numGlyphs = %d, indexToLocFormat=%d\n", head.unitsPerEm, maxp.numGlyphs, head.indexToLocFormat);

    // find loca and glyf
    int loca_idx = find_table_index(&sfnt, "loca");
    int glyf_idx = find_table_index(&sfnt, "glyf");
    if (loca_idx < 0 || glyf_idx < 0) {
        printf("No glyf/loca found — maybe this is a CFF font.\n");
        int cff_idx = find_table_index(&sfnt, "CFF ");
        if (cff_idx >= 0) {
            printf("CFF table present at offset %u length %u\n", sfnt.records[cff_idx].offset, sfnt.records[cff_idx].length);
            // For now extract CFF table bytes and print first bytes of CharStrings INDEX
            const u8 *cff = table_ptr(&mf, sfnt.records[cff_idx].offset, sfnt.records[cff_idx].length);
            // Very minimal: print first 64 bytes
            printf("CFF first 64 bytes (hex):\n");
            for (int i=0;i<64 && i < (int)sfnt.records[cff_idx].length; i++) printf("%02X ", cff[i]);
            printf("\n");
            printf("Implementing a Type2 CharString interpreter is the next step (stack-machine, subrs, global subrs).\n");
            return 0;
        } else {
            fprintf(stderr,"No glyf/loca and no CFF: can't parse outlines\n"); return 1;
        }
    }

    // read loca table into offsets
    const u8 *loca_ptr = table_ptr(&mf, sfnt.records[loca_idx].offset, sfnt.records[loca_idx].length);
    const u8 *glyf_ptr = table_ptr(&mf, sfnt.records[glyf_idx].offset, sfnt.records[glyf_idx].length);
    u32 numGlyphs = maxp.numGlyphs;
    u32 *glyphOffsets = malloc((numGlyphs+1) * sizeof(u32));
    if (head.indexToLocFormat == 0) {
        // short format: u16 offsets / 2
        for (u32 i=0;i<=numGlyphs;i++) {
            u16 off = read_u16_be(loca_ptr + i*2);
            glyphOffsets[i] = off * 2;
        }
    } else {
        for (u32 i=0;i<=numGlyphs;i++) glyphOffsets[i] = read_u32_be(loca_ptr + i*4);
    }

    if (glyphIndex >= numGlyphs) { fprintf(stderr,"glyphIndex out of range\n"); return 1; }
    u32 g0 = glyphOffsets[glyphIndex];
    u32 g1 = glyphOffsets[glyphIndex+1];
    if (g0 == g1) { fprintf(stderr,"empty glyph\n"); return 1; }
    GlyphOutline outline = {0};
    int r = parse_glyf_simple(glyf_ptr, sfnt.records[glyf_idx].length, g0, &outline);
    if (r == -2) { fprintf(stderr,"glyph is composite — not supported in this demo\n"); return 1; }
    if (r < 0) { fprintf(stderr,"failed to parse glyf simple glyph\n"); return 1; }

    printf("Glyph parsed: %d points, %d contours\n", outline.n_points, outline.n_contours);
    for (int c=0;c<outline.n_contours;c++) {
        printf(" contour %d end=%d\n", c, outline.contours_end[c]);
    }
    // Render to bitmap
    int px = 256;
    Bitmap *b = bmp_create(px, px);
    // center baseline roughly
    int tx = px/2;
    int ty = px/2 + 40;
    render_glyph_to_bitmap(&outline, b, head, 200 /*pixels em*/, tx, ty);
    write_bmp(b, "out.bmp");
    printf("Wrote out.bmp (open it to see the glyph render)\n");

    // cleanup
    free_outline(&outline);
    free(glyphOffsets);
    free(cmap_map.map);
    free(sfnt.records);
    free(mf.data);
    free(b->rgb); free(b);
    return 0;
}
