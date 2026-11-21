#pragma once

#include "cube/pattern.hpp"
#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QWidget>
#include <array>
#include <functional>
#include <string>
#include <vector>

// Presentation only. The facelet indices sent to the editor always refer to
// physical URFDLB faces, independently of the 3D camera orientation.
class CubeCanvas final : public QWidget {
public:
    enum class View { Classic, Net2D, Cube3D };
    explicit CubeCanvas(bool compact = false, QWidget* parent = nullptr);

    void setPattern(const cube::PatternState& pattern);
    void setFacelets(std::string facelets, const std::array<std::int8_t, 6>& logicalCenters);
    void setView(View view);
    [[nodiscard]] View view() const noexcept { return view_; }
    void setDarkMode(bool dark);
    void setColors(const std::array<QColor, 7>& colors);
    void setColors(const std::array<QColor, 6>& colors);
    [[nodiscard]] const std::array<QColor, 7>& colors() const noexcept { return colors_; }
    void setEditable(bool editable);
    void setShowFaceLabels(bool show);
    // Allows the original FacePaint host to align its overlays to source coordinates.
    // A negative value restores the normal editor/thumbnail margin.
    void setDrawingMargin(double pixels);
    void resetCamera();
    void setCamera(double yaw, double pitch, double zoom = 1.0);
    [[nodiscard]] double yaw() const noexcept { return yaw_; }
    [[nodiscard]] double pitch() const noexcept { return pitch_; }
    [[nodiscard]] double zoom() const noexcept { return zoom_; }

    // These use the same polygons as painting and are also used in UI tests.
    [[nodiscard]] int stickerAt(QPointF position) const;
    [[nodiscard]] QPolygonF stickerPolygon(int physicalIndex) const;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    std::function<void(int, Qt::MouseButton, Qt::KeyboardModifiers)> onSticker;
    std::function<void(Qt::MouseButton, Qt::KeyboardModifiers)> onBackground;
    std::function<void(double, double, double)> onCameraChanged;

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    struct Tile { int index; QPolygonF polygon; double depth = 0; double light = 1; };
    struct FaceBackdrop { QPolygonF polygon; double depth = 0; double light = 1; };
    std::string stickers_;
    std::array<std::int8_t, 6> logicalCenters_{};
    std::array<std::int8_t, 6> physicalCenters_{};
    std::array<QColor, 7> colors_;
    bool compact_ = false, editable_ = true, dark_ = false, labels_ = false;
    View view_ = View::Classic;
    double drawingMargin_ = -1;
    mutable bool geometryDirty_ = true;
    mutable std::vector<Tile> tiles_;
    mutable std::vector<FaceBackdrop> faces_;
    // TwistyPlayer's default Cube3D orbit; the camera has no roll.
    double yaw_ = 30.0, pitch_ = 35.0, zoom_ = 1.0;
    QPointF pressPosition_, previousPosition_;
    Qt::MouseButton pressedButton_ = Qt::NoButton;
    Qt::KeyboardModifiers pressedModifiers_ = Qt::NoModifier;
    bool dragging_ = false;
    int hovered_ = -1;

    void refreshCenterDirections();
    void invalidateGeometry();
    void ensureGeometry() const;
    void dispatchClick(QPointF position, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
};
