#include "model.h"
#include <cstring>
#include <stdexcept>

namespace snip
{
float segmentDistance(Point p, Point a, Point b)
{
    Point v = b - a, w = p - a;
    float n = v.x * v.x + v.y * v.y;
    float t = n > 0 ? std::clamp((w.x * v.x + w.y * v.y) / n, 0.0f, 1.0f) : 0;
    return length(p - (a + v * t));
}
Point Annotation::arrowSpine(float t) const
{
    Point v = b - a;
    float len = length(v);
    if (style != 2 || len < .01f)
        return a + v * t;
    Point n{-v.y / len, v.x / len};
    return a + v * t - n * (len * 1.2f * t * (1 - t));
}
std::vector<Point> Annotation::arrowContour() const
{
    Point v = b - a;
    float len = length(v);
    if (len < .01f || kind != Tool::Arrow || style == 0)
        return {};
    Point u = v * (1 / len), n{-u.y, u.x};
    float half = std::min(len * .09f, std::max(3.5f, thickness * 1.4f));
    float head = std::min(len * .45f, std::max({22.0f, thickness * 6, len * .18f}));
    if (style == 1)
    {
        Point base = b - u * head;
        float headHalf = std::max(half * 2.5f, head * .55f);
        return {a + n * half, base + n * half, base + n * headHalf, b,
                base - n * headHalf, base - n * half, a - n * half};
    }
    half = std::min(len * .10f, std::max(thickness * 2.2f, len * .055f));
    float baseT = std::clamp(1 - head / len, .55f, .84f);
    Point base = arrowSpine(baseT), tipDirection = b - base;
    tipDirection = tipDirection * (1 / length(tipDirection));
    Point tipNormal{-tipDirection.y, tipDirection.x};
    float headHalf = std::max(half * 2.1f, length(b - base) * .60f);
    auto side = [&](int step, float sign) {
        float fraction = step / 48.0f, t = baseT * fraction;
        Point tangent = v - n * (len * 1.2f * (1 - 2 * t));
        tangent = tangent * (1 / length(tangent));
        Point normal{-tangent.y, tangent.x};
        if (step == 48)
            normal = tipNormal;
        return arrowSpine(t) + normal * (sign * half * fraction);
    };
    std::vector<Point> outline{a};
    for (int i = 1; i <= 48; ++i)
        outline.push_back(side(i, 1));
    outline.push_back(base + tipNormal * headHalf);
    outline.push_back(b);
    outline.push_back(base - tipNormal * headHalf);
    for (int i = 48; i >= 1; --i)
        outline.push_back(side(i, -1));
    return outline;
}
Rect Annotation::bounds() const
{
    if (kind == Tool::Arrow && style != 0)
    {
        auto outline = arrowContour();
        if (!outline.empty())
        {
            Rect r{outline[0].x, outline[0].y, outline[0].x, outline[0].y};
            for (auto p : outline)
            {
                r.left = std::min(r.left, p.x);
                r.top = std::min(r.top, p.y);
                r.right = std::max(r.right, p.x);
                r.bottom = std::max(r.bottom, p.y);
            }
            return r;
        }
    }
    if (kind != Tool::Pen || points.empty())
        return rectangle(a, b);
    Rect r{points[0].x, points[0].y, points[0].x, points[0].y};
    for (auto p : points)
    {
        r.left = std::min(r.left, p.x);
        r.top = std::min(r.top, p.y);
        r.right = std::max(r.right, p.x);
        r.bottom = std::max(r.bottom, p.y);
    }
    return r;
}
void Annotation::move(Point d)
{
    a = a + d;
    b = b + d;
    for (auto &p : points)
        p = p + d;
}
void Annotation::resize(Rect from, Rect to)
{
    auto transform = [&](Point p) -> Point {
        float x = from.width() > .001f ? (p.x - from.left) / from.width() : .5f;
        float y = from.height() > .001f ? (p.y - from.top) / from.height() : .5f;
        return {to.left + x * to.width(), to.top + y * to.height()};
    };
    a = transform(a);
    b = transform(b);
    for (auto &p : points)
        p = transform(p);
}
bool Annotation::hit(Point p, float tol) const
{
    tol += thickness / 2;
    if (!bounds().contains(p, tol + (kind == Tool::Arrow ? std::max(12.0f, thickness * 3) : 0)))
        return false;
    if (kind == Tool::Pen)
    {
        if (points.size() == 1)
            return length(p - points[0]) <= tol;
        for (size_t i = 1; i < points.size(); ++i)
            if (segmentDistance(p, points[i - 1], points[i]) <= tol)
                return true;
        return false;
    }
    if (kind == Tool::Line)
        return segmentDistance(p, a, b) <= tol;
    if (kind == Tool::Arrow && style != 0)
    {
        auto outline = arrowContour();
        bool inside = false;
        for (size_t i = 0; i < outline.size(); ++i)
        {
            Point x = outline[i], y = outline[(i + 1) % outline.size()];
            if (segmentDistance(p, x, y) <= tol)
                return true;
            if ((x.y > p.y) != (y.y > p.y) &&
                p.x < (y.x - x.x) * (p.y - x.y) / (y.y - x.y) + x.x)
                inside = !inside;
        }
        return inside;
    }
    if (kind == Tool::Arrow)
    {
        if (segmentDistance(p, a, b) <= tol)
            return true;
        Point v = b - a;
        float len = length(v);
        if (len < .01f)
            return length(p - a) <= tol;
        Point u = v * (1 / len), n{-u.y, u.x};
        float head = std::min(len * .45f, std::max(12.0f, thickness * 3));
        const float headHalfWidth = head * .5f;
        return segmentDistance(p, b, b - u * head + n * headHalfWidth) <= tol ||
               segmentDistance(p, b, b - u * head - n * headHalfWidth) <= tol;
    }
    if (kind == Tool::Circle)
    {
        auto r = bounds();
        float rx = std::max(.5f, r.width() / 2), ry = std::max(.5f, r.height() / 2);
        float x = (p.x - (r.left + rx)) / rx, y = (p.y - (r.top + ry)) / ry;
        return x * x + y * y <= std::pow(1 + tol / std::min(rx, ry), 2);
    }
    return true;
}
void Document::begin()
{
    if (!pending_)
        pending_ = items;
}
void Document::commit()
{
    if (!pending_)
        return;
    if (undo_.size() >= 50)
        undo_.erase(undo_.begin());
    undo_.push_back(std::move(*pending_));
    pending_.reset();
    redo_.clear();
}
void Document::cancel()
{
    if (pending_)
    {
        items = std::move(*pending_);
        pending_.reset();
        selected = -1;
    }
}
bool Document::undo()
{
    if (pending_ || undo_.empty())
        return false;
    redo_.push_back(std::move(items));
    items = std::move(undo_.back());
    undo_.pop_back();
    selected = -1;
    return true;
}
bool Document::redo()
{
    if (pending_ || redo_.empty())
        return false;
    undo_.push_back(std::move(items));
    items = std::move(redo_.back());
    redo_.pop_back();
    selected = -1;
    return true;
}
int Document::hit(Point p, float tolerance) const
{
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i)
        if (items[i].hit(p, tolerance))
            return i;
    return -1;
}
void Document::clear()
{
    items.clear();
    selected = -1;
    pending_.reset();
    undo_.clear();
    redo_.clear();
}
Bitmap Bitmap::create(int w, int h)
{
    if (w <= 0 || h <= 0 || static_cast<uint64_t>(w) * h > 128000000)
        throw std::runtime_error("Image dimensions are invalid or too large.");
    Bitmap result;
    result.width = w;
    result.height = h;
    result.pixels.resize(static_cast<size_t>(w) * h * 4);
    return result;
}
Bitmap Bitmap::crop(int x, int y, int w, int h) const
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x > width - w || y > height - h)
        throw std::runtime_error("Capture rectangle is outside the screen.");
    auto result = create(w, h);
    for (int row = 0; row < h; ++row)
        std::memcpy(result.pixels.data() + static_cast<size_t>(row) * w * 4,
                    pixels.data() + (static_cast<size_t>(row + y) * width + x) * 4,
                    static_cast<size_t>(w) * 4);
    return result;
}
void runModelTests()
{
    auto require = [](bool ok) {
        if (!ok)
            throw std::runtime_error("Model self-test failed.");
    };
    Annotation arrow;
    arrow.kind = Tool::Arrow;
    arrow.a = {10, 10};
    arrow.b = {100, 100};
    require(arrow.hit({50, 50}, 2));
    require(!arrow.hit({10, 90}, 2));
    arrow.move({-20, 30});
    require(arrow.a.x == -10 && arrow.b.y == 130);
    Annotation pen;
    pen.points = {{0, 0}, {10, 20}};
    pen.resize(pen.bounds(), {10, 10, 30, 50});
    require(pen.points[1].x == 30 && pen.points[1].y == 50);
    Document d;
    d.begin();
    d.items.push_back(arrow);
    d.commit();
    require(d.undo() && d.items.empty());
    require(d.redo() && d.items.size() == 1);
    d.begin();
    d.items[0].move({10, 10});
    d.cancel();
    require(d.items[0].a.x == -10);
    d.begin();
    d.items.clear();
    d.commit();
    require(d.undo() && d.items.size() == 1);
    d.begin();
    d.items.push_back(pen);
    d.commit();
    require(!d.canRedo());
    View v{.375f, {-1440, 220}};
    Point p{230, 1024};
    Point q = v.toImage(v.toScreen(p));
    require(length(q - p) < .001f);
    auto bitmap = Bitmap::create(8, 6);
    for (size_t i = 0; i < bitmap.pixels.size(); ++i)
        bitmap.pixels[i] = static_cast<uint8_t>(i);
    auto cropped = bitmap.crop(2, 1, 3, 2);
    require(cropped.pixels[0] == bitmap.pixels[40] && cropped.pixels[12] == bitmap.pixels[72]);
    bool rejected = false;
    try
    {
        bitmap.crop(-1, 0, 2, 2);
    }
    catch (...)
    {
        rejected = true;
    }
    require(rejected);
}
} // namespace snip
