#pragma once
#include "Shape.h"
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <vector>

// SDK bridge: Shape.h deliberately contains only allocation-free geometry/math.
namespace shape {
inline Microsoft::WRL::ComPtr<ID2D1PathGeometry> Geometry(ID2D1Factory *factory, const Path &outline) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (!outline.count || FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
        return {};
    auto transform =
        outline.reflected ? D2D1::Matrix3x2F::Scale(1, -1, {0, outline.axis}) : D2D1::Matrix3x2F::Identity();
    auto point = [&](Point value) { return transform.TransformPoint({value.x, value.y}); };
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(point(outline.start), D2D1_FIGURE_BEGIN_FILLED);
    for (unsigned i = 0; i < outline.count; ++i) {
        const auto &segment = outline.segments[i];
        if (segment.command == Command::Line)
            sink->AddLine(point(segment.end));
        else if (segment.command == Command::Cubic)
            sink->AddBezier(D2D1::BezierSegment(point(segment.first), point(segment.second), point(segment.end)));
        else {
            bool clockwise = segment.clockwise != outline.reflected;
            sink->AddArc(
                D2D1::ArcSegment(point(segment.end), {segment.radius, segment.radius}, 0,
                                 clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                 D2D1_ARC_SIZE_SMALL));
        }
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close()))
        return {};
    return path;
}
inline Microsoft::WRL::ComPtr<ID2D1PathGeometry> RoundedPath(ID2D1Factory *factory, Rect rect, float radius = Surface,
                                                             float smoothing = Smoothing) {
    return Geometry(factory, RoundedPath(rect, radius, smoothing));
}
inline Microsoft::WRL::ComPtr<ID2D1PathGeometry> Union(ID2D1Factory *factory, Rect pill, Rect extension,
                                                       Edge edge = Edge::Bottom) {
    return Geometry(factory, Union(pill, extension, edge));
}
class PolygonSink final : public ID2D1SimplifiedGeometrySink {
    LONG references_ = 1;
    bool failed_ = false;

  public:
    std::vector<D2D1_POINT_2F> points;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **output) override {
        if (!output)
            return E_POINTER;
        *output = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(ID2D1SimplifiedGeometrySink))
            return E_NOINTERFACE;
        *output = static_cast<ID2D1SimplifiedGeometrySink *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (!count)
            delete this;
        return count;
    }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override {
        if (!points.empty())
            failed_ = true;
        points.push_back(start);
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F *vertices, UINT count) override {
        points.insert(points.end(), vertices, vertices + count);
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT *, UINT) override { failed_ = true; }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) override {
        if (end != D2D1_FIGURE_END_CLOSED)
            failed_ = true;
    }
    HRESULT STDMETHODCALLTYPE Close() override { return failed_ ? E_FAIL : S_OK; }
};
inline std::vector<D2D1_POINT_2F> Polygon(ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F &transform,
                                          float tolerance = .25f) {
    if (!geometry)
        return {};
    Microsoft::WRL::ComPtr<PolygonSink> sink;
    sink.Attach(new PolygonSink);
    if (FAILED(geometry->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, &transform, tolerance, sink.Get())) ||
        FAILED(sink->Close()))
        return {};
    return std::move(sink->points);
}
inline HRGN Region(ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F &transform) {
    auto vertices = Polygon(geometry, transform);
    std::vector<POINT> points;
    for (auto vertex : vertices) {
        POINT point{static_cast<LONG>(std::lround(vertex.x)), static_cast<LONG>(std::lround(vertex.y))};
        if (points.empty() || point.x != points.back().x || point.y != points.back().y)
            points.push_back(point);
    }
    if (points.size() > 1 && points.front().x == points.back().x && points.front().y == points.back().y)
        points.pop_back();
    return points.size() < 3 ? CreateRectRgn(0, 0, 0, 0)
                             : CreatePolygonRgn(points.data(), static_cast<int>(points.size()), WINDING);
}
inline bool Contains(ID2D1Geometry *geometry, D2D1_POINT_2F point) {
    BOOL contains = FALSE;
    return geometry && SUCCEEDED(geometry->FillContainsPoint(point, nullptr, .25f, &contains)) && contains;
}
} // namespace shape
