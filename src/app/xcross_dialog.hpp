#pragma once
#include "app_common.hpp"
#include "editor_state.hpp"
#include "ui_i18n.hpp"
#include "ui_icons.hpp"
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <array>
#include <bit>
#include <set>

struct XCrossBuildResult {
    PatternState pattern;
    QString rotationText;
};

struct XCrossOrientationInfo {
    CubeState oriented;
    QString rotationText;
    int cU = 0, cR = 1, cF = 2, cD = 3, cL = 4, cB = 5;
    // Each slot's (bottomColorIdx, side1ColorIdx, side2ColorIdx) in order: 0=FR, 1=FL, 2=BR, 3=BL
    std::array<std::array<int, 3>, 4> slotColors{};
};

inline XCrossOrientationInfo orientCubeForXCross(const CubeState &input, int bottomFace) {
    const auto physicalFacelets = to_facelets(input);
    int currentPhysical = -1;
    for (int f = 0; f < 6; ++f)
        if (physicalFacelets.colors[f * 9 + 4] == bottomFace)
            currentPhysical = f;

    std::vector<Move> rotation;
    QString rotText;
    switch (currentPhysical) {
    case 0: // U -> D
        rotation = parse_moves("x2");
        rotText = "x2";
        break;
    case 1: // R -> D
        rotation = parse_moves("z");
        rotText = "z";
        break;
    case 2: // F -> D
        rotation = parse_moves("x'");
        rotText = "x'";
        break;
    case 3: // already on D
        rotText = "";
        break;
    case 4: // L -> D
        rotation = parse_moves("z'");
        rotText = "z'";
        break;
    case 5: // B -> D
        rotation = parse_moves("x");
        rotText = "x";
        break;
    default:
        break;
    }

    XCrossOrientationInfo info;
    info.oriented = rotation.empty() ? input : cube::apply(input, rotation);
    info.rotationText = rotText;
    const auto orientedFacelets = to_facelets(info.oriented);
    info.cU = orientedFacelets.colors[0 * 9 + 4];
    info.cR = orientedFacelets.colors[1 * 9 + 4];
    info.cF = orientedFacelets.colors[2 * 9 + 4];
    info.cD = orientedFacelets.colors[3 * 9 + 4];
    info.cL = orientedFacelets.colors[4 * 9 + 4];
    info.cB = orientedFacelets.colors[5 * 9 + 4];
    // 0=FR (D,F,R), 1=FL (D,F,L), 2=BR (D,B,R), 3=BL (D,B,L)
    info.slotColors = {{
        {info.cD, info.cF, info.cR},
        {info.cD, info.cF, info.cL},
        {info.cD, info.cB, info.cR},
        {info.cD, info.cB, info.cL},
    }};
    return info;
}

