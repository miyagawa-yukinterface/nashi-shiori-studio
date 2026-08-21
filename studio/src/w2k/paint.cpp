#include "paint.h"

#include <vector>

namespace nashi {
namespace w2k {

namespace {

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], n);
    return w;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** 角をすこし落とした四角の外形を作る（GDI には丸みが無いので、面取りで代えます）。 */
void AddChamferedRect(std::vector<POINT>& pts, int l, int t, int r, int b, int c) {
    if (c * 2 > r - l) c = (r - l) / 2;
    if (c * 2 > b - t) c = (b - t) / 2;
    POINT p[8] = {
        { l + c, t }, { r - c, t }, { r, t + c }, { r, b - c },
        { r - c, b }, { l + c, b }, { l, b - c }, { l, t + c },
    };
    for (int i = 0; i < 8; i++) pts.push_back(p[i]);
}

void Add(std::vector<POINT>& pts, int x, int y) {
    POINT p = { x, y };
    pts.push_back(p);
}

/** 上の切り欠き（へこみ）を、左から右へ進む辺の途中に足す。 */
void AddNotchDown(std::vector<POINT>& pts, int x, int y, const Metrics& m) {
    Add(pts, x + m.notchX, y);
    Add(pts, x + m.notchX + 3, y + m.notchH);
    Add(pts, x + m.notchX + m.notchW - 3, y + m.notchH);
    Add(pts, x + m.notchX + m.notchW, y);
}

/** 下の出っぱりを、右から左へ進む辺の途中に足す。 */
void AddBumpDown(std::vector<POINT>& pts, int x, int y, const Metrics& m) {
    Add(pts, x + m.notchX + m.notchW, y);
    Add(pts, x + m.notchX + m.notchW - 3, y + m.notchH);
    Add(pts, x + m.notchX + 3, y + m.notchH);
    Add(pts, x + m.notchX, y);
}

/** そのブロックが持っている腕を、上から順に集める。 */
void CollectArms(const Layout& lay, int blockIndex, std::vector<int>& arms) {
    const Piece& b = lay.pieces[blockIndex];
    for (int i = b.firstChild; i < b.firstChild + b.childCount && i < (int)lay.pieces.size(); i++) {
        const Piece& p = lay.pieces[i];
        if (p.kind == PieceKind::Arm && p.depth == b.depth + 1) arms.push_back(i);
    }
}

/** ブロックの外形を作る。 */
void BuildOutline(const Layout& lay, int blockIndex, const Metrics& m, std::vector<POINT>& pts) {
    const Piece& b = lay.pieces[blockIndex];
    const int x = b.x, y = b.y, w = b.w, h = b.h;
    const Shape shape = b.def ? b.def->shape : Shape::Stack;

    if (shape == Shape::Reporter) {
        // 丸い薬のかたち
        int r = h / 2;
        Add(pts, x + r, y);
        Add(pts, x + w - r, y);
        Add(pts, x + w, y + r);
        Add(pts, x + w - r, y + h);
        Add(pts, x + r, y + h);
        Add(pts, x, y + r);
        return;
    }
    if (shape == Shape::Boolean) {
        int r = h / 2;
        Add(pts, x + r, y);
        Add(pts, x + w - r, y);
        Add(pts, x + w, y + r);
        Add(pts, x + w - r, y + h);
        Add(pts, x + r, y + h);
        Add(pts, x, y + r);
        return;
    }

    std::vector<int> arms;
    CollectArms(lay, blockIndex, arms);

    const bool isHat = (shape == Shape::Hat);
    const bool isCap = (shape == Shape::Cap);

    // ---- 上の辺
    if (isHat) {
        // 帽子。左から、ゆるい山をこえて右へ。
        Add(pts, x, y + m.hatHeight);
        const int steps = 6;
        for (int i = 0; i <= steps; i++) {
            double t = (double)i / steps;
            int px = x + (int)(t * (m.hatHeight * 3));
            int py = y + m.hatHeight - (int)(m.hatHeight * (1.0 - (1.0 - t) * (1.0 - t)));
            Add(pts, px, py);
        }
        Add(pts, x + w - m.corner, y + m.hatHeight);
        Add(pts, x + w, y + m.hatHeight + m.corner);
    } else {
        Add(pts, x + m.corner, y);
        AddNotchDown(pts, x, y, m);
        Add(pts, x + w - m.corner, y);
        Add(pts, x + w, y + m.corner);
    }

    // ---- 右の辺・腕のくぼみ
    if (arms.empty()) {
        Add(pts, x + w, y + h - m.corner);
        Add(pts, x + w - m.corner, y + h);
        if (!isCap) AddBumpDown(pts, x, y + h, m);
        Add(pts, x + m.corner, y + h);
    } else {
        for (size_t i = 0; i < arms.size(); i++) {
            const Piece& a = lay.pieces[arms[i]];
            // 見出し（または前の桟）の下まで下りて、左へ入る
            Add(pts, x + w, a.y);
            Add(pts, x + m.armIndent + m.notchX + m.notchW, a.y);
            // 腕の中に入るブロックの受け口（切り欠き）
            Add(pts, x + m.armIndent + m.notchX + m.notchW - 3, a.y + m.notchH);
            Add(pts, x + m.armIndent + m.notchX + 3, a.y + m.notchH);
            Add(pts, x + m.armIndent + m.notchX, a.y);
            Add(pts, x + m.armIndent, a.y);
            // 腕の左をとおって下へ
            Add(pts, x + m.armIndent, a.y + a.h);
            // 桟の右へ出る
            Add(pts, x + w, a.y + a.h);
        }
        Add(pts, x + w, y + h - m.corner);
        Add(pts, x + w - m.corner, y + h);
        if (!isCap) AddBumpDown(pts, x, y + h, m);
        Add(pts, x + m.corner, y + h);
    }

    // ---- 左の辺
    Add(pts, x, y + h - m.corner);
    Add(pts, x, y + (isHat ? m.hatHeight : m.corner));
}

void FillOutline(HDC dc, const std::vector<POINT>& pts, COLORREF fill, COLORREF line) {
    if (pts.size() < 3) return;
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    Polygon(dc, &pts[0], (int)pts.size());
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

COLORREF BlockColor(const Piece& b) {
    if (!b.def) return RGB(0x99, 0x99, 0x99);
    const CategoryDef* cat = FindCategory(b.def->category);
    return ColorFromHex(cat ? cat->color : NULL, RGB(0x99, 0x99, 0x99));
}

} // namespace

// ---------------------------------------------------------------------- 色

COLORREF ColorFromHex(const char* hex, COLORREF fallback) {
    if (!hex) return fallback;
    const char* p = hex;
    if (*p == '#') p++;
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = HexDigit(p[i]);
        if (v[i] < 0) return fallback;
    }
    return RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
}

