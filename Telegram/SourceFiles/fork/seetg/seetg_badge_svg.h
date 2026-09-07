#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <QtCore/QXmlStreamReader>
namespace Fork::SeeTg::BadgeIcons {
inline constexpr auto MaxSvgBytes = 128 * 1024;
[[nodiscard]] inline bool ValidType(const QString &type) {
 if (type.isEmpty() || type.size() > 32) return false;
 for (const auto ch : type) {
  if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-') return false;
 }
 return true;
}
// Keep SVG self-contained: no external resources, scripts, entities or animation.
[[nodiscard]] inline bool ValidSvg(const QByteArray &svg) {
 if (svg.isEmpty() || svg.size() > MaxSvgBytes) return false;
 auto xml = QXmlStreamReader(svg);
 const auto tags = QStringList{ "svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline", "polygon", "defs", "linearGradient", "radialGradient", "stop", "clipPath", "image", "title", "desc" };
 auto elements = 0;
 auto depth = 0;
 while (!xml.atEnd()) {
  const auto token = xml.readNext();
  if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference || token == QXmlStreamReader::ProcessingInstruction) return false;
  if (xml.isEndElement()) --depth;
  if (!xml.isStartElement()) continue;
  if (++elements > 2048 || ++depth > 16 || !tags.contains(xml.name().toString())) return false;
  if (elements == 1 && xml.name() != u"svg") return false;
  for (const auto &attr : xml.attributes()) {
   const auto name = attr.name().toString().toLower();
   const auto value = attr.value().toString().trimmed();
   if (name.startsWith("on") || (name == "style" && value != "display: block;") || name == "base") return false;
   if (name == "href" && !value.startsWith('#') && !(xml.name() == u"image" && value.startsWith("data:image/png;base64,"))) return false;
   if (value.contains("url(", Qt::CaseInsensitive) && !value.startsWith("url(#")) return false;
  }
 }
 return !xml.hasError() && elements > 0 && depth == 0;
}
} // namespace Fork::SeeTg::BadgeIcons
