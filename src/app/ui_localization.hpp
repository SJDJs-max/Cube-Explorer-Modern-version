#pragma once
#include "ui_i18n.hpp"
#include <QHash>
#include <QString>
#include <QTranslator>

// The portable Qt bundle has no translation package. Translate Qt's built-in
// color-picker, standard buttons, and text/spinbox context menus in Chinese
// mode while falling back cleanly to English when English UI mode is active.
class ChineseUiTranslator final : public QTranslator {
  public:
    using QTranslator::QTranslator;
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char *context, const char *source, const char *, int) const override {
        if (uiEnglish())
            return {};
        const auto group = QByteArray(context);
        const auto key = QString::fromUtf8(source);
        if (group == "QColorDialog") {
            static const QHash<QString, QString> words = {
                {"Hu&e:", "色相："},
                {"&Sat:", "饱和度："},
                {"&Val:", "明度："},
                {"&Red:", "红色："},
                {"&Green:", "绿色："},
                {"Bl&ue:", "蓝色："},
                {"A&lpha channel:", "透明度："},
                {"&HTML:", "十六进制："},
                {"Select Color", "选择颜色"},
                {"&Basic colors", "基本颜色"},
                {"&Custom colors", "自定义颜色"},
                {"&Add to Custom Colors", "添加到自定义颜色"},
                {"&Pick Screen Color", "拾取屏幕颜色"},
                {"Cursor at %1, %2\nPress ESC to cancel", "光标位置：%1，%2\n按 Esc 取消"}};
            return words.value(key);
        }
        if (group == "QLineEdit" || group == "QWidgetTextControl") {
            static const QHash<QString, QString> words = {
                {"&Undo", "撤销(&U)"},
                {"&Redo", "重做(&R)"},
                {"Cu&t", "剪切(&T)"},
                {"&Copy", "复制(&C)"},
                {"Copy &Link Location", "复制链接地址(&L)"},
                {"&Paste", "粘贴(&P)"},
                {"Delete", "删除"},
                {"Select All", "全选"},
                {"&Select All", "全选(&A)"},
                {"Insert Unicode control character", "插入 Unicode 控制字符"}};
            return words.value(key);
        }
        if (group == "QAbstractSpinBox") {
            static const QHash<QString, QString> words = {
                {"&Step up", "增加(&S)"},
                {"Step &down", "减少(&D)"},
                {"&Select All", "全选(&A)"},
                {"Select All", "全选"}};
            return words.value(key);
        }
        if (group == "QUnicodeControlCharacterMenu") {
            static const QHash<QString, QString> words = {
                {"Insert Unicode control character", "插入 Unicode 控制字符"},
                {"LRM Left-to-right mark", "LRM 从左到右标记"},
                {"RLM Right-to-left mark", "RLM 从右到左标记"},
                {"ZWJ Zero width joiner", "ZWJ 零宽连接符"},
                {"ZWNJ Zero width non-joiner", "ZWNJ 零宽非连接符"},
                {"LRE Start of left-to-right embedding", "LRE 从左到右嵌入开始"},
                {"RLE Start of right-to-left embedding", "RLE 从右到左嵌入开始"},
                {"LRO Start of left-to-right override", "LRO 从左到右强制覆盖"},
                {"RLO Start of right-to-left override", "RLO 从右到左强制覆盖"},
                {"PDF Pop directional formatting", "PDF 结束方向格式"},
                {"LRI Left-to-right isolate", "LRI 从左到右隔离"},
                {"RLI Right-to-left isolate", "RLI 从右到左隔离"},
                {"FSI First strong isolate", "FSI 首强方向隔离"},
                {"PDI Pop directional isolate", "PDI 结束方向隔离"}};
            return words.value(key);
        }
        if (group == "QPlatformTheme" || group == "QDialogButtonBox") {
            static const QHash<QString, QString> words = {
                {"OK", "确定"},     {"Save", "保存"},         {"Save All", "全部保存"},
                {"Open", "打开"},   {"&Yes", "是"},           {"Yes to &All", "全部是"},
                {"&No", "否"},      {"N&o to All", "全部否"}, {"Abort", "中止"},
                {"Retry", "重试"},  {"Ignore", "忽略"},       {"Close", "关闭"},
                {"Cancel", "取消"}, {"Discard", "不保存"},    {"Help", "帮助"},
                {"Apply", "应用"},  {"Reset", "重置"},        {"Restore Defaults", "恢复默认"}};
            return words.value(key);
        }
        return {};
    }
};
