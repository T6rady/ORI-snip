#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace snip
{
struct Point
{
    float x = 0, y = 0;
};
inline Point operator+(Point a, Point b)
{
    return {a.x + b.x, a.y + b.y};
}
inline Point operator-(Point a, Point b)
{
    return {a.x - b.x, a.y - b.y};
}
inline Point operator*(Point a, float k)
{
    return {a.x * k, a.y * k};
}
inline float length(Point a)
{
    return std::hypot(a.x, a.y);
}
struct Rect
{
    float left = 0, top = 0, right = 0, bottom = 0;
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    bool contains(Point p, float margin = 0) const
    {
        return p.x >= left - margin && p.x <= right + margin && p.y >= top - margin &&
               p.y <= bottom + margin;
    }
};
inline Rect rectangle(Point a, Point b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
enum class Tool
{
    Select,
    Pen,
    Circle,
    Arrow,
    Check
};
using Color = uint32_t;
constexpr Color rgb(unsigned r, unsigned g, unsigned b)
{
    return r | (g << 8) | (b << 16);
}
struct Annotation
{
    Tool kind = Tool::Pen;
    Color color = rgb(239, 68, 68);
    float thickness = 4;
    Point a, b;
    std::vector<Point> points;
    Rect bounds() const;
    void move(Point delta);
    void resize(Rect from, Rect to);
    bool hit(Point p, float tolerance) const;
};
class Document
{
  public:
    std::vector<Annotation> items;
    int selected = -1;
    void begin();
    void commit();
    void cancel();
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool editing() const { return pending_.has_value(); }
    int hit(Point point, float tolerance) const;
    void clear();

  private:
    using State = std::vector<Annotation>;
    std::optional<State> pending_;
    std::vector<State> undo_, redo_;
};
struct Bitmap
{
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // top-down, BGRA, opaque screenshots
    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    static Bitmap create(int width, int height);
    Bitmap crop(int x, int y, int width, int height) const;
};
struct View
{
    float scale = 1;
    Point origin;
    Point toImage(Point screen) const { return (screen - origin) * (1 / scale); }
    Point toScreen(Point image) const { return origin + image * scale; }
};
float segmentDistance(Point p, Point a, Point b);
void runModelTests();
} // namespace snip