// Pure helper: rotates `input` so logical `bottomFace` (0=U,1=R,2=F,3=D,4=L,5=B)
// sits on physical D, then keeps only the 4 bottom cross edges plus any selected
// physical F2L slots (bit 0=FR, 1=FL, 2=BR, 3=BL; 0..4 slots for Cross/XC/XXC/XXXC/F2L).
inline XCrossBuildResult buildXCrossPattern(const CubeState &input, int bottomFace, int slotMask) {
    const auto orientInfo = orientCubeForXCross(input, bottomFace);
    const CubeState &oriented = orientInfo.oriented;
    const QString &rotText = orientInfo.rotationText;

    CubeState solvedOriented;
    solvedOriented.frame = oriented.frame;
    const auto solvedStr = format_facelets(to_facelets(solvedOriented));
    std::string mask(54, '?');
    for (int f = 0; f < 6; ++f)
        mask[f * 9 + 4] = solvedStr[f * 9 + 4];
    // Physical D-cross 4 edges: DF(28,25), DR(32,16), DB(34,52), DL(30,43)
    for (int idx : {28, 25, 32, 16, 34, 52, 30, 43})
        mask[idx] = solvedStr[idx];
    // Physical F2L slots: FR, FL, BR, BL
    constexpr std::array<std::array<int, 5>, 4> slotStickers = {{
        {29, 26, 15, 23, 12}, // FR
        {27, 44, 24, 21, 41}, // FL
        {35, 17, 51, 14, 48}, // BR
        {33, 53, 42, 39, 50}, // BL
    }};
    for (int s = 0; s < 4; ++s)
        if (slotMask & (1 << s))
            for (int idx : slotStickers[s])
                mask[idx] = solvedStr[idx];

    const PatternState targetMask = parse_pattern_facelets(mask);
    std::set<std::int8_t> keptCorners, keptEdges;
    for (auto id : targetMask.cp)
        if (id >= 0)
            keptCorners.insert(id);
    for (auto id : targetMask.ep)
        if (id >= 0)
            keptEdges.insert(id);

    PatternState result = pattern_from_cube(oriented, false);
    for (int i = 0; i < 8; ++i)
        if (!keptCorners.contains(result.cp[i])) {
            result.cp[i] = -1;
            result.co[i] = -1;
        }
    for (int i = 0; i < 12; ++i)
        if (!keptEdges.contains(result.ep[i])) {
            result.ep[i] = -1;
            result.eo[i] = -1;
        }
    const auto valid = validate_pattern(result);
    if (!valid)
        throw std::invalid_argument(valid.message);
    return {result, rotText};
}

class XCrossDialog final : public QDialog {
    std::array<QColor, 7> colors_;
    CubeState editorCube_;
    double scale_ = 1.0;
    int bottomFace_ = 0; // Default: U face (White)
    int slotMask_ = 0;   // Bit 0=FR, 1=FL, 2=BR, 3=BL (0..4 freely toggleable)
    bool openSolver_ = true;
    std::optional<XCrossBuildResult> built_;

    std::array<QPushButton *, 6> colorButtons_{};
    std::array<QPushButton *, 4> slotButtons_{};
    QRadioButton *useEditor_ = nullptr;
    QRadioButton *useScramble_ = nullptr;
    QLineEdit *scrambleInput_ = nullptr;
    QLabel *hintLabel_ = nullptr;
    QComboBox *preRotCombo_ = nullptr;

    static QString colorShortZh(int idx, const QColor &c) {
        int h = -1, s = 0, v = 0;
        c.getHsv(&h, &s, &v);
        if (s < 38)
            return v > 190 ? "白" : (v < 65 ? "黑" : "灰");
        if (h < 16 || h >= 340)
            return "红";
        if (h < 45)
            return "橙";
        if (h < 75)
            return "黄";
        if (h < 165)
            return "绿";
        if (h < 200)
            return "青";
        if (h < 265)
            return "蓝";
        if (h < 310)
            return "紫";
        static constexpr std::array<const char *, 6> fallback = {"白", "红", "绿", "黄", "橙", "蓝"};
        return QString::fromUtf8(fallback[std::clamp(idx, 0, 5)]);
    }

    static QString colorShortEn(int idx, const QColor &c) {
        int h = -1, s = 0, v = 0;
        c.getHsv(&h, &s, &v);
        if (s < 38)
            return v > 190 ? "W" : (v < 65 ? "K" : "Gr");
        if (h < 16 || h >= 340)
            return "R";
        if (h < 45)
            return "O";
        if (h < 75)
            return "Y";
        if (h < 165)
            return "G";
        if (h < 200)
            return "C";
        if (h < 265)
            return "B";
        if (h < 310)
            return "P";
        static constexpr std::array<const char *, 6> fallback = {"W", "R", "G", "Y", "O", "B"};
        return QString::fromUtf8(fallback[std::clamp(idx, 0, 5)]);
    }

