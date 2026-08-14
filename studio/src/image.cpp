#include "image.h"
#include "deflate.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace nashi {

Rgb RgbFromHex(const std::string& hex, Rgb fallback) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() != 6) return fallback;
    unsigned v = 0;
    for (size_t i = 0; i < 6; i++) {
        char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return fallback;
        v = v * 16 + d;
    }
    Rgb out;
    out.r = (unsigned char)((v >> 16) & 0xFF);
    out.g = (unsigned char)((v >> 8) & 0xFF);
    out.b = (unsigned char)(v & 0xFF);
    return out;
}

Rgb Shade(Rgb c, double amount) {
    auto f = [amount](unsigned char v) {
        double x = amount < 0 ? v * (1.0 + amount) : v + (255.0 - v) * amount;
        if (x < 0) x = 0;
        if (x > 255) x = 255;
        return (unsigned char)(x + 0.5);
    };
    Rgb out;
    out.r = f(c.r);
    out.g = f(c.g);
    out.b = f(c.b);
    return out;
}

Canvas::Canvas(int width, int height) : w_(width), h_(height) {
    buf_.assign((size_t)width * height * 4, 0);
}

void Canvas::Blend(int x, int y, Rgb color, double alpha) {
    if (alpha <= 0 || x < 0 || y < 0 || x >= w_ || y >= h_) return;
    size_t i = ((size_t)y * w_ + x) * 4;
    double da = buf_[i + 3] / 255.0;
    double na = alpha + da * (1.0 - alpha);
    if (na <= 0) return;
    buf_[i + 0] = (unsigned char)((color.r * alpha + buf_[i + 0] * da * (1 - alpha)) / na + 0.5);
    buf_[i + 1] = (unsigned char)((color.g * alpha + buf_[i + 1] * da * (1 - alpha)) / na + 0.5);
    buf_[i + 2] = (unsigned char)((color.b * alpha + buf_[i + 2] * da * (1 - alpha)) / na + 0.5);
    buf_[i + 3] = (unsigned char)(na * 255.0 + 0.5);
}

void Canvas::Fill(const std::function<bool(double, double)>& test, Rgb color, double alpha,
                  double bx0, double by0, double bx1, double by1) {
    int x0 = std::max(0, (int)std::floor(bx0));
    int y0 = std::max(0, (int)std::floor(by0));
    int x1 = std::min(w_, (int)std::ceil(bx1));
    int y1 = std::min(h_, (int)std::ceil(by1));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int hits = 0;
            for (int sy = 0; sy < 3; sy++) {
                for (int sx = 0; sx < 3; sx++) {
                    if (test(x + (sx + 0.5) / 3.0, y + (sy + 0.5) / 3.0)) hits++;
                }
            }
            if (hits) Blend(x, y, color, (hits / 9.0) * alpha);
        }
    }
}

void Canvas::Ellipse(double cx, double cy, double rx, double ry, Rgb color, double alpha) {
    if (rx <= 0 || ry <= 0) return;
    Fill([=](double x, double y) {
        double dx = (x - cx) / rx, dy = (y - cy) / ry;
        return dx * dx + dy * dy <= 1.0;
    }, color, alpha, cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1);
}

void Canvas::RoundRect(double x, double y, double w, double h, double r, Rgb color, double alpha) {
    Fill([=](double px, double py) {
        if (px < x || px > x + w || py < y || py > y + h) return false;
        double dx = std::max(std::max(x + r - px, 0.0), px - (x + w - r));
        double dy = std::max(std::max(y + r - py, 0.0), py - (y + h - r));
        return dx * dx + dy * dy <= r * r;
    }, color, alpha, x - 1, y - 1, x + w + 1, y + h + 1);
}

void Canvas::Polygon(const std::vector<std::pair<double, double> >& pts, Rgb color, double alpha) {
    if (pts.size() < 3) return;
    double minX = pts[0].first, maxX = pts[0].first;
    double minY = pts[0].second, maxY = pts[0].second;
    for (size_t i = 1; i < pts.size(); i++) {
        minX = std::min(minX, pts[i].first);
        maxX = std::max(maxX, pts[i].first);
        minY = std::min(minY, pts[i].second);
        maxY = std::max(maxY, pts[i].second);
    }
    Fill([&pts](double x, double y) {
        bool inside = false;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            double xi = pts[i].first, yi = pts[i].second;
            double xj = pts[j].first, yj = pts[j].second;
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
        }
        return inside;
    }, color, alpha, minX - 1, minY - 1, maxX + 1, maxY + 1);
}

// -------------------------------------------------------------- PNG 出力

static void PutU32(std::string& s, unsigned v) {
    s += (char)(unsigned char)((v >> 24) & 0xFF);
    s += (char)(unsigned char)((v >> 16) & 0xFF);
    s += (char)(unsigned char)((v >> 8) & 0xFF);
    s += (char)(unsigned char)(v & 0xFF);
}

static void PutChunk(std::string& out, const char* type, const std::string& data) {
    PutU32(out, (unsigned)data.size());
    std::string body(type);
    body += data;
    out += body;
    PutU32(out, Crc32((const unsigned char*)body.c_str(), body.size()));
}

std::string EncodePng(int width, int height, const std::vector<unsigned char>& rgba) {
    std::string ihdr;
    PutU32(ihdr, (unsigned)width);
    PutU32(ihdr, (unsigned)height);
    ihdr += (char)8;   // bit depth
    ihdr += (char)6;   // RGBA
    ihdr += (char)0;
    ihdr += (char)0;
    ihdr += (char)0;

    const size_t stride = (size_t)width * 4;
    std::vector<unsigned char> raw((stride + 1) * (size_t)height);
    for (int y = 0; y < height; y++) {
        size_t dst = (size_t)y * (stride + 1);
        // フィルタ 2 (Up) にすると、同じ色が続く絵がよく縮む
        raw[dst] = (unsigned char)(y == 0 ? 0 : 2);
        const unsigned char* cur = &rgba[(size_t)y * stride];
        const unsigned char* up = y == 0 ? NULL : &rgba[((size_t)y - 1) * stride];
        for (size_t x = 0; x < stride; x++) {
            raw[dst + 1 + x] = (unsigned char)(up ? (cur[x] - up[x]) : cur[x]);
        }
    }

    std::string out;
    const char sig[] = { (char)0x89, 'P', 'N', 'G', '\r', '\n', (char)0x1A, '\n' };
    out.append(sig, sizeof(sig));
    PutChunk(out, "IHDR", ihdr);
    PutChunk(out, "IDAT", ZlibCompress(raw.data(), raw.size()));
    PutChunk(out, "IEND", std::string());
    return out;
}

std::string Canvas::ToPng() const {
    return EncodePng(w_, h_, buf_);
}

bool PngSize(const std::string& data, int* width, int* height) {
    // 8バイトの署名 + 長さ(4) + "IHDR" + 幅(4) + 高さ(4)
    if (data.size() < 24) return false;
    const unsigned char* p = (const unsigned char*)data.data();
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    for (int i = 0; i < 8; i++) {
        if (p[i] != sig[i]) return false;
    }
    if (memcmp(p + 12, "IHDR", 4) != 0) return false;
    int w = (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
    int h = (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) return false;
    if (width) *width = w;
    if (height) *height = h;
    return true;
}

} // namespace nashi
