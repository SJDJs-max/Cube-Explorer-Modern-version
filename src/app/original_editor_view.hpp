#pragma once

#include "cube_canvas.hpp"
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <array>

// Cube editor canvas with embedded classic/2D/3D view switcher, compact brush
// swatch, and Auto-Fix toggle. This widget only renders raw facelets; its owner
// retains all editing and solver logic.
class OriginalEditorView final : public QWidget {
  public:
    explicit OriginalEditorView(QWidget *parent = nullptr);

    void setFacelets(std::string raw, const std::array<std::int8_t, 6> &centers);
    void setColors(const std::array<QColor, 7> &colors);
    void setColors(const std::array<QColor, 6> &colors);
    void setDarkMode(bool dark);
    // 0: Classic; 1: 2D Net; 2: 3D Cube.
    void setView(int view);
    [[nodiscard]] int view() const noexcept {
        return view_;
    }
    void setBrush(char brush);
    void setAutoFix(bool enabled);
    [[nodiscard]] bool autoFix() const;
    void retranslateUi();
    [[nodiscard]] CubeCanvas *canvas() const noexcept {
        return canvas_;
    }
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override {
        return true;
    }
    [[nodiscard]] int heightForWidth(int width) const override;

    std::function<void(int, Qt::MouseButton, Qt::KeyboardModifiers)> onSticker;
    std::function<void(bool)> onAutoFix;
    std::function<void(int)> onViewChanged;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    CubeCanvas *canvas_;
    QCheckBox *autoFix_;
    QLabel *brushCaption_;
    QLabel *brushSwatch_;
    QFrame *viewBar_;
    std::array<QToolButton *, 3> viewButtons_{};
    QToolButton *resetCamera_;
    int view_ = 0;
    char brush_ = 'U';
    bool dark_ = false;

    void arrangeChildren();
    void updateBrush();
    void updateIcons();
};
