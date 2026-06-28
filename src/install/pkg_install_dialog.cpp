#include "install/pkg_install_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include "hook/menu_hook.h"
#include "install/pkg_installer.h"
#include "install/pkg_passcode.h"

namespace PkgInstallDialog {

namespace {

QString ToQString(const std::string& text) {
    return QString::fromUtf8(text.c_str(), static_cast<int>(text.size()));
}

void RunOnQtThread(std::function<void()> fn) {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        fn();
        return;
    }
    if (QThread::currentThread() == app->thread()) {
        fn();
        return;
    }
    QTimer::singleShot(0, app, std::move(fn));
}

QWidget* FindLauncherParentWidget(HWND parent_hwnd) {
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        for (QWidget* widget : app->topLevelWidgets()) {
            if (qobject_cast<QMainWindow*>(widget)) {
                return widget;
            }
        }
    }
    if (parent_hwnd) {
        if (QWidget* widget = QWidget::find(reinterpret_cast<WId>(parent_hwnd))) {
            return widget;
        }
    }
    return nullptr;
}

int OverallPercent(const PkgInstaller::InstallProgress& state, bool install_complete) {
    if (install_complete) {
        return 100;
    }
    if (state.pkg_count <= 0) {
        return 0;
    }

    const double pkg_slice = 100.0 / static_cast<double>(state.pkg_count);
    const double base = static_cast<double>(state.pkg_index) * pkg_slice;

    if (state.stage == "routing" || state.stage == "opening" || state.stage == "metadata") {
        return std::min(98, static_cast<int>(base + pkg_slice * 0.05));
    }

    if (state.stage == "files" && state.file_count > 0) {
        const double file_slice = pkg_slice / static_cast<double>(state.file_count);
        const double within =
            (static_cast<double>(state.file_index) * file_slice) +
            (file_slice * static_cast<double>(state.file_percent) / 100.0);
        return std::min(98, static_cast<int>(std::floor(base + within)));
    }

    if (state.stage == "done") {
        const bool last_pkg = state.pkg_index + 1 >= state.pkg_count;
        if (last_pkg) {
            return 98;
        }
        return std::min(98, static_cast<int>(std::floor(base + pkg_slice)));
    }

    return std::min(98, static_cast<int>(base));
}

void ApplyLauncherStyle(QDialog* dialog, QWidget* parent) {
    if (parent != nullptr) {
        dialog->setStyle(parent->style());
        dialog->setPalette(parent->palette());
        dialog->setFont(parent->font());
        if (!parent->windowIcon().isNull()) {
            dialog->setWindowIcon(parent->windowIcon());
        }
    }

    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setWindowModality(Qt::ApplicationModal);
}