COLORREF Shade(COLORREF c, double amount) {
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    if (amount >= 0) {
        r += (int)((255 - r) * amount);
        g += (int)((255 - g) * amount);
        b += (int)((255 - b) * amount);
    } else {
        r += (int)(r * amount);
        g += (int)(g * amount);
        b += (int)(b * amount);
    }
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return RGB(r, g, b);
}

// ------------------------------------------------------------------ 文字幅

int GdiMeasurer::Width(const std::string& utf8) const {
    if (utf8.empty()) return 0;
    std::wstring w = ToWide(utf8);
    HGDIOBJ old = SelectObject(dc_, font_);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc_, w.c_str(), (int)w.size(), &sz);
    SelectObject(dc_, old);
    return sz.cx;
}

// -------------------------------------------------------------------- 道具

bool PaintTools::Create(int pointSize) {
    // MS UI Gothic は Windows 2000 から入っています。
    HDC screen = GetDC(NULL);
    int height = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(NULL, screen);

    blockFont = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH, L"MS UI Gothic");
    slotFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH, L"MS UI Gothic");
    return blockFont != NULL && slotFont != NULL;
}

void PaintTools::Free() {
    if (blockFont) { DeleteObject(blockFont); blockFont = NULL; }
    if (slotFont) { DeleteObject(slotFont); slotFont = NULL; }
}

// -------------------------------------------------------------------- 下地

