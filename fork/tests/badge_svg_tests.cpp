#include "fork/seetg/seetg_badge_svg.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <iostream>
using namespace Fork::SeeTg;
int main(int argc, char **argv) {
 QCoreApplication app(argc, argv);
 const auto dir = QDir(QString::fromUtf8(BADGE_RESOURCE_DIR));
 int count = 0;
 for (const auto &name : dir.entryList({"*.svg"})) {
  QFile file(dir.filePath(name)); if (!file.open(QIODevice::ReadOnly)) return 4;
  const auto svg = file.readAll();
  if (!BadgeIcons::ValidSvg(svg)) { std::cerr << "Rejected " << name.toStdString(); return 1; }
  ++count;
 }
 for (const auto &svg : {
  QByteArray("<svg><script/></svg>"), QByteArray("<!DOCTYPE svg><svg/>"),
  QByteArray("<svg><image href=\"https://example.com/a.png\"/></svg>"),
  QByteArray("<svg onload=\"run()\"/>"), QByteArray("<svg><animate/></svg>"),
  QByteArray("<svg><path fill=\"url(https://example.com/a)\"/></svg>"),
  QByteArray("<svg><image href=\"file:///tmp/a\"/></svg>"),
  QByteArray("<svg>"), QByteArray(BadgeIcons::MaxSvgBytes+1, ' '),
 }) { if (BadgeIcons::ValidSvg(svg)) return 2; }
 if (BadgeIcons::ValidType("../beta") || BadgeIcons::ValidType("https://host") || !BadgeIcons::ValidType("new_badge")) return 3;
 std::cout << count << " SVG assets and invalid-input cases passed\n";
}
