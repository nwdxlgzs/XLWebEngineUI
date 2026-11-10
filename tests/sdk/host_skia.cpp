// Model a host's independently linked Skia C++ method. SDK rendering must
// resolve its private implementation rather than this deliberately empty one.
struct SkRect;
class SkPaint;
class SkCanvas {
public:
    void drawRect(const SkRect&, const SkPaint&);
};
void SkCanvas::drawRect(const SkRect&, const SkPaint&) {}
