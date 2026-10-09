#include <Arduino.h>
#include "UiIcons.h"
#include "UiIconShapes.h"
#include "Display.h"

void drawUiIcon(UiIcon icon, int x, int y, int size, uint16_t color, uint16_t accent, uint16_t background) {
  IconShape shape = uiIconShape(icon);
  auto px = [x, size](int v) { return x + iconScale(v, size); };
  auto py = [y, size](int v) { return y + iconScale(v, size); };
  auto len = [size](int v) { int s = iconScale(v, size); return s < 1 ? 1 : s; };
  for (uint8_t i = 0; i < shape.count; i++) {
    const IconStep& s = shape.steps[i];
    uint16_t c = s.flags & ICON_CUT ? background : s.flags & ICON_ALT ? accent : color;
    switch (s.op) {
      case IconOp::Line:
        tft.drawLine(px(s.a), py(s.b), px(s.c), py(s.d), c);
        break;
      case IconOp::Thick:
        tft.drawLine(px(s.a), py(s.b), px(s.c), py(s.d), c);
        tft.drawLine(px(s.a) + 1, py(s.b), px(s.c) + 1, py(s.d), c);
        if (size >= 28) tft.drawLine(px(s.a), py(s.b) + 1, px(s.c), py(s.d) + 1, c);
        break;
      case IconOp::Rect: tft.drawRect(px(s.a), py(s.b), len(s.c), len(s.d), c); break;
      case IconOp::FillRect: tft.fillRect(px(s.a), py(s.b), len(s.c), len(s.d), c); break;
      case IconOp::Circle: tft.drawCircle(px(s.a), py(s.b), len(s.c), c); break;
      case IconOp::FillCircle: tft.fillCircle(px(s.a), py(s.b), len(s.c), c); break;
      case IconOp::Triangle: tft.fillTriangle(px(s.a), py(s.b), px(s.c), py(s.d), px(s.e), py(s.f), c); break;
      case IconOp::RoundRect: tft.drawRoundRect(px(s.a), py(s.b), len(s.c), len(s.d), iconScale(s.e, size), c); break;
      case IconOp::FillRoundRect: tft.fillRoundRect(px(s.a), py(s.b), len(s.c), len(s.d), iconScale(s.e, size), c); break;
    }
  }
}
