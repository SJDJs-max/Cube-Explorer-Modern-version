#pragma once

#include "app_common.hpp"

// Original Maneuver Window workflow: collect locally, explicitly add to main.
class FormulaWindow final : public QDialog {
    PatternState input_;
    bool generate_ = false;
    std::function<void(ResultData)> sink_;
    std::jthread worker_;
    std::atomic<bool> stopping_{false};
    bool busy_ = false, closePending_ = false;
    std::uint64_t generation_ = 0;
    std::vector<ResultData> results_;
    std::vector<QWidget *> settings_;
    std::array<QCheckBox *, 18> moveBoxes_{};
    std::array<std::array<bool, 3>, 18> movePowerEnabled_{};
    std::array<int, 18> maxSymbolUses_{};
    std::array<QCheckBox *, 3> powerBoxes_{}, categoryBoxes_{};
    QSpinBox *minimum_ = nullptr, *maximum_ = nullptr, *resultLimit_ = nullptr, *timeLimit_ = nullptr,
             *maxRotations_ = nullptr;
    QDoubleSpinBox *nodes_ = nullptr;
    QCheckBox *modU_ = nullptr, *symmetry_ = nullptr, *dedup_ = nullptr, *triples_ = nullptr,
              *memoize_ = nullptr, *rotationQuotient_ = nullptr;
    QComboBox *heuristic_ = nullptr, *preRotation_ = nullptr;
    QLabel *preRotLabel_ = nullptr, *preRotNoteLabel_ = nullptr;
    QPushButton *fineMovesBtn_ = nullptr;
    QComboBox *mccSort_ = nullptr;
    QToolButton *mccParamsBtn_ = nullptr;
    bool mccIgnoreAuf_ = true;
    MccParams mccParams_{};
    std::chrono::steady_clock::time_point lastLiveSort_{};
    QPlainTextEdit *output_ = nullptr;
    QPushButton *start_ = nullptr, *clear_ = nullptr, *add_ = nullptr;
    QGroupBox *moveGroup_ = nullptr;
    QToolButton *fold_ = nullptr;
    QLabel *powersLabel_ = nullptr;
    QLabel *minLabel_ = nullptr, *maxLabel_ = nullptr, *resLabel_ = nullptr, *timeLabel_ = nullptr,
           *nodesLabel_ = nullptr, *heurLabel_ = nullptr, *maxRotLabel_ = nullptr;
    QString statusText_, statisticsText_;
    QString preambleText_, epilogueText_;
    std::optional<PatternReport> lastReport_;
    double uiScale_ = 1.0;
    bool scaling_ = false;
    struct LayoutMetrics { QLayout* layout; QMargins margins; int spacing; int horizontal; int vertical; };
    std::vector<LayoutMetrics> layoutMetrics_;

    QSpinBox *spin(QFormLayout *, QLabel *&, const QString &, int, int, int);
    void refreshControls();
    void updateMoveBoxDecoration(int symbolIndex);
    void showMoveContextMenu(int symbolIndex, const QPoint &globalPos);
    void showFineMovesDialog();
    void showMccParamsDialog();
    void recomputeAllMcc();
    std::vector<std::size_t> displayOrder() const;
    QString formatResultLine(const ResultData &data) const;
    void rebuildOutput();
    void updateButtonIcons();
    void configureState(bool);
    void setStatus(QString);
    void updateStatusLine();
    void appendResult(ResultData);
    void appendMessage(const QString&);
    void finish(std::uint64_t, QString, std::optional<PatternReport> = std::nullopt);
    bool postAndWait(std::uint64_t, std::stop_token, std::function<void()>);
    template <class Action> void post(std::uint64_t generation, Action action) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, action = std::move(action)]() mutable {
                if (generation == generation_)
                    action();
            },
            Qt::QueuedConnection);
    }

  protected:
    void closeEvent(QCloseEvent *) override;
    void reject() override;
    void resizeEvent(QResizeEvent*) override;

  public:
    std::function<void(bool)> onBusy;
    std::function<void()> onFinished;
    FormulaWindow(PatternState, bool generate, std::function<void(ResultData)> sink,
                  QWidget *parent = nullptr);
    ~FormulaWindow() override;
    [[nodiscard]] bool busy() const {
        return busy_;
    }
    [[nodiscard]] QString startText() const;
    [[nodiscard]] int resultCount() const {
        return static_cast<int>(results_.size());
    }
    void startSearch();
    void stopSearch();
    void setUiScale(double);
    [[nodiscard]] double uiScale() const { return uiScale_; }
    [[nodiscard]] PatternRequest request() const;
    [[nodiscard]] const std::optional<PatternReport>& lastReport() const { return lastReport_; }
    void smokeConfigure(PatternState, int maximum);
    void setModuloU(bool enabled);
    void setPreRotationMode(PatternPreRotation mode);
    void setPreRotateY(bool enabled);
    void setMovePowerEnabled(int symbolIndex, int powerIndex, bool enabled);
    void setMaxSymbolUses(int symbolIndex, int limit);
    void setMaxRotations(int limit);
    void setMccSortMode(int mode);
    void retranslateUi();
    void refreshAppearance();
    void clearResults();
    // Deliberately copies all buffered results on every click, like BAddClick.
    void addResults();
};