    static QIcon makeSlotSwatchIcon(const QColor &cBottom, const QColor &cSide1,
                                    const QColor &cSide2, double scale) {
        const int box = std::max(11, qRound(13 * scale));
        const int gap1 = std::max(4, qRound(5 * scale));
        const int gap2 = std::max(2, qRound(2 * scale));
        const int pad = std::max(1, qRound(1 * scale));
        const int w = pad * 2 + box * 3 + gap1 + gap2;
        const int h = pad * 2 + box;

        QPixmap pix(w * 2, h * 2);
        pix.setDevicePixelRatio(2.0);
        pix.fill(Qt::transparent);

        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor borderCol(30, 41, 59, 175);
        p.setPen(QPen(borderCol, 1.0));

        const double r = std::max(2.0, 2.8 * scale);
        const int y = pad;
        const int x0 = pad;
        const int x1 = x0 + box + gap1;
        const int x2 = x1 + box + gap2;

        p.setBrush(cBottom);
        p.drawRoundedRect(QRectF(x0 + 0.5, y + 0.5, box - 1.0, box - 1.0), r, r);

        // Small separator dot between bottom color and the two adjacent side colors
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(120, 134, 156, 190));
        const double dotX = x0 + box + gap1 * 0.5;
        const double dotY = y + box * 0.5;
        p.drawEllipse(QPointF(dotX, dotY), 1.2 * scale, 1.2 * scale);

        p.setPen(QPen(borderCol, 1.0));
        p.setBrush(cSide1);
        p.drawRoundedRect(QRectF(x1 + 0.5, y + 0.5, box - 1.0, box - 1.0), r, r);

