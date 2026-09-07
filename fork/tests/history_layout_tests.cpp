#include "fork/seetg/seetg_history_layout.h"
#include <cstdio>
#include <cstdlib>

void Check(bool value) {
    if (!value) std::abort();
}

int main() {
    using Fork::SeeTg::History::ComputeRowGeometry;
    int cases = 0;
    for (const auto scale : { 100, 125, 150, 175, 200, 250, 300 }) {
        const auto px = [=](int value) { return (value * scale + 50) / 100; };
        for (const auto width : { 220, 280, 320, 380, 480, 640, 900 }) {
            for (const auto lines : { 2, 4, 6 }) {
                const auto textHeight = px(22) + (lines - 1) * px(26);
                const auto g = ComputeRowGeometry(width, px(120), px(64), px(160),
                    textHeight, px(10), px(14), px(10), px(5));
                Check(g.block.contains(g.art));
                Check(g.block.contains(g.text));
                Check(!g.art.intersects(g.text));
                Check(g.art.width() == g.art.height());
                Check(g.text.width() > 0 && g.text.height() >= textHeight);
                Check(g.block.left() >= 0 && g.block.right() < width);
                Check(g.block.bottom() < g.height);
                Check(g.art.width() <= px(120));
                ++cases;
            }
        }
    }
    const auto normal = ComputeRowGeometry(400, 120, 64, 160, 100, 10, 14, 10, 5);
    Check(normal.art.right() < normal.text.left());
    const auto narrow = ComputeRowGeometry(320, 240, 128, 320, 200, 20, 28, 20, 10);
    Check(narrow.art.bottom() < narrow.text.top());
    std::printf("PASS: %d layouts at 100–300%% scale, including narrow cards\n", cases);
}
