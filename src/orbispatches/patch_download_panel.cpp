#include "orbispatches/patch_download_panel.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QLayout>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <filesystem>

#include "config/shad_config.h"
#include "hook/qt_launcher_util.h"
#include "install/pkg_installer.h"
#include "install/pkg_merge.h"
#include "install/pkg_passcode.h"
#include "install/pkg_router.h"
#include "orbispatches/patch_link_fetcher.h"

namespace PatchDownload {

namespace {

constexpr const char* kPanelObjectName = "orbisPatchDownloadPanel";

QString ToQString(const std::string& text) {
    return QString::fromUtf8(text.c_str(), static_cast<int>(text.size()));
}

QString FormatBytes(qint64 bytes) {
    if (bytes < 0) {
        return QStringLiteral("?");
    }
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 %2").arg(bytes).arg(QLatin1String(units[unit]));
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

int OverallDownloadPercent(std::size_t piece_index, std::size_t piece_count, qint64 received,
                           qint64 total) {
    if (piece_count == 0) {
        return 0;
    }
    const qint64 completed_pieces = static_cast<qint64>(piece_index);
    const qint64 piece_progress = (total > 0) ? ((received * 100LL) / total) : 0;
    const qint64 overall =
        ((completed_pieces * 100LL) + piece_progress) / static_cast<qint64>(piece_count);
    return qBound(0, static_cast<int>(overall), 100);
}

std::wstring SanitizeFolderComponent(std::wstring text) {
    static constexpr wchar_t kInvalid[] = L"<>:\"/\\|?*";
    for (wchar_t& ch : text) {
        if (wcschr(kInvalid, ch) != nullptr || ch < 32) {
            ch = L'_';
        }
    }
    while (!text.empty() && (text.back() == L' ' || text.back() == L'.')) {
        text.pop_back();
    }
    while (!text.empty() && (text.front() == L' ' || text.front() == L'.')) {
        text.erase(text.begin());
    }
    return text;
}

std::wstring FormatPatchFolderName(const std::string& game_name, const std::string& titleid,
                                   const std::string& version) {
    std::wstring name = SanitizeFolderComponent(
        QString::fromUtf8(game_name.c_str(), static_cast<int>(game_name.size())).toStdWString());
    if (name.empty()) {
        name = SanitizeFolderComponent(
            QString::fromUtf8(titleid.c_str(), static_cast<int>(titleid.size())).toStdWString());
    }

    std::wstring version_label =
        QString::fromUtf8(version.c_str(), static_cast<int>(version.size())).toStdWString();
    version_label = SanitizeFolderComponent(version_label);
    if (!version_label.empty() && (version_label[0] != L'v' && version_label[0] != L'V')) {
        version_label.insert(version_label.begin(), L'v');
    }

    const std::wstring serial = SanitizeFolderComponent(
        QString::fromUtf8(titleid.c_str(), static_cast<int>(titleid.size())).toStdWString());

    std::wstring folder_name = name + L" " + version_label + L" (" + serial + L")";
    if (folder_name.size() > 200) {
        folder_name.resize(200);
        while (!folder_name.empty() && folder_name.back() == L' ') {
            folder_name.pop_back();
        }
    }
    return folder_name;
}

std::filesystem::path PatchDownloadDirectory(const std::string& game_name, const std::string& titleid,
                                             const std::string& version) {
    return ShadConfig::GetUpdatePatchesDir() / FormatPatchFolderName(game_name, titleid, version);
}

bool PanelIsBelowSplitter(QSplitter* splitter, QWidget* panel) {
    if (!splitter || !panel) {
        return false;
    }

    QWidget* host = splitter->parentWidget();
    if (!host || panel->parentWidget() != host) {
        return false;
    }

    QLayout* layout = host->layout();
    if (!layout) {
        return false;
    }

    const int splitter_index = layout->indexOf(splitter);
    const int panel_index = layout->indexOf(panel);
    return splitter_index >= 0 && panel_index > splitter_index;
}

class PlayGuardFilter final : public QObject {
public:
    explicit PlayGuardFilter(QMainWindow* main_window) : main_window_(main_window) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!main_window_) {
            return QObject::eventFilter(watched, event);
        }

        const bool is_play_click =
            (event->type() == QEvent::MouseButtonRelease &&
             watched->objectName() == QLatin1String("playButton"));
        const bool is_game_launch =
            (event->type() == QEvent::MouseButtonDblClick &&
             (watched->objectName() == QLatin1String("gamelist") ||
              watched->objectName() == QLatin1String("gamegridlist")));

        if ((is_play_click || is_game_launch) && PatchDownload::ShouldBlockPlay(main_window_)) {
            const QString serial = QtLauncherUtil::SelectedGameSerial(main_window_);
            QMessageBox::information(main_window_, QStringLiteral("Patch Download"),
                                     QStringLiteral("Cannot play %1 while its ORBISPatches update "
                                                    "is downloading or installing.")
                                         .arg(serial.isEmpty() ? QStringLiteral("this game")
                                                               : serial));
            return true;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QMainWindow* main_window_ = nullptr;
};

class PatchDownloadPanel final : public QWidget {
public:
    PatchDownloadPanel() {
        setObjectName(QLatin1String(kPanelObjectName));
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 6, 10, 6);
        root->setSpacing(4);

        title_label_ = new QLabel(this);
        title_label_->setWordWrap(true);
        root->addWidget(title_label_);

        status_label_ = new QLabel(QStringLiteral("Preparing…"), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        progress_bar_ = new QProgressBar(this);
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(0);
        progress_bar_->setTextVisible(true);
        progress_bar_->setMinimumHeight(18);
        root->addWidget(progress_bar_);

        auto* buttons = new QHBoxLayout();
        retry_install_button_ = new QPushButton(QStringLiteral("Install PKGs"), this);
        cancel_button_ = new QPushButton(QStringLiteral("Cancel"), this);
        retry_install_button_->setVisible(false);
        cancel_button_->setMinimumWidth(88);
        cancel_button_->setMinimumHeight(28);
        retry_install_button_->setMinimumHeight(28);
        buttons->addWidget(retry_install_button_);
        buttons->addStretch();
        buttons->addWidget(cancel_button_);
        root->addLayout(buttons);

        connect(cancel_button_, &QPushButton::clicked, this, [this]() { RequestCancel(); });

        const QMargins margins = root->contentsMargins();
        panel_height_ = root->sizeHint().height() + margins.top() + margins.bottom();
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setStyleSheet(
            QStringLiteral("#%1 { border-top: 1px solid palette(mid); }").arg(kPanelObjectName));

        HidePanel();
    }

    void SetTitle(const QString& title) {
        title_label_->setText(title);
    }

    void SetStatus(const QString& status, int percent = -1, bool indeterminate = false) {
        status_label_->setText(status);
        if (indeterminate) {
            progress_bar_->setRange(0, 0);
            progress_bar_->setFormat(status);
        } else {
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(qMax(0, percent));
            progress_bar_->setFormat(percent >= 0 ? QStringLiteral("%p%") : QString());
        }
    }

    void ShowPanel() {
        setFixedHeight(panel_height_);
        show();
    }

    void HidePanel() {
        hide();
        setFixedHeight(0);
    }

    void SetBusyUi(bool busy) {
        cancel_button_->setEnabled(true);
        cancel_button_->setText(busy ? QStringLiteral("Cancel") : QStringLiteral("Dismiss"));
    }

    void SetRetryVisible(bool visible, bool enabled) {
        retry_install_button_->setVisible(visible);
        retry_install_button_->setEnabled(enabled);
    }

    QPushButton* RetryButton() const {
        return retry_install_button_;
    }

    bool IsCancelRequested() const {
        return cancel_requested_;
    }

    void ResetCancel() {
        cancel_requested_ = false;
    }

    void RequestCancel() {
        if (cancel_button_->text() == QStringLiteral("Dismiss")) {
            HidePanel();
            return;
        }
        cancel_requested_ = true;
        cancel_button_->setEnabled(false);
        SetStatus(QStringLiteral("Cancelling…"), -1, true);
    }

private:
    QLabel* title_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* retry_install_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    bool cancel_requested_ = false;
    int panel_height_ = 0;
};

class PatchDownloadService final : public QObject {
public:
    static PatchDownloadService& Instance() {
        static PatchDownloadService service;
        return service;
    }

    void Attach(QMainWindow* main_window) {
        if (!main_window) {
            return;
        }

        if (attached_main_ != main_window) {
            attached_main_ = main_window;
            QSplitter* splitter = QtLauncherUtil::FindMainSplitter(main_window);

            QWidget* existing_panel =
                main_window->findChild<QWidget*>(QLatin1String(kPanelObjectName));
            panel_ = existing_panel ? static_cast<PatchDownloadPanel*>(existing_panel) : nullptr;

            if (!panel_ && splitter) {
                panel_ = new PatchDownloadPanel();
                QtLauncherUtil::AttachBelowSplitter(splitter, panel_);
                panel_->HidePanel();
            } else if (panel_ && splitter) {
                if (splitter->indexOf(panel_) >= 0) {
                    panel_->setParent(nullptr);
                    QtLauncherUtil::AttachBelowSplitter(splitter, panel_);
                    panel_->HidePanel();
                } else if (!PanelIsBelowSplitter(splitter, panel_)) {
                    QtLauncherUtil::AttachBelowSplitter(splitter, panel_);
                    panel_->HidePanel();
                }
            }

            if (panel_) {
                connect(panel_->RetryButton(), &QPushButton::clicked, this,
                        [this]() { RetryInstall(); }, Qt::UniqueConnection);
            }

            if (!play_guard_) {
                play_guard_ = new PlayGuardFilter(main_window);
                if (QPushButton* play = main_window->findChild<QPushButton*>("playButton")) {
                    play->installEventFilter(play_guard_);
                }
                if (QTableWidget* list = main_window->findChild<QTableWidget*>("gamelist")) {
                    list->installEventFilter(play_guard_);
                }
                if (QTableWidget* grid = main_window->findChild<QTableWidget*>("gamegridlist")) {
                    grid->installEventFilter(play_guard_);
                }
            }
        }
    }

    bool IsBusy() const {
        return job_active_;
    }

    const std::string& BlockedTitleId() const {
        return blocked_titleid_;
    }

    void Start(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
               const std::string& game_name, HWND parent_hwnd, bool download_only) {
        if (!attached_main_ || !panel_) {
            return;
        }

        if (job_active_) {
            QMessageBox::information(attached_main_, QStringLiteral("Patch Download"),
                                     QStringLiteral("Another ORBISPatches download is already "
                                                    "in progress."));
            return;
        }

        patch_ = patch;
        titleid_ = titleid;
        game_name_ = game_name;
        parent_hwnd_ = parent_hwnd;
        download_only_ = download_only;
        blocked_titleid_ = titleid;
        job_active_ = true;
        cancel_requested_ = false;
        downloaded_pkgs_.clear();
        download_dir_.clear();

        const QString display_name =
            game_name_.empty() ? ToQString(titleid_) : ToQString(game_name_);
        panel_->SetTitle(download_only_
                             ? QStringLiteral("Downloading patch — %1 v%2")
                                   .arg(display_name, ToQString(patch_.version))
                             : QStringLiteral("Downloading & installing patch — %1 v%2")
                                   .arg(display_name, ToQString(patch_.version)));
        panel_->ResetCancel();
        panel_->SetBusyUi(true);
        panel_->SetRetryVisible(false, false);
        panel_->SetStatus(QStringLiteral("Preparing…"), -1, true);
        panel_->ShowPanel();

        QTimer::singleShot(0, this, [this]() { RunJob(); });
    }

    void RetryInstall() {
        if (downloaded_pkgs_.empty() || job_active_ || download_only_ || !panel_) {
            return;
        }
        job_active_ = true;
        blocked_titleid_ = titleid_;
        panel_->SetBusyUi(true);
        panel_->SetRetryVisible(false, false);
        InstallAllPkgs();
    }

    void RunJob() {
        if (!panel_) {
            job_active_ = false;
            blocked_titleid_.clear();
            return;
        }

        std::string error;
        panel_->SetStatus(QStringLiteral("Contacting ORBISPatches and downloading patch…"), -1,
                          true);
        QCoreApplication::processEvents(QEventLoop::AllEvents);

        const auto download_dir = PatchDownloadDirectory(game_name_, titleid_, patch_.version);
        download_dir_ = QString::fromStdWString(download_dir.wstring());
        std::error_code ec;
        std::filesystem::create_directories(download_dir, ec);
        downloaded_pkgs_.clear();

        const auto pump = []() { QCoreApplication::processEvents(QEventLoop::AllEvents); };
        const auto cancelled = [this]() { return IsCancelled(); };

        if (!OrbisPatches::PatchLinkFetcher::FetchAndDownloadPieces(
                titleid_, patch_.version, patch_.patch_key, download_dir, downloaded_pkgs_,
                [this](size_t piece_index, size_t piece_count, int64_t received, int64_t total) {
                    if (!panel_) {
                        return;
                    }
                    const QString piece_label = QStringLiteral("piece %1 of %2")
                                                    .arg(static_cast<int>(piece_index + 1))
                                                    .arg(static_cast<int>(piece_count));
                    const int overall =
                        OverallDownloadPercent(piece_index, piece_count, received, total);
                    if (total > 0) {
                        panel_->SetStatus(QStringLiteral("Downloading %1 (%2 / %3)")
                                              .arg(piece_label, FormatBytes(received),
                                                   FormatBytes(total)),
                                          overall, false);
                    } else {
                        panel_->SetStatus(
                            QStringLiteral("Downloading %1 (%2 received…)")
                                .arg(piece_label, FormatBytes(received)),
                            overall >= 0 ? overall : -1, overall < 0);
                    }
                    QCoreApplication::processEvents(QEventLoop::AllEvents);
                },
                error, parent_hwnd_, pump, cancelled)) {
            if (cancelled() || error == "Download cancelled.") {
                FinishCancelled();
                return;
            }
            panel_->SetStatus(ToQString(error), 0, false);
            FinishJob(!download_only_ && !downloaded_pkgs_.empty());
            QMessageBox::warning(attached_main_, QStringLiteral("Patch Download"),
                                 ToQString(error));
            return;
        }

        if (cancelled()) {
            FinishCancelled();
            return;
        }

        if (!downloaded_pkgs_.empty()) {
            std::string prepare_error;
            const auto install_ready = PkgMerge::PrepareInstallPaths(
                downloaded_pkgs_, std::filesystem::path(download_dir_.toStdWString()),
                prepare_error);
            if (install_ready.empty()) {
                panel_->SetStatus(ToQString(prepare_error), 0, false);
                FinishJob(!download_only_ && !downloaded_pkgs_.empty());
                QMessageBox::warning(attached_main_, QStringLiteral("Patch Download"),
                                     ToQString(prepare_error));
                return;
            }
        }

        if (download_only_) {
            panel_->SetStatus(QStringLiteral("Download complete. Saved to:\n%1").arg(download_dir_),
                              100, false);
            FinishJob(false);
            return;
        }

        InstallAllPkgs();
    }

    bool IsCancelled() const {
        return cancel_requested_ || (panel_ && panel_->IsCancelRequested());
    }

    void InstallAllPkgs() {
        std::string merge_error;
        const auto install_paths = PkgMerge::PrepareInstallPaths(
            downloaded_pkgs_, std::filesystem::path(download_dir_.toStdWString()), merge_error);
        if (install_paths.empty()) {
            panel_->SetStatus(ToQString(merge_error), 0, false);
            FinishJob(true);
            if (std::string_view(merge_error) == PkgMerge::StandaloneDeltaPkgMessage()) {
                PkgRouter::ShowDeltaPkgNotSupported(parent_hwnd_);
            } else {
                QMessageBox::warning(attached_main_, QStringLiteral("Patch Install"),
                                     ToQString(merge_error));
            }
            return;
        }

        const int install_count = static_cast<int>(install_paths.size());
        int installed = 0;

        for (size_t i = 0; i < install_paths.size(); ++i) {
            if (IsCancelled()) {
                FinishCancelled();
                return;
            }

            const auto& pkg_path = install_paths[i];
            const QString file_name = QString::fromStdWString(pkg_path.filename().wstring());
            panel_->SetStatus(QStringLiteral("Installing %1 (%2 of %3)…")
                                  .arg(file_name)
                                  .arg(i + 1)
                                  .arg(install_count),
                              -1, true);
            QCoreApplication::processEvents(QEventLoop::AllEvents);

            std::string install_error;
            if (PkgInstaller::InstallPkgFiles(
                    parent_hwnd_, {pkg_path},
                    [this, file_name, i, install_count](const PkgInstaller::InstallProgress& state) {
                        if (!panel_) {
                            return;
                        }
                        if (state.stage == "files" && state.file_count > 0) {
                            panel_->SetStatus(QStringLiteral("Installing %1 — %2 (%3 of %4)…")
                                                  .arg(file_name)
                                                  .arg(state.file_name.empty()
                                                           ? QStringLiteral("files")
                                                           : ToQString(std::string(state.file_name)))
                                                  .arg(state.file_index + 1)
                                                  .arg(state.file_count),
                                              -1, true);
                        } else {
                            panel_->SetStatus(QStringLiteral("Installing %1 (%2 of %3)…")
                                                  .arg(file_name)
                                                  .arg(i + 1)
                                                  .arg(install_count),
                                              -1, true);
                        }
                        QCoreApplication::processEvents(QEventLoop::AllEvents);
                    },
                    &install_error, [this]() { return IsCancelled(); })) {
                ++installed;
            } else if (IsCancelled()) {
                FinishCancelled();
                return;
            } else if (PkgPasscode::IsRequiredError(install_error)) {
                panel_->SetStatus(QString::fromUtf8(PkgPasscode::RequiredErrorMessage()), 0, false);
                FinishJob(true);
                QMessageBox::warning(attached_main_, QStringLiteral("PKG Passcode Required"),
                                     QString::fromUtf8(PkgPasscode::RequiredErrorMessage()));
                return;
            }
        }

        if (IsCancelled()) {
            FinishCancelled();
            return;
        }

        if (installed == install_count) {
            panel_->SetStatus(QStringLiteral("Patch v%1 installed successfully (%2 PKG%3).")
                                  .arg(ToQString(patch_.version))
                                  .arg(installed)
                                  .arg(installed == 1 ? QString() : QStringLiteral("s")),
                              100, false);
            FinishJob(false);
        } else if (installed > 0) {
            panel_->SetStatus(QStringLiteral("Installed %1 of %2 PKG(s). Remaining files saved to:\n%3")
                                  .arg(installed)
                                  .arg(downloaded_pkgs_.size())
                                  .arg(download_dir_),
                              100, false);
            FinishJob(true);
        } else {
            panel_->SetStatus(
                QStringLiteral("Could not install patch PKG(s).\n\nSaved to:\n%1").arg(download_dir_),
                100, false);
            FinishJob(true);
        }
    }

    void CleanupDownloads() {
        for (const auto& path : downloaded_pkgs_) {
            std::error_code remove_ec;
            std::filesystem::remove(path, remove_ec);
        }
        downloaded_pkgs_.clear();

        if (!download_dir_.isEmpty()) {
            std::error_code remove_ec;
            std::filesystem::remove_all(std::filesystem::path(download_dir_.toStdWString()),
                                       remove_ec);
            download_dir_.clear();
        }
    }

    void FinishCancelled() {
        CleanupDownloads();
        if (panel_) {
            panel_->SetStatus(QStringLiteral("Cancelled."), 0, false);
        }
        FinishJob(false);
    }

    void FinishJob(bool show_install_retry) {
        job_active_ = false;
        blocked_titleid_.clear();
        if (!panel_) {
            return;
        }
        panel_->SetBusyUi(false);
        const bool show_retry = show_install_retry && !download_only_;
        panel_->SetRetryVisible(show_retry, show_retry && !downloaded_pkgs_.empty());
    }

    QMainWindow* attached_main_ = nullptr;
    PatchDownloadPanel* panel_ = nullptr;
    PlayGuardFilter* play_guard_ = nullptr;
    OrbisPatches::PatchEntry patch_;
    std::string titleid_;
    std::string game_name_;
    std::string blocked_titleid_;
    HWND parent_hwnd_ = nullptr;
    bool download_only_ = false;
    bool job_active_ = false;
    bool cancel_requested_ = false;
    QString download_dir_;
    std::vector<std::filesystem::path> downloaded_pkgs_;
};

} // namespace

void EnsureAttached(QMainWindow* main_window) {
    PatchDownloadService::Instance().Attach(main_window);
}

bool IsBusy() {
    return PatchDownloadService::Instance().IsBusy();
}

bool ShouldBlockPlay(QMainWindow* main_window) {
    PatchDownloadService& service = PatchDownloadService::Instance();
    if (!service.IsBusy() || service.BlockedTitleId().empty()) {
        return false;
    }

    const QString selected = QtLauncherUtil::SelectedGameSerial(main_window);
    if (selected.isEmpty()) {
        return true;
    }
    return selected.compare(QString::fromStdString(service.BlockedTitleId()), Qt::CaseInsensitive) ==
           0;
}

void Start(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
           const std::string& game_name, HWND parent_hwnd, QWidget* /*parent_widget*/,
           bool download_only) {
    QMainWindow* main_window = QtLauncherUtil::FindMainWindow();
    if (!main_window && parent_hwnd) {
        if (QWidget* widget = QWidget::find(reinterpret_cast<WId>(parent_hwnd))) {
            main_window = qobject_cast<QMainWindow*>(widget->window());
        }
    }

    if (!main_window) {
        QMessageBox::warning(nullptr, QStringLiteral("Patch Download"),
                             QStringLiteral("Could not find the shadPS4 launcher window."));
        return;
    }

    PatchDownloadService::Instance().Attach(main_window);
    PatchDownloadService::Instance().Start(patch, titleid, game_name, parent_hwnd, download_only);
}

} // namespace PatchDownload