void PaintBackground(HDC dc, const RECT& rc, const PaintStyle& style, int ox, int oy) {
    HBRUSH bg = CreateSolidBrush(style.background);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    if (style.gridStep <= 0) return;
    HPEN pen = CreatePen(PS_SOLID, 1, style.canvasLine);
    HGDIOBJ op = SelectObject(dc, pen);
    int step = style.gridStep;
    int startX = rc.left - ((rc.left + ox) % step + step) % step;
    for (int x = startX; x < rc.right; x += step) {
        MoveToEx(dc, x, rc.top, NULL);
        LineTo(dc, x, rc.bottom);
    }
    int startY = rc.top - ((rc.top + oy) % step + step) % step;
    for (int y = startY; y < rc.bottom; y += step) {
        MoveToEx(dc, rc.left, y, NULL);
        LineTo(dc, rc.right, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

// -------------------------------------------------------------------- 本体

void PaintLayout(HDC dc, const Layout& lay, const PaintTools& tools,
                 const PaintStyle& style, int ox, int oy) {
    Metrics m;   // 置き場所を決めたときと同じ寸法（既定のまま使います）

    SetBkMode(dc, TRANSPARENT);
    POINT oldOrg;
    SetViewportOrgEx(dc, -ox, -oy, &oldOrg);

    // ---- ブロックの地
    for (size_t i = 0; i < lay.pieces.size(); i++) {
        const Piece& p = lay.pieces[i];
        if (p.kind != PieceKind::Block) continue;
        std::vector<POINT> pts;
        BuildOutline(lay, (int)i, m, pts);
        COLORREF fill = BlockColor(p);
        // 中にはまっているブロックは、すこし明るくして見分けます
        if (p.depth > 0) fill = Shade(fill, 0.12);
        FillOutline(dc, pts, fill, Shade(fill, -0.35));
    }

    // ---- 欄と文字
    for (size_t i = 0; i < lay.pieces.size(); i++) {
        const Piece& p = lay.pieces[i];

        if (p.kind == PieceKind::Slot) {
            if (p.firstChild >= 0) continue;      // 中にブロックが入っている
            COLORREF fill = style.slotFill;
            std::vector<POINT> pts;
            if (p.boolSlot) {
                int r = p.h / 2;
                Add(pts, p.x + r, p.y);
                Add(pts, p.x + p.w - r, p.y);
                Add(pts, p.x + p.w, p.y + r);
                Add(pts, p.x + p.w - r, p.y + p.h);
                Add(pts, p.x + r, p.y + p.h);
                Add(pts, p.x, p.y + r);
                fill = RGB(0xe8, 0xea, 0xf2);
            } else {
                AddChamferedRect(pts, p.x, p.y, p.x + p.w, p.y + p.h, 4);
            }
            FillOutline(dc, pts, fill, RGB(0xb8, 0xbd, 0xcc));

            if (!p.text.empty()) {
                std::wstring w = ToWide(p.text);
                HGDIOBJ of = SelectObject(dc, tools.slotFont);
                SetTextColor(dc, style.slotText);
                RECT r = { p.x + 4, p.y, p.x + p.w - 4, p.y + p.h };
                DrawTextW(dc, w.c_str(), (int)w.size(), &r,
                          DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, of);
            }
            // えらぶ欄には、右に小さな三角を出す
            if (p.argKind != ArgKind::Input) {
                int cx = p.x + p.w - 8, cy = p.y + p.h / 2 - 1;
                POINT tri[3] = { { cx - 3, cy }, { cx + 3, cy }, { cx, cy + 4 } };
                HBRUSH br = CreateSolidBrush(RGB(0x66, 0x6c, 0x7d));
                HGDIOBJ ob = SelectObject(dc, br);
                HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
                Polygon(dc, tri, 3);
                SelectObject(dc, ob);
                SelectObject(dc, op2);
                DeleteObject(br);
            }
            continue;
        }

        if (p.kind == PieceKind::Label && !p.text.empty()) {
            std::wstring w = ToWide(p.text);
            HGDIOBJ of = SelectObject(dc, tools.blockFont);
            SetTextColor(dc, style.blockText);
            RECT r = { p.x, p.y, p.x + p.w + 2, p.y + p.h };
            DrawTextW(dc, w.c_str(), (int)w.size(), &r,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            SelectObject(dc, of);
        }
    }

    SetViewportOrgEx(dc, oldOrg.x, oldOrg.y, NULL);
}

} // namespace w2k
} // namespace nashi
