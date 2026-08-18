#include "ui_text_small.h"

static unsigned char GlyphBits(char ch, int row) {
    static const unsigned char blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const unsigned char space[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const unsigned char zero[7]  = { 14, 17, 19, 21, 25, 17, 14 };
    static const unsigned char one[7]   = { 4, 12, 4, 4, 4, 4, 14 };
    static const unsigned char two[7]   = { 14, 17, 1, 2, 4, 8, 31 };
    static const unsigned char three[7] = { 30, 1, 1, 14, 1, 1, 30 };
    static const unsigned char four[7]  = { 2, 6, 10, 18, 31, 2, 2 };
    static const unsigned char five[7]  = { 31, 16, 16, 30, 1, 1, 30 };
    static const unsigned char six[7]   = { 14, 16, 16, 30, 17, 17, 14 };
    static const unsigned char seven[7] = { 31, 1, 2, 4, 8, 8, 8 };
    static const unsigned char eight[7] = { 14, 17, 17, 14, 17, 17, 14 };
    static const unsigned char nine[7]  = { 14, 17, 17, 15, 1, 1, 14 };
    static const unsigned char a[7]     = { 14, 17, 17, 31, 17, 17, 17 };
    static const unsigned char b[7]     = { 30, 17, 17, 30, 17, 17, 30 };
    static const unsigned char c[7]     = { 14, 17, 16, 16, 16, 17, 14 };
    static const unsigned char d[7]     = { 30, 17, 17, 17, 17, 17, 30 };
    static const unsigned char e[7]     = { 31, 16, 16, 30, 16, 16, 31 };
    static const unsigned char f[7]     = { 31, 16, 16, 30, 16, 16, 16 };
    static const unsigned char g[7]     = { 14, 17, 16, 23, 17, 17, 15 };
    static const unsigned char h[7]     = { 17, 17, 17, 31, 17, 17, 17 };
    static const unsigned char i[7]     = { 14, 4, 4, 4, 4, 4, 14 };
    static const unsigned char j[7]     = { 1, 1, 1, 1, 17, 17, 14 };
    static const unsigned char k[7]     = { 17, 18, 20, 24, 20, 18, 17 };
    static const unsigned char l[7]     = { 16, 16, 16, 16, 16, 16, 31 };
    static const unsigned char m[7]     = { 17, 27, 21, 21, 17, 17, 17 };
    static const unsigned char n[7]     = { 17, 25, 21, 19, 17, 17, 17 };
    static const unsigned char o[7]     = { 14, 17, 17, 17, 17, 17, 14 };
    static const unsigned char p[7]     = { 30, 17, 17, 30, 16, 16, 16 };
    static const unsigned char q[7]     = { 14, 17, 17, 17, 21, 18, 13 };
    static const unsigned char r[7]     = { 30, 17, 17, 30, 20, 18, 17 };
    static const unsigned char s[7]     = { 15, 16, 16, 14, 1, 1, 30 };
    static const unsigned char t[7]     = { 31, 4, 4, 4, 4, 4, 4 };
    static const unsigned char u[7]     = { 17, 17, 17, 17, 17, 17, 14 };
    static const unsigned char v[7]     = { 17, 17, 17, 17, 10, 10, 4 };
    static const unsigned char w[7]     = { 17, 17, 17, 21, 21, 21, 10 };
    static const unsigned char x[7]     = { 17, 17, 10, 4, 10, 17, 17 };
    static const unsigned char y[7]     = { 17, 17, 10, 4, 4, 4, 4 };
    static const unsigned char z[7]     = { 31, 1, 2, 4, 8, 16, 31 };
    static const unsigned char lbr[7]   = { 14, 8, 8, 8, 8, 8, 14 };
    static const unsigned char rbr[7]   = { 14, 2, 2, 2, 2, 2, 14 };

    const unsigned char* glyph = blank;
    switch (ch) {
        case ' ': glyph = space; break;
        case '0': glyph = zero; break;
        case '1': glyph = one; break;
        case '2': glyph = two; break;
        case '3': glyph = three; break;
        case '4': glyph = four; break;
        case '5': glyph = five; break;
        case '6': glyph = six; break;
        case '7': glyph = seven; break;
        case '8': glyph = eight; break;
        case '9': glyph = nine; break;
        case 'A': glyph = a; break;
        case 'B': glyph = b; break;
        case 'C': glyph = c; break;
        case 'D': glyph = d; break;
        case 'E': glyph = e; break;
        case 'F': glyph = f; break;
        case 'G': glyph = g; break;
        case 'H': glyph = h; break;
        case 'I': glyph = i; break;
        case 'J': glyph = j; break;
        case 'K': glyph = k; break;
        case 'L': glyph = l; break;
        case 'M': glyph = m; break;
        case 'N': glyph = n; break;
        case 'O': glyph = o; break;
        case 'P': glyph = p; break;
        case 'Q': glyph = q; break;
        case 'R': glyph = r; break;
        case 'S': glyph = s; break;
        case 'T': glyph = t; break;
        case 'U': glyph = u; break;
        case 'V': glyph = v; break;
        case 'W': glyph = w; break;
        case 'X': glyph = x; break;
        case 'Y': glyph = y; break;
        case 'Z': glyph = z; break;
        case '[': glyph = lbr; break;
        case ']': glyph = rbr; break;
    }
    return glyph[row];
}

void UiTextSmallDraw(RenderContext* render, int x, int y, const char* text, int scale, uint32_t color) {
    int cursor = x;
    for (const char* p = text; *p; ++p) {
        for (int row = 0; row < 7; ++row) {
            unsigned char bits = GlyphBits(*p, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) {
                    DrawRect(render, cursor + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        cursor += 6 * scale;
    }
}

void UiTextSmallDrawContext(RenderContext* render, int x, int y, const char* text, int scale, uint32_t color, uint32_t bg_color) {
    UiTextSmallDraw(render, x + 2, y + 2, text, scale, bg_color);
    UiTextSmallDraw(render, x, y, text, scale, color);
}
