#pragma once
#include "cube/mcc.hpp"
#include "cube/model.hpp"
#include "cube/pattern.hpp"
#include "cube/runtime_config.hpp"
#include "cube/twophase.hpp"
#include <QtWidgets>
#include <QDesktopServices>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <stop_token>
#include <thread>

using namespace cube;
inline QString qstr(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
// Formats formula moves for GUI display: brackets any leading pre-rotation
// (e.g. [y'], [x' y]) and any initial U / pre-AUF (e.g. [U], [y'] [U]) when
// bracketInitial is true, without altering the underlying Move vector or costs.
inline QString formatDisplayMoves(std::span<const Move> moves, bool bracketInitial) {
    if (!bracketInitial)
        return qstr(format_moves(moves, false));
    std::string result;
    std::size_t i = 0;
    if (i < moves.size() && moves[i].is_rotation()) {
        result += '[';
        while (i < moves.size() && moves[i].is_rotation()) {
            if (result.back() != '[')
                result += ' ';
            result += format_move(moves[i]);
            ++i;
        }
        result += ']';
    }
    if (i < moves.size() && moves[i].symbol == 'U') {
        if (!result.empty())
            result += ' ';
        result += '[';
        result += format_move(moves[i]);
        result += ']';
        ++i;
    }
    for (; i < moves.size(); ++i) {
        if (!result.empty())
            result += ' ';
        result += format_move(moves[i]);
    }
    return qstr(result);
}
inline std::filesystem::path fsPath(const QString &value) {
    return std::filesystem::path(value.toStdWString());
}
inline QString baseDirectory() {
    return QCoreApplication::applicationDirPath();
}
inline QString tableDirectory() {
    const auto packaged = baseDirectory() + "/tables/original";
    return QFileInfo::exists(packaged + "/phase1PF.prun")
               ? packaged
               : QString::fromUtf8(CUBE_PROJECT_ROOT) + "/tables/original";
}
inline QString stopText(PatternStop stop) {
    switch (stop) {
    case PatternStop::Exhausted:
        return "已完成当前步数范围的搜索";
    case PatternStop::ResultLimit:
        return "已达到结果数量上限";
    case PatternStop::TimeLimit:
        return "已达到时间上限，搜索未穷尽";
    case PatternStop::NodeLimit:
        return "已达到节点上限，搜索未穷尽";
    case PatternStop::Cancelled:
        return "搜索已停止，已有结果保留";
    case PatternStop::InvalidInput:
        return "输入或搜索参数无效";
    }
    return {};
}
inline QString friendlyError(std::string_view error) {
    const auto text = qstr(error);
    if (text.contains("Specify an entire cubie"))
        return "块的颜色尚未完整：请继续染色、启用自动修正，或使用特殊标记。";
    if (text.contains("identity occurs more than once"))
        return "有重复的角块或棱块，请检查颜色。";
    if (text.contains("parit"))
        return "角、棱的朝向或置换奇偶不合法，请检查输入。";
    if (text.contains("impossible color") || text.contains("reflection"))
        return "块的颜色组合或排列不合法。";
    return text;
}
inline bool ordinaryPattern(const PatternState &p) {
    const auto known = [](const auto &a) {
        return std::all_of(a.begin(), a.end(), [](int n) { return n >= 0; });
    };
    return known(p.cp) && known(p.co) && known(p.ep) && known(p.eo) &&
           std::none_of(p.center_ori.begin(), p.center_ori.end(), [](int n) { return n >= 0; });
}
inline CubeState completeCube(const PatternState &p) {
    if (!ordinaryPattern(p))
        throw std::invalid_argument("不完整或中心方向约束请使用公式求解。");
    CubeState c;
    c.frame = p.frame;
    std::copy(p.cp.begin(), p.cp.end(), c.cubies.cp.begin());
    std::copy(p.co.begin(), p.co.end(), c.cubies.co.begin());
    std::copy(p.ep.begin(), p.ep.end(), c.cubies.ep.begin());
    std::copy(p.eo.begin(), p.eo.end(), c.cubies.eo.begin());
    return c;
}
struct ResultData {
    PatternState target, start, terminal;
    std::vector<Move> moves;
    int turns = 0, rotations = 0;
    double mcc = -1.0;
    bool formula = true, generate = false, optimalityProven = false;
};