class PkgInstallDialog final : public QDialog {
    Q_OBJECT

public:
    PkgInstallDialog(std::vector<std::filesystem::path> pkg_files, HWND parent_hwnd,
                     QWidget* parent)
        : QDialog(parent), pkg_files_(std::move(pkg_files)), parent_hwnd_(parent_hwnd) {
        ApplyLauncherStyle(this, parent);

        setWindowTitle(pkg_files_.size() == 1 ? QStringLiteral("Install Package")
                                             : QStringLiteral("Install Packages"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(20, 18, 20, 18);
        root->setSpacing(12);

        title_label_ = new QLabel(pkg_files_.size() == 1 ? QStringLiteral("Installing package")
                                                         : QStringLiteral("Installing packages"),
                                  this);
        QFont title_font = font();
        title_font.setPointSizeF(title_font.pointSizeF() + 1.0);
        title_font.setBold(true);
        title_label_->setFont(title_font);
        root->addWidget(title_label_);

        status_label_ = new QLabel(QStringLiteral("Preparing installation…"), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        progress_bar_ = new QProgressBar(this);
        progress_bar_->setRange(0, 0);
        progress_bar_->setValue(0);
        progress_bar_->setTextVisible(true);
        progress_bar_->setFormat(QStringLiteral("Preparing…"));
        progress_bar_->setMinimumHeight(22);
        root->addWidget(progress_bar_);

        auto* buttons = new QHBoxLayout();
        buttons->addStretch();
        action_button_ = new QPushButton(QStringLiteral("Cancel"), this);
        action_button_->setDefault(true);
        action_button_->setAutoDefault(true);
        buttons->addWidget(action_button_);
        root->addLayout(buttons);

        connect(action_button_, &QPushButton::clicked, this, &PkgInstallDialog::OnActionClicked);

        setMinimumWidth(460);
        resize(500, 170);
    }

    void RunInstallFlow() {
        show();
        raise();
        activateWindow();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        StartInstallAttempt();
        exec();

        if (install_succeeded_) {
            MenuHook::TriggerRefreshGameList(parent_hwnd_);
        }
    }

    void StartInstallAttempt() {
        install_finished_ = false;
        action_button_->setEnabled(false);
        cancel_requested_ = false;

        std::string error;
        const bool ok = PkgInstaller::InstallPkgFiles(
            parent_hwnd_, pkg_files_,
            [this](const PkgInstaller::InstallProgress& state) { OnProgress(state); }, &error,
            [this]() { return cancel_requested_; });

        install_finished_ = true;
        action_button_->setEnabled(true);

        if (ok) {
            install_succeeded_ = true;
            action_button_->setText(QStringLiteral("Close"));

            if (pkg_files_.size() == 1) {
                SetStatus(QStringLiteral("PKG installed successfully."), 100, false, true);
            } else {
                SetStatus(QStringLiteral("All PKGs installed successfully."), 100, false, true);
            }

            QTimer::singleShot(700, this, [this]() {
                if (isVisible()) {
                    accept();
                }
            });
        } else if (cancel_requested_) {
            action_button_->setText(QStringLiteral("Close"));
            SetStatus(QStringLiteral("Installation cancelled."), 0, false, false);
        } else if (error == PkgInstaller::kInstallAbortedError) {
            accept();
        } else if (PkgPasscode::IsRequiredError(error)) {
            bool accepted = false;
            const QString entered = QInputDialog::getText(
                this, QStringLiteral("PKG Passcode Required"),
                QString::fromUtf8(PkgPasscode::RequiredErrorMessage()), QLineEdit::Normal,
                QString(), &accepted);
            if (accepted) {
                const auto passcode = entered.trimmed().toStdString();
                if (passcode.size() == 32) {
                    PkgPasscode::SaveForContentId(PkgPasscode::ContentIdForFile(pkg_files_.front()),
                                                  passcode);
                    SetStatus(QStringLiteral("Retrying installation with PKG passcode…"), 0, true,
                              false);
                    QTimer::singleShot(0, this, [this]() { StartInstallAttempt(); });
                    return;
                }
            }
            action_button_->setText(QStringLiteral("Close"));
            SetStatus(QString::fromUtf8(PkgPasscode::RequiredErrorMessage()), 0, false, false);
            QMessageBox::warning(this, QStringLiteral("PKG Passcode Required"),
                                 QString::fromUtf8(PkgPasscode::RequiredErrorMessage()));
        } else if (!error.empty()) {
            action_button_->setText(QStringLiteral("Close"));
            SetStatus(ToQString(error), 0, false, false);
            QMessageBox::warning(this, QStringLiteral("Install Package"), ToQString(error));
        } else {
            action_button_->setText(QStringLiteral("Close"));
            SetStatus(QStringLiteral("PKG installation did not complete."), 0, false, false);
        }
    }

private slots:
    void OnActionClicked() {
        if (install_finished_) {
            accept();
            return;
        }

        cancel_requested_ = true;
        action_button_->setEnabled(false);
        SetStatus(QStringLiteral("Cancelling installation…"), displayed_percent_, true, false);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (!install_finished_) {
            cancel_requested_ = true;
            action_button_->setEnabled(false);
            SetStatus(QStringLiteral("Cancelling installation…"), displayed_percent_, true, false);
            event->ignore();
            return;
        }
        QDialog::closeEvent(event);
    }

private:
    void SetStatus(const QString& status, int percent, bool indeterminate, bool install_complete) {
        status_label_->setText(status);
        if (indeterminate) {
            progress_bar_->setRange(0, 0);
            progress_bar_->setFormat(QStringLiteral("%1").arg(status));
        } else {
            progress_bar_->setRange(0, 100);
            const int value = install_complete ? 100 : std::min(percent, 98);
            displayed_percent_ = value;
            progress_bar_->setValue(value);
            progress_bar_->setFormat(QStringLiteral("%p%"));
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }

    void SetStatusSmoothed(const QString& status, int target_percent, bool indeterminate) {
        if (indeterminate) {
            SetStatus(status, target_percent, true, false);
            return;
        }

        if (displayed_percent_ < 0) {
            displayed_percent_ = target_percent;
        } else if (target_percent > displayed_percent_) {
            const int step = std::max(1, (target_percent - displayed_percent_) / 4);
            displayed_percent_ = std::min(target_percent, displayed_percent_ + step);
        } else {
            displayed_percent_ = target_percent;
        }

        status_label_->setText(status);
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(std::min(displayed_percent_, 98));
        progress_bar_->setFormat(QStringLiteral("%p%"));
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }

    void OnProgress(const PkgInstaller::InstallProgress& state) {
        if (cancel_requested_) {
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            return;
        }

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool stage_changed = state.stage != last_stage_;
        const bool file_changed = state.file_index != last_file_index_;
        const bool force = stage_changed || file_changed || state.stage == "done";

        if (!force && now - last_update_ms_ < 200) {
            return;
        }
        last_update_ms_ = now;
        last_stage_ = state.stage;
        last_file_index_ = state.file_index;

        const QString file_name = QString::fromStdWString(state.pkg_path.filename().wstring());
        QString status;
        const int percent = OverallPercent(state, false);
        const bool indeterminate =
            state.stage == "routing" || state.stage == "opening" || state.stage == "metadata";

        if (state.stage == "routing") {
            status = pkg_files_.size() == 1
                         ? QStringLiteral("Checking install location…")
                         : QStringLiteral("Checking install location (PKG %1 of %2)…")
                               .arg(state.pkg_index + 1)
                               .arg(state.pkg_count);
        } else if (state.stage == "opening") {
            status = pkg_files_.size() == 1
                         ? QStringLiteral("Opening package…")
                         : QStringLiteral("Opening PKG %1 of %2…")
                               .arg(state.pkg_index + 1)
                               .arg(state.pkg_count);
        } else if (state.stage == "metadata") {
            status = QStringLiteral("Reading package metadata…");
        } else if (state.stage == "files") {
            status = pkg_files_.size() == 1
                         ? QStringLiteral("Extracting files (%1 of %2)")
                               .arg(state.file_index + 1)
                               .arg(state.file_count)
                         : QStringLiteral("PKG %1 of %2 — extracting files (%3 of %4)")
                               .arg(state.pkg_index + 1)
                               .arg(state.pkg_count)
                               .arg(state.file_index + 1)
                               .arg(state.file_count);
        } else if (state.stage == "done") {
            status = QStringLiteral("Finishing %1…").arg(file_name);
        } else {
            status = QStringLiteral("Installing…");
        }

        if (indeterminate) {
            SetStatus(status, percent, true, false);
        } else {
            SetStatusSmoothed(status, percent, false);
        }
    }

    std::vector<std::filesystem::path> pkg_files_;
    HWND parent_hwnd_ = nullptr;
    QLabel* title_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* action_button_ = nullptr;
    qint64 last_update_ms_ = 0;
    int displayed_percent_ = -1;
    int last_file_index_ = -1;
    std::string last_stage_;
    bool cancel_requested_ = false;
    bool install_finished_ = false;
    bool install_succeeded_ = false;
};

} // namespace

void Run(const std::vector<std::filesystem::path>& pkg_files, HWND parent_hwnd) {
    if (pkg_files.empty()) {
        return;
    }

    RunOnQtThread([pkg_files, parent_hwnd]() {
        PkgInstallDialog dialog(pkg_files, parent_hwnd, FindLauncherParentWidget(parent_hwnd));
        dialog.RunInstallFlow();
    });
}

} // namespace PkgInstallDialog

#include "pkg_install_dialog.moc"
