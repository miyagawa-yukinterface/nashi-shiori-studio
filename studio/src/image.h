// なしスタジオ - PNG 書き出しと簡易ラスタライザ
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace nashi {

struct Rgb {
    unsigned char r, g, b;
};

Rgb RgbFromHex(const std::string& hex, Rgb fallback);
Rgb Shade(Rgb c, double amount);   // +で明るく、-で暗く

class Canvas {
public:
    Canvas(int width, int height);

    void Blend(int x, int y, Rgb color, double alpha);
    void Fill(const std::function<bool(double, double)>& test, Rgb color, double alpha,
              double x0, double y0, double x1, double y1);

    void Ellipse(double cx, double cy, double rx, double ry, Rgb color, double alpha = 1.0);
    void RoundRect(double x, double y, double w, double h, double r, Rgb color, double alpha = 1.0);
    void Polygon(const std::vector<std::pair<double, double> >& pts, Rgb color, double alpha = 1.0);

    std::string ToPng() const;

private:
    int w_, h_;
    std::vector<unsigned char> buf_;   // RGBA
};

std::string EncodePng(int width, int height, const std::vector<unsigned char>& rgba);

// PNG のヘッダから大きさだけ読む。PNG でなければ false。
bool PngSize(const std::string& data, int* width, int* height);

} // namespace nashi
