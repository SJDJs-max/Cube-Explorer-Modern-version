#pragma once
#include <QApplication>
#include <QString>

inline bool uiEnglish() {
    return qApp && qApp->property("cubeEnglish").toBool();
}

inline void setUiEnglish(bool english) {
    if (qApp)
        qApp->setProperty("cubeEnglish", english);
}

inline QString trUi(const char *zh, const char *en) {
    return uiEnglish() ? QString::fromUtf8(en) : QString::fromUtf8(zh);
}
