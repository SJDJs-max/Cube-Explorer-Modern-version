#pragma once
#include "app_common.hpp"
#include "editor_state.hpp"
#include "ui_i18n.hpp"
#include "ui_icons.hpp"
#include <QColor>
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

// Pure helper: rotates `input` so logical `bottomFace` (0=U,1=R,2=F,3=D,4=L,5=B)
// sits on physical D, then keeps only the 4 bottom cross edges plus any selected
// physical F2L slots (bit 0=FR, 1=FL, 2=BR, 3=BL; 0..4 slots for Cross/XC/XXC/XXXC/F2L).
inline XCrossBuildResult buildXCrossPattern(const CubeState &input, int bottomFace, int slotMask) {
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

    const CubeState oriented = rotation.empty() ? input : cube::apply(input, rotation);

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
    int bottomFace_ = 3; // Default: D face
    int slotMask_ = 0;   // Bit 0=FR, 1=FL, 2=BR, 3=BL (0..4 freely toggleable)
    bool openSolver_ = true;
    std::optional<XCrossBuildResult> built_;

    std::array<QPushButton *, 6> colorButtons_{};
    std::array<QPushButton *, 4> slotButtons_{};
    QRadioButton *useEditor_ = nullptr;
    QRadioButton *useScramble_ = nullptr;
    QLineEdit *scrambleInput_ = nullptr;
    QLabel *hintLabel_ = nullptr;

    void refreshUi() {
        const auto faceNamesZh = std::array<QString, 6>{"U 顶", "R 右", "F 前", "D 底", "L 左", "B 后"};
        const auto faceNamesEn = std::array<QString, 6>{"U", "R", "F", "D", "L", "B"};
        for (int i = 0; i < 6; ++i) {
            const bool active = (i == bottomFace_);
            const QColor c = colors_[i];
            const bool bright = c.redF() * 0.2126 + c.greenF() * 0.7152 + c.blueF() * 0.0722 > 0.5;
            colorButtons_[i]->setText(uiEnglish() ? faceNamesEn[i] : faceNamesZh[i]);
            colorButtons_[i]->setStyleSheet(
                QString("QPushButton { background:%1; color:%2; border:%3px solid %4; "
                        "border-radius:6px; font-weight:%5; padding:4px 6px; }")
                    .arg(c.name())
                    .arg(bright ? "#182433" : "#f8fafc")
                    .arg(active ? 2 : 1)
                    .arg(active ? "#2563eb" : "#8b9cb0")
                    .arg(active ? 700 : 500));
        }

        for (int s = 0; s < 4; ++s)
            slotButtons_[s]->setChecked((slotMask_ & (1 << s)) != 0);

        if (hintLabel_) {
            const int count = std::popcount(static_cast<unsigned>(slotMask_));
            if (count == 0)
                hintLabel_->setText(
                    trUi("当前目标：Cross（仅底面十字，未勾选 F2L 槽位）；自动关闭模 U 以便精确对齐中心。",
                         "Target: Cross (bottom cross only, 0 F2L slots); disables Mod-U for exact center alignment."));
            else if (count == 1)
                hintLabel_->setText(trUi("当前目标：XCross（底面十字 + 1 组 F2L 槽位）；自动关闭模 U。",
                                         "Target: XCross (Cross + 1 F2L slot); disables Mod-U."));
            else if (count == 2)
                hintLabel_->setText(trUi("当前目标：XXCross（底面十字 + 2 组 F2L 槽位）；自动关闭模 U。",
                                         "Target: XXCross (Cross + 2 F2L slots); disables Mod-U."));
            else if (count == 3)
                hintLabel_->setText(trUi("当前目标：XXXCross（底面十字 + 3 组 F2L 槽位）；自动关闭模 U。",
                                         "Target: XXXCross (Cross + 3 F2L slots); disables Mod-U."));
            else
                hintLabel_->setText(trUi("当前目标：完整 F2L（底面十字 + 全部 4 组槽位）；自动关闭模 U。",
                                         "Target: Full F2L (Cross + all 4 F2L slots); disables Mod-U."));
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
        : QDialog(parent), colors_(colors), editorCube_(editorCube) {
        setWindowTitle(trUi("Cross 快速求解", "Cross Quick Solver"));
        setMinimumWidth(qRound(420 * scale));
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

        // 2. Freely selectable F2L slots (0..4: Cross / XC / XXC / XXXC / F2L)
        auto *slotGroup = new QGroupBox(trUi("求解槽位", "F2L Slots to Solve"));
        auto *slotLayout = new QVBoxLayout(slotGroup);
        slotLayout->setContentsMargins(qRound(8 * scale), qRound(14 * scale), qRound(8 * scale), qRound(8 * scale));
        slotLayout->setSpacing(qRound(8 * scale));

        auto *slotGrid = new QGridLayout;
        slotGrid->setSpacing(qRound(6 * scale));
        const std::array<QString, 4> slotNames = {trUi("FR（右前）", "FR (Front-Right)"),
                                                  trUi("FL（左前）", "FL (Front-Left)"),
                                                  trUi("BR（右后）", "BR (Back-Right)"),
                                                  trUi("BL（左后）", "BL (Back-Left)")};
        const std::array<std::pair<int, int>, 4> slotPos = {{{1, 1}, {1, 0}, {0, 1}, {0, 0}}};
        for (int s = 0; s < 4; ++s) {
            slotButtons_[s] = new QPushButton(slotNames[s]);
            slotButtons_[s]->setCheckable(true);
            slotButtons_[s]->setCursor(Qt::PointingHandCursor);
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
        connect(useScramble_, &QRadioButton::toggled, scrambleInput_, &QLineEdit::setEnabled);
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
};
