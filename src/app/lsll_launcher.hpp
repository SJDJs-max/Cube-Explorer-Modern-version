#pragma once
#include "app_common.hpp"
#include <QProcess>
#include <QStandardPaths>

class LsllLauncher final : public QObject {
    QWidget *window_;
    QProcess server_;
    QUrl url_;
    QByteArray diagnostics_;
    bool closing_ = false;

    void warn(const QString &message) {
        QMessageBox::warning(window_, "LSLL 批量求解", message);
    }

    void openPage() {
        if (!QDesktopServices::openUrl(url_))
            warn("请在浏览器打开 " + url_.toString());
    }

  public:
    explicit LsllLauncher(QWidget *window) : QObject(window), window_(window) {
        connect(&server_, &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray prefix("LSLL_READY ");
            while (server_.canReadLine()) {
                const auto line = server_.readLine().trimmed();
                if (closing_ || url_.isValid() || !line.startsWith(prefix))
                    continue;
                bool ok = false;
                const int port = line.mid(prefix.size()).toInt(&ok);
                if (ok && port > 0 && port <= 65535) {
                    url_ = QUrl(QString("http://127.0.0.1:%1/").arg(port));
                    openPage();
                }
            }
        });
        connect(&server_, &QProcess::readyReadStandardError, this, [this] {
            diagnostics_ = (diagnostics_ + server_.readAllStandardError()).right(4096);
        });
        connect(&server_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (!closing_ && error == QProcess::FailedToStart)
                warn("无法启动 Node.js。\n" + server_.errorString());
        });
        connect(&server_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const bool ready = url_.isValid();
            url_.clear();
            if (!closing_ && (!ready || code != 0 || status != QProcess::NormalExit))
                warn("LSLL 本地服务已退出。\n" + QString::fromUtf8(diagnostics_).trimmed());
        });
    }

    ~LsllLauncher() override {
        closing_ = true;
        if (server_.state() == QProcess::NotRunning)
            return;
        server_.closeWriteChannel();
        if (!server_.waitForFinished(3000)) {
            server_.kill();
            server_.waitForFinished(1000);
        }
    }

    void open() {
        if (url_.isValid()) {
            openPage();
            return;
        }
        if (server_.state() != QProcess::NotRunning)
            return;
        const auto node = QStandardPaths::findExecutable("node");
        if (node.isEmpty()) {
            warn("找不到 Node.js。请将 Node.js 22 或更新版本加入 PATH。");
            return;
        }
        const auto base = baseDirectory();
        const auto packagedScript = base + "/lsll/server.mjs";
        const auto script = QFileInfo::exists(packagedScript)
                                ? packagedScript
                                : QString::fromUtf8(CUBE_PROJECT_ROOT) + "/lsll/server.mjs";
        const auto packagedEngine = base + "/CubeExplorerEngine.exe";
        const auto engine = QFileInfo::exists(packagedEngine)
                                ? packagedEngine
                                : QDir::cleanPath(base + "/../engine/CubeExplorerEngine.exe");
        if (!QFileInfo(script).isFile() || !QFileInfo(engine).isFile()) {
            warn("找不到 LSLL 服务或当前 Cube Explorer 引擎。\n" + script + "\n" + engine);
            return;
        }
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LSLL_ENGINE", engine);
        environment.insert("LSLL_WORKSPACE", base + "/data/lsll");
        environment.insert("LSLL_MANAGED", "1");
        server_.setProcessEnvironment(environment);
        server_.setWorkingDirectory(base);
        diagnostics_.clear();
        server_.start(node, {script});
    }
};
