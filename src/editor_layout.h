// Included inside the editor's private namespace. All geometry is in device-independent pixels.
constexpr float StrokeSliderMax = 40;
Tool inspectorTool()
{
    return selected() ? app.document.items[app.document.selected].kind : app.tool;
}
float propertySize()
{
    if (textMode())
        return selected() ? app.document.items[app.document.selected].fontSize : app.fontSize;
    return selected() && inspectorTool() != Tool::Check
               ? app.document.items[app.document.selected].thickness
               : brushWidth();
}
float propertyOpacity()
{
    return selected() ? app.document.items[app.document.selected].opacity
                      : app.opacities[static_cast<size_t>(app.tool)];
}
std::array<int, 3> strokePresets()
{
    return textMode()        ? std::array<int, 3>{16, 24, 32}
           : highlightMode() ? std::array<int, 3>{12, 24, 40}
                             : std::array<int, 3>{2, 4, 6};
}
int inspectorStyleMenu()
{
    switch (inspectorTool())
    {
    case Tool::Circle:
    case Tool::Rectangle:
        return CircleStyleMenu;
    case Tool::Arrow:
        return ArrowStyleMenu;
    case Tool::Check:
        return CheckStyleMenu;
    case Tool::Line:
        return LineStyleMenu;
    default:
        return 0;
    }
}
const wchar_t *styleName(Tool tool, int style)
{
    static constexpr const wchar_t *circle[] = {L"Circle", L"Highlight circle", L"Dashed circle"};
    static constexpr const wchar_t *rectangle[] = {L"Square", L"Rounded square", L"Highlight box",
                                                   L"Filled box"};
    static constexpr const wchar_t *arrow[] = {L"Classic", L"Outlined", L"Curved gloss",
                                               L"Straight gloss", L"Block gloss"};
    static constexpr const wchar_t *check[] = {L"Boxed check", L"Circle badge",   L"Simple check",
                                               L"Boxed X",     L"Circle X badge", L"Simple X"};
    static constexpr const wchar_t *line[] = {L"Solid", L"Dashed", L"Dotted"};
    switch (tool)
    {
    case Tool::Circle:
        return circle[style % 3];
    case Tool::Rectangle:
        return rectangle[style % 4];
    case Tool::Arrow:
        return arrow[style % 5];
    case Tool::Check:
        return check[style % 6];
    case Tool::Line:
        return line[style % 3];
    default:
        return L"";
    }
}
struct InspectorLayout
{
    Rect panel, body, color, palette, size, presets, slider, styles, opacity, opacitySlider, help;
    float contentHeight = 0, maxScroll = 0;
};
InspectorLayout inspectorLayout()
{
    InspectorLayout l;
    const auto client = clientDips(), canvas = canvasRect();
    l.panel = {canvas.right, toolbarHeight(), client.right, client.bottom - StatusHeight};
    l.body = {l.panel.left, l.panel.top + 78, l.panel.right, l.panel.bottom};
    const float left = l.panel.left + 18, right = l.panel.right - 18;
    float y = 12;
    auto region = [&](Rect &r, float h) {
        r = {left, y, right, y + h};
        y += h;
    };
    region(l.color, 68);
    region(l.palette, 30.0f * ((static_cast<int>(app.palette.size()) + 2 + 7) / 8));
    y += 20;
    region(l.size, 30);
    if (inspectorTool() != Tool::Check)
    {
        region(l.presets, 34);
        y += 8;
        region(l.slider, 32);
    }
    y += 22;
    if (inspectorStyleMenu())
    {
        region(l.styles, 82);
        y += 22;
    }
    else if (textMode())
    {
        region(l.styles, 64);
        y += 22;
    }
    region(l.opacity, 30);
    region(l.opacitySlider, 32);
    y += 26;
    region(l.help, 100);
    l.contentHeight = y;
    l.maxScroll = std::max(0.0f, y - l.body.height());
    const float offset = l.body.top - std::clamp(app.inspectorScroll, 0.0f, l.maxScroll);
    for (Rect *r : {&l.color, &l.palette, &l.size, &l.presets, &l.slider, &l.styles, &l.opacity,
                    &l.opacitySlider, &l.help})
    {
        r->top += offset;
        r->bottom += offset;
    }
    return l;
}
bool railCommand(int id)
{
    return (id >= SelectTool && id <= LineTool) || id == CropTool || id == HighlightTool ||
           id == TextTool || id == EraserTool || id == AppMenu;
}
bool propertyCommand(int id)
{
    return paletteCommand(id) || id == CustomColor || id == Eyedropper || id == SizeDown ||
           id == SizeUp || id == StrokeSlider || id == OpacitySlider ||
           (id >= StrokePresetFirst && id <= StrokePresetThird) ||
           (id >= CircleStyleMenu && id <= LineStyleMenu) || id == TextBold || id == TextBox ||
           id == TextSizeMenu;
}