        p.setBrush(cSide2);
        p.drawRoundedRect(QRectF(x2 + 0.5, y + 0.5, box - 1.0, box - 1.0), r, r);
        p.end();
        return QIcon(pix);
    }

    CubeState currentReferenceCube() const {
        if (useScramble_ && useScramble_->isChecked() && scrambleInput_) {
            const auto text = scrambleInput_->text().trimmed();
            if (!text.isEmpty()) {
                try {
                    const auto moves = editor::parse_legacy_moves(text.toStdString());
                    return cube::apply(CubeState{}, moves);
                } catch (...) {
                    return CubeState{};
                }
            }
            return CubeState{};
        }
        return editorCube_;
    }

    void refreshUi() {
        const auto faceLetters = std::array<QString, 6>{"U", "R", "F", "D", "L", "B"};
        for (int i = 0; i < 6; ++i) {
            const bool active = (i == bottomFace_);
            const QColor c = colors_[i];
            const bool bright = c.redF() * 0.2126 + c.greenF() * 0.7152 + c.blueF() * 0.0722 > 0.5;
            const QString cName = uiEnglish() ? colorShortEn(i, c) : colorShortZh(i, c);
            colorButtons_[i]->setText(QString("%1 %2").arg(faceLetters[i], cName));
            colorButtons_[i]->setStyleSheet(
                QString("QPushButton { background:%1; color:%2; border:%3px solid %4; "
                        "border-radius:6px; font-weight:%5; padding:4px 6px; }")
                    .arg(c.name())
                    .arg(bright ? "#182433" : "#f8fafc")
                    .arg(active ? 2 : 1)
                    .arg(active ? "#2563eb" : "#8b9cb0")
                    .arg(active ? 700 : 500));
        }

        const auto orientInfo = orientCubeForXCross(currentReferenceCube(), bottomFace_);

        const int iconW = std::max(38, qRound(48 * scale_));
        const int iconH = std::max(14, qRound(16 * scale_));
        for (int s = 0; s < 4; ++s) {
            const bool checked = (slotMask_ & (1 << s)) != 0;
            const auto [cD, cS1, cS2] = orientInfo.slotColors[s];
            const QString bZh = colorShortZh(cD, colors_[cD]);
            const QString s1Zh = colorShortZh(cS1, colors_[cS1]);
            const QString s2Zh = colorShortZh(cS2, colors_[cS2]);
            const QString bEn = colorShortEn(cD, colors_[cD]);
            const QString s1En = colorShortEn(cS1, colors_[cS1]);
            const QString s2En = colorShortEn(cS2, colors_[cS2]);

            slotButtons_[s]->setChecked(checked);
            slotButtons_[s]->setIcon(
                makeSlotSwatchIcon(colors_[cD], colors_[cS1], colors_[cS2], scale_));
            slotButtons_[s]->setIconSize(QSize(iconW, iconH));
            slotButtons_[s]->setText(
                uiEnglish() ? QString("%1-%2%3 Pair").arg(bEn, s1En, s2En)
                            : QString("%1-%2%3 Pair").arg(bZh, s1Zh, s2Zh));
            slotButtons_[s]->setToolTip(
                uiEnglish()
                    ? QString("Target F2L Color Pair: %1-%2-%3 (Cross bottom color %1 stays on D face upon completion)")
                          .arg(bEn, s1En, s2En)
                    : QString("目标 F2L 颜色配对：%1-%2%3 Pair（底色 %1 始终在 D 面完成，配合起手预旋转不限初始握法）")
                          .arg(bZh, s1Zh, s2Zh));
            slotButtons_[s]->setStyleSheet(
                checked ? "QPushButton { border: 2px solid #2563eb; border-radius: 6px; "
                          "font-weight: 700; padding: 5px 8px; text-align: left; }"
                        : "QPushButton { border: 1px solid rgba(139,156,176,0.55); border-radius: 6px; "
                          "font-weight: 500; padding: 5px 8px; text-align: left; }");
        }

        if (hintLabel_) {
            const int count = std::popcount(static_cast<unsigned>(slotMask_));
            if (count == 0)
                hintLabel_->setText(
                    trUi("当前目标：Cross（仅底面十字）",
                         "Target: Cross (bottom cross only)"));
            else if (count == 1)
                hintLabel_->setText(trUi("当前目标：XCross（底面十字 + 1 组 F2L Pair）",
                                         "Target: XCross (Cross + 1 F2L pair)"));
            else if (count == 2)
                hintLabel_->setText(trUi("当前目标：XXCross（底面十字 + 2 组 F2L Pair）",
                                         "Target: XXCross (Cross + 2 F2L pairs)"));
            else if (count == 3)
                hintLabel_->setText(trUi("当前目标：XXXCross（底面十字 + 3 组 F2L Pair）",
                                         "Target: XXXCross (Cross + 3 F2L pairs)"));
            else
                hintLabel_->setText(trUi("当前目标：完整 F2L（底面十字 + 4 组 F2L Pair）",
                                         "Target: Full F2L (Cross + 4 F2L pairs)"));
        }
    }

    bool tryBuild() {
        try {
            CubeState source = editorCube_;
            if (useScramble_->isChecked()) {
                const auto text = scrambleInput_->text().trimmed();
                if (text.isEmpty()) {
                    QMessageBox::warning(this, windowTitle(),
                                         trUi("请输入打乱公式，或选择使用当前编辑器魔方。",
                                              "Enter a scramble or use the current editor cube."));
                    return false;
                }
                const auto moves = editor::parse_legacy_moves(text.toStdString());
                source = cube::apply(CubeState{}, moves);
            }
            built_ = buildXCrossPattern(source, bottomFace_, slotMask_);
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(this, windowTitle(), friendlyError(e.what()));
            return false;
        }
    }

  public:
    XCrossDialog(const CubeState &editorCube, const std::array<QColor, 7> &colors, double scale,
                 QWidget *parent = nullptr)
        : QDialog(parent), colors_(colors), editorCube_(editorCube), scale_(scale) {
        setWindowTitle(trUi("Cross 快速求解", "Cross Quick Solver"));
        setMinimumWidth(qRound(465 * scale));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(qRound(14 * scale), qRound(14 * scale), qRound(14 * scale),
                                   qRound(14 * scale));
        layout->setSpacing(qRound(10 * scale));

        // 1. Cross color selection
        auto *colorGroup = new QGroupBox(trUi("选择底色（Cross 面颜色）", "Bottom / Cross Color"));
        auto *colorRow = new QHBoxLayout(colorGroup);
        colorRow->setContentsMargins(qRound(8 * scale), qRound(14 * scale), qRound(8 * scale), qRound(8 * scale));
        colorRow->setSpacing(qRound(6 * scale));
        for (int i = 0; i < 6; ++i) {
            colorButtons_[i] = new QPushButton;
            colorButtons_[i]->setCursor(Qt::PointingHandCursor);
            colorButtons_[i]->setMinimumHeight(qRound(30 * scale));
            connect(colorButtons_[i], &QPushButton::clicked, this, [this, i] {
                bottomFace_ = i;
                refreshUi();
            });
            colorRow->addWidget(colorButtons_[i], 1);
        }
        layout->addWidget(colorGroup);

        // 2. Freely selectable F2L color pairs (0..4: Cross / XC / XXC / XXXC / F2L)
        auto *slotGroup = new QGroupBox(
            trUi("指定要完成的 F2L 颜色 Pair",
                 "Target F2L Color Pairs"));
        auto *slotLayout = new QVBoxLayout(slotGroup);
        slotLayout->setContentsMargins(qRound(8 * scale), qRound(14 * scale), qRound(8 * scale), qRound(8 * scale));
        slotLayout->setSpacing(qRound(8 * scale));

        auto *slotGrid = new QGridLayout;
        slotGrid->setSpacing(qRound(6 * scale));
        // Order: 0=FR (0,0), 1=FL (0,1), 2=BR (1,0), 3=BL (1,1)
        const std::array<std::pair<int, int>, 4> slotPos = {{{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
        for (int s = 0; s < 4; ++s) {
            slotButtons_[s] = new QPushButton;
            slotButtons_[s]->setCheckable(true);
            slotButtons_[s]->setCursor(Qt::PointingHandCursor);
            slotButtons_[s]->setMinimumHeight(qRound(32 * scale));
            connect(slotButtons_[s], &QPushButton::clicked, this, [this, s] {
                slotMask_ ^= (1 << s);
                refreshUi();
            });
            slotGrid->addWidget(slotButtons_[s], slotPos[s].first, slotPos[s].second);
        }
        slotLayout->addLayout(slotGrid);

        hintLabel_ = new QLabel;
        hintLabel_->setObjectName("smallMuted");
        hintLabel_->setWordWrap(true);
        slotLayout->addWidget(hintLabel_);

        auto *preRotRow = new QHBoxLayout;
        preRotRow->setSpacing(qRound(6 * scale));
        auto *preRotLbl = new QLabel(trUi("起手预旋转：", "Pre-Rotation:"));
        preRotCombo_ = new QComboBox;
        preRotCombo_->setObjectName("xcrossPreRotation");
        preRotCombo_->addItem(
            trUi("水平预旋转 y/y2/y'（4 种朝向）",
                 "Horizontal y/y2/y' (4 orientations)"));
        preRotCombo_->addItem(
            trUi("全空间预旋转 x/y/z（24 种朝向）",
                 "Full 24-orientation x/y/z (24 orientations)"));
        preRotCombo_->addItem(
            trUi("无预旋转",
                 "None"));
        preRotCombo_->setCurrentIndex(0);
        preRotCombo_->setToolTip(
            trUi("专为 Cross/XCross 设计：仅在所有转动之前尝试预旋转，后续不再转体，且 100% 保证求解完成后底色在 D 底面：\n"
                 "• 水平预旋转 y/y2/y'（4 种）：起手仅在水平面调整正面；\n"
                 "• 全空间预旋转 x/y/z（24 种）：支持高手起手 x/z 斜握 + 双层/中层转动（如 x' r U' r'），起手时底色中心无需在 D 面，做完时自动落回 D 底面！",
                 "Designed for Cross/XCross: tries initial pre-rotation before the first move only, and guarantees the bottom center finishes on physical D:\n"
                 "• Horizontal y/y2/y' (4): horizontal pre-rotation only;\n"
                 "• Full x/y/z (24): supports initial x/z grip + wide/slice turns (e.g. x' r U' r') that bring the bottom center onto D upon completion."));
        preRotRow->addWidget(preRotLbl);
        preRotRow->addWidget(preRotCombo_, 1);
        slotLayout->addLayout(preRotRow);
        layout->addWidget(slotGroup);

        // 3. Input source
        auto *sourceGroup = new QGroupBox(trUi("输入魔方状态", "Cube State Source"));
        auto *sourceLayout = new QVBoxLayout(sourceGroup);
        sourceLayout->setContentsMargins(qRound(8 * scale), qRound(14 * scale), qRound(8 * scale), qRound(8 * scale));
        sourceLayout->setSpacing(qRound(6 * scale));
        useEditor_ = new QRadioButton(trUi("使用当前编辑器中的魔方", "Use current editor cube"));
        useScramble_ = new QRadioButton(trUi("输入打乱公式（从复原态打乱）", "Enter scramble formula"));
        useEditor_->setChecked(true);
        scrambleInput_ = new QLineEdit;
        scrambleInput_->setPlaceholderText(
            trUi("例如：R U2 F' L D' B2 U R'", "e.g. R U2 F' L D' B2 U R'"));
        scrambleInput_->setEnabled(false);
        connect(useScramble_, &QRadioButton::toggled, this, [this](bool checked) {
            scrambleInput_->setEnabled(checked);
            refreshUi();
        });
        connect(scrambleInput_, &QLineEdit::textChanged, this, [this] { refreshUi(); });
        sourceLayout->addWidget(useEditor_);
        sourceLayout->addWidget(useScramble_);
        sourceLayout->addWidget(scrambleInput_);
        layout->addWidget(sourceGroup);

        // 4. Actions
        auto *actionRow = new QHBoxLayout;
        actionRow->setSpacing(qRound(8 * scale));
        auto *applyOnly = new QPushButton(trUi("仅载入编辑器", "Load to Editor"));
        auto *applySolve = new QPushButton(trUi("载入并打开公式求解", "Load & Open Solver"));
        applySolve->setObjectName("solveCurrent");
        applySolve->setIcon(uiIcon(UiIcon::Sparkles, palette().color(QPalette::ButtonText), qRound(15 * scale)));
        auto *cancel = new QPushButton(trUi("取消", "Cancel"));
        actionRow->addStretch();
        actionRow->addWidget(applyOnly);
        actionRow->addWidget(applySolve);
        actionRow->addWidget(cancel);
        layout->addLayout(actionRow);

        connect(applyOnly, &QPushButton::clicked, this, [this] {
            openSolver_ = false;
            if (tryBuild())
                accept();
        });
        connect(applySolve, &QPushButton::clicked, this, [this] {
            openSolver_ = true;
            if (tryBuild())
                accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        refreshUi();
    }

    const std::optional<XCrossBuildResult> &built() const {
        return built_;
    }
    bool openSolver() const {
        return openSolver_;
    }
    PatternPreRotation preRotationMode() const {
        if (!preRotCombo_)
            return PatternPreRotation::YOnly;
        const int idx = preRotCombo_->currentIndex();
        return idx == 0   ? PatternPreRotation::YOnly
               : idx == 1 ? PatternPreRotation::All24
                          : PatternPreRotation::None;
    }
    bool preRotateY() const {
        return preRotationMode() == PatternPreRotation::YOnly;
    }
    void selectBottomFace(int face) {
        bottomFace_ = std::clamp(face, 0, 5);
        refreshUi();
    }
    void toggleSlot(int s) {
        if (s >= 0 && s < 4) {
            slotMask_ ^= (1 << s);
            refreshUi();
        }
    }
    QString slotButtonText(int s) const {
        return (s >= 0 && s < 4 && slotButtons_[s]) ? slotButtons_[s]->text() : QString{};
    }
    bool hasSlotSwatchIcon(int s) const {
        return s >= 0 && s < 4 && slotButtons_[s] && !slotButtons_[s]->icon().isNull();
    }
};
