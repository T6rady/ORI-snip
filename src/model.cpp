#include "model.h"
#include <cstring>
#include <stdexcept>

namespace snip
{
std::vector<Point> chiselSegment(Point a, Point b, float width)
{
    std::vector<Point> candidates;
    candidates.reserve(8);
    for (Point center : {a, b})
        for (Point offset : {Point{-.3f, -.5f}, Point{.05f, -.5f},
                             Point{.3f, .5f}, Point{-.05f, .5f}})
            candidates.push_back(center + offset * width);
    std::sort(candidates.begin(), candidates.end(), [](Point x, Point y) {
        return x.x < y.x || (x.x == y.x && x.y < y.y);
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    auto cross = [](Point x, Point y, Point z) {
        auto u = y - x, v = z - x;
        return u.x * v.y - u.y * v.x;
    };
    std::vector<Point> hull;
    hull.reserve(9);
    for (Point p : candidates)
    {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
            hull.pop_back();
        hull.push_back(p);
    }
    const size_t lower = hull.size();
    for (auto i = candidates.rbegin() + 1; i != candidates.rend(); ++i)
    {
        while (hull.size() > lower && cross(hull[hull.size() - 2], hull.back(), *i) <= 0)
            hull.pop_back();
        hull.push_back(*i);
    }
    hull.pop_back();
    return hull;
}
float segmentDistance(Point p, Point a, Point b)
{
    Point v = b - a, w = p - a;
    float n = v.x * v.x + v.y * v.y;
    float t = n > 0 ? std::clamp((w.x * v.x + w.y * v.y) / n, 0.0f, 1.0f) : 0;
    return length(p - (a + v * t));
}
bool Annotation::flipCurvedArrow()
{
    if (kind != Tool::Arrow || style != 2 || length(b - a) < .01f)
        return false;
    curveFlipped = !curveFlipped;
    return true;
}
Point Annotation::arrowSpine(float t) const
{
    Point v = b - a;
    float len = length(v);
    if (style != 2 || len < .01f)
        return a + v * t;
    Point n{-v.y / len, v.x / len};
    return a + v * t - n * ((curveFlipped ? -1 : 1) * len * 1.2f * t * (1 - t));
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
    if (style == 4)
    {
        // Broad, constant-width shaft, square tail, and a straight triangular head.
        Point base = b - u * head;
        float headHalf = std::max(half * 2.1f, head * .60f);
        return {a + n * half, base + n * half, base + n * headHalf, b,
                base - n * headHalf, base - n * half, a - n * half};
    }
    float baseT = std::clamp(1 - head / len, .55f, .84f);
    Point base = arrowSpine(baseT), tipDirection = b - base;
    tipDirection = tipDirection * (1 / length(tipDirection));
    Point tipNormal{-tipDirection.y, tipDirection.x};
    float headHalf = std::max(half * 2.1f, length(b - base) * .60f);
    auto side = [&](int step, float sign) {
        float fraction = step / 48.0f, t = baseT * fraction;
        Point tangent = style == 2
            ? v - n * ((curveFlipped ? -1 : 1) * len * 1.2f * (1 - 2 * t)) : v;
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
    if ((kind != Tool::Pen && kind != Tool::Highlight) || points.empty())
        return rectangle(a, b);
    Rect r{points[0].x, points[0].y, points[0].x, points[0].y};
    for (auto p : points)
    {
        r.left = std::min(r.left, p.x);
        r.top = std::min(r.top, p.y);
        r.right = std::max(r.right, p.x);
        r.bottom = std::max(r.bottom, p.y);
    }
    if (kind == Tool::Highlight)
        return {r.left - thickness * .3f, r.top - thickness * .5f,
                r.right + thickness * .3f, r.bottom + thickness * .5f};
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
    if (kind == Tool::Highlight)
    {
        const float scale = std::min(to.width() / std::max(1.0f, from.width()),
                                     to.height() / std::max(1.0f, from.height()));
        const float oldWidth = thickness;
        thickness = std::max(.1f, oldWidth * scale);
        from = {from.left + oldWidth * .3f, from.top + oldWidth * .5f,
                from.right - oldWidth * .3f, from.bottom - oldWidth * .5f};
        to = {to.left + thickness * .3f, to.top + thickness * .5f,
              to.right - thickness * .3f, to.bottom - thickness * .5f};
    }
    if (kind == Tool::Text)
    {
        const float scale = std::min(to.width() / std::max(1.0f, from.width()),
                                     to.height() / std::max(1.0f, from.height()));
        fontSize = std::clamp(fontSize * scale, 8.0f, 144.0f);
        textWidth = std::max(1.0f, textWidth * scale);
    }
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
    if (kind == Tool::Highlight)
    {
        if (points.empty() || !bounds().contains(p, tol))
            return false;
        for (size_t i = 0; i < points.size(); ++i)
        {
            auto hull = chiselSegment(points[i ? i - 1 : 0], points[i], thickness);
            bool inside = false;
            for (size_t j = 0; j < hull.size(); ++j)
            {
                Point x = hull[j], y = hull[(j + 1) % hull.size()];
                if (segmentDistance(p, x, y) <= tol)
                    return true;
                if ((x.y > p.y) != (y.y > p.y) &&
                    p.x < (y.x - x.x) * (p.y - x.y) / (y.y - x.y) + x.x)
                    inside = !inside;
            }
            if (inside)
                return true;
        }
        return false;
    }
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
        pending_ = State{items, cropBounds};
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
        items = std::move(pending_->items);
        cropBounds = pending_->cropBounds;
        pending_.reset();
        selected = -1;
    }
}
bool Document::undo()
{
    if (pending_ || undo_.empty())
        return false;
    redo_.push_back({std::move(items), cropBounds});
    items = std::move(undo_.back().items);
    cropBounds = undo_.back().cropBounds;
    undo_.pop_back();
    selected = -1;
    return true;
}
bool Document::redo()
{
    if (pending_ || redo_.empty())
        return false;
    undo_.push_back({std::move(items), cropBounds});
    items = std::move(redo_.back().items);
    cropBounds = redo_.back().cropBounds;
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
bool Document::eraseAlong(Point from, Point to, float tolerance)
{
    if (items.empty())
        return false;
    // Sample the swept eraser closely enough to catch thin strokes even when
    // Windows delivers widely separated pointer events. Hit against the unchanged
    // list first, so one contact removes the topmost object rather than its layers.
    const int steps = static_cast<int>(std::ceil(length(to - from) / std::max(.25f, tolerance * .5f)));
    std::vector<bool> touched(items.size());
    std::vector<Rect> hitBounds;
    hitBounds.reserve(items.size());
    for (const auto &item : items)
    {
        auto r = item.bounds();
        const float extra = tolerance + (item.kind == Tool::Highlight ? 0 : item.thickness / 2) +
            (item.kind == Tool::Arrow ? std::max(12.0f, item.thickness * 3) : 0);
        hitBounds.push_back({r.left - extra, r.top - extra, r.right + extra, r.bottom + extra});
    }
    bool changed = false;
    for (int i = steps ? 1 : 0; i <= steps; ++i)
    {
        const Point p = steps ? from + (to - from) * (static_cast<float>(i) / steps) : from;
        for (int index = static_cast<int>(items.size()) - 1; index >= 0; --index)
            if (hitBounds[index].contains(p) && items[index].hit(p, tolerance))
            {
                touched[index] = true;
                changed = true;
                break;
            }
    }
    if (!changed)
        return false;
    size_t index = 0;
    std::erase_if(items, [&](const Annotation &) { return touched[index++]; });
    selected = -1;
    return true;
}
void Document::clear()
{
    items.clear();
    cropBounds.reset();
    selected = -1;
    pending_.reset();
    undo_.clear();
    redo_.clear();
}
float View::fittedScale(Rect viewport, Rect content, float nativeScale)
{
    return std::max(.0001f, std::min({std::max(1.0f, viewport.width()) / std::max(1.0f, content.width()),
                                    std::max(1.0f, viewport.height()) / std::max(1.0f, content.height()),
                                    nativeScale}));
}
void View::fitTo(Rect viewport, Rect content, float nativeScale)
{
    scale = fittedScale(viewport, content, nativeScale);
    origin = {viewport.left + (viewport.width() - content.width() * scale) / 2 - content.left * scale,
              viewport.top + (viewport.height() - content.height() * scale) / 2 - content.top * scale};
}
void View::constrain(Rect viewport, Rect content)
{
    auto axis = [&](float &position, float start, float end, float first, float last) {
        const float extent = (last - first) * scale;
        if (extent <= end - start + .001f)
            position = start + (end - start - extent) / 2 - first * scale;
        else
            position = std::clamp(position, end - last * scale, start - first * scale);
    };
    axis(origin.x, viewport.left, viewport.right, content.left, content.right);
    axis(origin.y, viewport.top, viewport.bottom, content.top, content.bottom);
}
void View::zoomAt(Point pointer, float nextScale, Rect viewport, Rect content)
{
    const Point focus = toImage(pointer);
    scale = nextScale;
    origin = pointer - focus * scale;
    constrain(viewport, content);
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
std::optional<Color> Bitmap::sample(Point point) const
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
        point.x >= width || point.y >= height)
        return std::nullopt;
    const size_t offset = (static_cast<size_t>(point.y) * width +
                           static_cast<size_t>(point.x)) * 4;
    if (offset + 3 >= pixels.size())
        return std::nullopt;
    return rgb(pixels[offset + 2], pixels[offset + 1], pixels[offset]);
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
    for (Point direction : {Point{160, 0}, Point{0, 160}, Point{-100, 80}, Point{1, 1}})
    {
        Annotation curved = arrow;
        curved.style = 2;
        curved.a = {100, 100};
        curved.b = curved.a + direction;
        const auto original = curved;
        require(curved.flipCurvedArrow());
        require(curved.a == original.a && curved.b == original.b);
        for (float t : {.2f, .5f, .8f})
        {
            require(length(curved.arrowSpine(t) + original.arrowSpine(t) -
                           (original.a + direction * t) * 2) < .001f);
            require(curved.hit(curved.arrowSpine(t), 0));
        }
        require(curved.flipCurvedArrow() && curved == original);
    }
    for (int style : {0, 1, 3, 4})
    {
        Annotation straight = arrow;
        straight.style = static_cast<uint8_t>(style);
        const auto unchanged = straight;
        require(!straight.flipCurvedArrow() && straight == unchanged);
    }
    Annotation other = arrow;
    other.kind = Tool::Line;
    other.style = 2;
    require(!other.flipCurvedArrow());
    other.kind = Tool::Arrow;
    other.b = other.a;
    require(!other.flipCurvedArrow());
    Annotation pen;
    pen.points = {{0, 0}, {10, 20}};
    pen.resize(pen.bounds(), {10, 10, 30, 50});
    require(pen.points[1].x == 30 && pen.points[1].y == 50);
    Annotation text;
    Annotation highlight;
    highlight.kind = Tool::Highlight;
    highlight.thickness = 20;
    highlight.points = {{50, 50}, {150, 50}};
    require(highlight.bounds().left == 44 && highlight.bounds().bottom == 60);
    require(highlight.hit({100, 59}, 0) && !highlight.hit({100, 62}, 0));
    require(!highlight.hit({43, 50}, 0));
    highlight.resize(highlight.bounds(), {88, 80, 312, 120});
    require(highlight.thickness == 40 && highlight.bounds().left == 88 &&
            highlight.bounds().right == 312 && highlight.bounds().bottom == 120);
    highlight.move({10, 15});
    require(highlight.hit({200, 115}, 0) && highlight.bounds().left == 98);
    text.kind = Tool::Text;
    text.a = {10, 10};
    text.b = {110, 40};
    text.text = L"Work note";
    require(text.hit({50, 20}, 0) && !text.hit({200, 80}, 0));
    text.resize(text.bounds(), {20, 20, 220, 80});
    require(text.fontSize == 48 && text.textWidth == 1200 && text.a.x == 20);
    text.move({15, 25});
    require(text.a.x == 35 && text.a.y == 45 && text.text == L"Work note");
    Document d;
    {
        Document erased;
        Annotation oldStroke;
        oldStroke.points = {{10, 40}, {100, 40}, {180, 80}};
        Annotation kept = oldStroke;
        kept.move({0, 80});
        erased.items = {oldStroke, kept};
        erased.begin();
        require(erased.eraseAlong({60, 40}, {60, 40}, 2) && erased.items == std::vector<Annotation>{kept});
        erased.commit();
        require(erased.undo() && erased.items == std::vector<Annotation>{oldStroke, kept});
        require(erased.redo() && erased.items == std::vector<Annotation>{kept});
        erased.begin();
        require(erased.eraseAlong({60, 120}, {60, 120}, 2));
        erased.cancel();
        require(erased.items == std::vector<Annotation>{kept});
        // A fast sweep must hit a thin line between otherwise empty endpoints.
        Annotation crossing;
        crossing.kind = Tool::Line;
        crossing.a = {45, 0};
        crossing.b = {45, 80};
        crossing.thickness = 1;
        erased.items = {crossing, kept};
        require(erased.eraseAlong({0, 40}, {100, 40}, 2) && erased.items == std::vector<Annotation>{kept});
        // One contact uses drawing order and does not peel off multiple layers.
        Annotation newest = oldStroke;
        newest.color = rgb(1, 2, 3);
        erased.items = {oldStroke, newest};
        require(erased.eraseAlong({60, 40}, {60, 40}, 2) && erased.items == std::vector<Annotation>{oldStroke});
    }
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
    const auto beforeCrop = d.items;
    d.begin();
    d.cropBounds = Rect{10, 20, 110, 120};
    for (auto &item : d.items) item.move({-10, -20});
    const auto afterCrop = d.items;
    d.commit();
    require(d.undo() && !d.cropBounds && d.items == beforeCrop);
    require(d.redo() && d.cropBounds == Rect{10, 20, 110, 120} && d.items == afterCrop);
    d.begin();
    d.cropBounds = Rect{20, 30, 50, 70};
    d.cancel();
    require(d.cropBounds == Rect{10, 20, 110, 120});
    d.clear();
    require(!d.cropBounds && !d.canUndo());
    View v{.375f, {-1440, 220}};
    Point p{230, 1024};
    Point q = v.toImage(v.toScreen(p));
    require(length(q - p) < .001f);
    const Rect viewport{20, 100, 1020, 600}, content{-20, -20, 2020, 1520};
    v.fitTo(viewport, content, 1);
    require(std::abs(v.scale - 500.0f / 1540) < .00001f);
    require(length(v.toScreen({1000, 750}) - Point{520, 350}) < .001f);
    v.zoomAt({520, 350}, 2, viewport, content);
    const Point anchor{420, 300};
    const auto focus = v.toImage(anchor);
    v.zoomAt(anchor, 3, viewport, content);
    require(length(v.toImage(anchor) - focus) < .001f);
    v.origin = {100000, -100000};
    v.constrain(viewport, content);
    require(length(v.origin - Point{80, -3960}) < .001f);
    v.scale = .1f;
    v.constrain(viewport, content);
    require(length(v.toScreen({1000, 750}) - Point{520, 350}) < .001f);
    v.fitTo(viewport, {0, 0, 100, 80}, .5f);
    require(v.scale == .5f); // Small snips stay at native size at the fitted minimum.
    auto bitmap = Bitmap::create(8, 6);
    for (size_t i = 0; i < bitmap.pixels.size(); ++i)
        bitmap.pixels[i] = static_cast<uint8_t>(i);
    auto cropped = bitmap.crop(2, 1, 3, 2);
    require(cropped.pixels[0] == bitmap.pixels[40] && cropped.pixels[12] == bitmap.pixels[72]);
    require(bitmap.sample({2.9f, 1.2f}) == rgb(42, 41, 40));
    require(bitmap.sample({7.99f, 5.99f}) == rgb(190, 189, 188));
    require(!bitmap.sample({-0.01f, 0}) && !bitmap.sample({8, 0}) &&
            !bitmap.sample({0, 6}) && !Bitmap{}.sample({0, 0}));
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
