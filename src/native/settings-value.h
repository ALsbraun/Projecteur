// SPDX-License-Identifier: MIT
#pragma once

#include "platform.h"

#include <variant>

namespace native {

struct Color
{
  int r = -1;
  int g = -1;
  int b = -1;
  int a = 255;

  int red() const { return r; }
  int green() const { return g; }
  int blue() const { return b; }
  int alpha() const { return a; }
  bool isValid() const { return r >= 0 && g >= 0 && b >= 0; }
  QString name() const;
  static Color fromString(const QString& text);
  bool operator==(const Color& other) const
  { return std::tie(r, g, b, a) == std::tie(other.r, other.g, other.b, other.a); }
  bool operator!=(const Color& other) const { return !(*this == other); }
};

using Setting = std::variant<std::monostate, QString, bool, int, double, Color>;
}
