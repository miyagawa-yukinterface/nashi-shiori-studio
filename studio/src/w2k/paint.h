// なしスタジオ（ネイティブ版）- ブロックを GDI で描く
//
// 置き場所は layout.cpp が決めます。ここは、決まったものを描くだけです。
//
// Windows 2000 でも動くように、GDI の古い関数しか使いません
// （GDI+ も、テーマも、Vista からの二重描きの仕掛けも使いません）。
// なめらかな角丸や影は出ないので、見た目は素朴になります。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "layout.h"

namespace nashi {
namespace w2k {

struct PaintStyle {
    COLORREF background = RGB(0xf5, 0xf6, 0xfa);
    COLORREF canvasLine = RGB(0xe2, 0xe5, 0xef);   // 方眼
    COLORREF blockText  = RGB(0xff, 0xff, 0xff);
    COLORREF slotFill   = RGB(0xff, 0xff, 0xff);
    COLORREF slotText   = RGB(0x22, 0x26, 0x33);
    COLORREF slotLine   = RGB(0x00, 0x00, 0x00);   // 実際は下地を暗くして使います
    int gridStep = 0;                              // 0 なら方眼を描かない
};

/** 文字を GDI で測る道具（layout に渡します）。 */
class GdiMeasurer : public TextMeasurer {
public:
    GdiMeasurer(HDC dc, HFONT font) : dc_(dc), font_(font) {}
    int Width(const std::string& utf8) const;
private:
    HDC dc_;
    HFONT font_;
};

/** 画面で使う持ちもの（フォントなど）。使い終わったら Free を呼びます。 */
struct PaintTools {
    HFONT blockFont = NULL;   // ブロックの文字
    HFONT slotFont = NULL;    // 欄の中の文字
    bool Create(int pointSize = 9);
    void Free();
};

/** 置き場所ぜんぶを dc に描きます。ox, oy は画面上のずらし幅です。 */
void PaintLayout(HDC dc, const Layout& lay, const PaintTools& tools,
                 const PaintStyle& style, int ox, int oy);

/** 下地（方眼）を塗ります。 */
void PaintBackground(HDC dc, const RECT& rc, const PaintStyle& style, int ox, int oy);

/** "#ffbf00" のような文字を COLORREF に。 */
COLORREF ColorFromHex(const char* hex, COLORREF fallback);
/** 明るく／暗く（amount は -1.0〜1.0）。 */
COLORREF Shade(COLORREF c, double amount);

} // namespace w2k
} // namespace nashi
