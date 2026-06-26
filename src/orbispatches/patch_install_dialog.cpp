#include "orbispatches/patch_install_dialog.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>

#include "install/pkg_installer.h"
#include "orbispatches/orbispatches_client.h"
#include "orbispatches/patch_link_fetcher.h"

namespace PatchInstall {

namespace {

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

std::filesystem::path PatchDownloadDirectory(const std::string& titleid,
                                             const std::string& version) {
    wchar_t appdata[MAX_PATH] = {};
    std::filesystem::path root;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata))) {
        root = std::filesystem::path(appdata) / L"shadPS4" / L"update_patches";
    } else {
        root = std::filesystem::temp_directory_path() / L"ShadPS4PkgPlugin" / L"update_patches";
    }
    return root / (QString::fromStdString(titleid).toStdWString() + L"_" +
                   QString::fromStdString(version).toStdWString());
}

std::filesystem::path PiecePath(const std::filesystem::path& directory,
                                const OrbisPatches::PatchPiece& piece, size_t index) {
    std::wstring filename =
        L"piece_" + std::to_wstring(piece.order > 0 ? piece.order : index + 1) + L".pkg";
    return directory / filename;
}

class PatchInstallDialog final : public QDialog {
    Q_OBJECT

public:
    PatchInstallDialog(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
                       HWND parent_hwnd, QWidget* parent)
        : QDialog(parent), patch_(patch), titleid_(titleid), parent_hwnd_(parent_hwnd) {
        setWindowTitle(QStringLiteral("Download & Install Patch — %1 v%2")
                           .arg(ToQString(titleid_), ToQString(patch.version)));
        setMinimumWidth(620);
        resize(680, 210);

        auto* root = new QVBoxLayout(this);
        status_label_ = new QLabel(QStringLiteral("Preparing…"), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        progress_bar_ = new QProgressBar(this);
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(0);
        progress_bar_->setTextVisible(true);
        root->addWidget(progress_bar_);

        auto* buttons = new QHBoxLayout();
        open_folder_button_ = new QPushButton(QStringLiteral("Open download folder"), this);
        retry_install_button_ = new QPushButton(QStringLiteral("Install downloaded PKGs"), this);
        close_button_ = new QPushButton(QStringLiteral("Close"), this);
        open_folder_button_->setEnabled(false);
        retry_install_button_->setEnabled(false);
        close_button_->setEnabled(false);
        buttons->addWidget(open_folder_button_);
        buttons->addWidget(retry_install_button_);
        buttons->addStretch();
        buttons->addWidget(close_button_);
        root->addLayout(buttons);

        connect(open_folder_button_, &QPushButton::clicked, this, &PatchInstallDialog::OpenDownloadFolder);
        connect(retry_install_button_, &QPushButton::clicked, this, &PatchInstallDialog::InstallDownloadedPkgs);
        connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);

        QTimer::singleShot(0, this, [this]() { RunJob(); });
    }

private slots:
    void OpenDownloadFolder() {
        if (download_dir_.isEmpty()) {
            return;
        }
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(download_dir_.utf16()), nullptr,
                      nullptr, SW_SHOWNORMAL);
    }

    void InstallDownloadedPkgs() {
        if (downloaded_pkgs_.empty()) {
            return;
        }
        InstallAllPkgs();
    }

private:
    void SetStatus(const QString& status, int percent = -1, bool indeterminate = false) {
        status_label_->setText(status);
        if (indeterminate) {
            progress_bar_->setRange(0, 0);
            progress_bar_->setFormat(status);
        } else {
            progress_bar_->setRange(0, 100);
            progress_bar_->setValue(qMax(0, percent));
            progress_bar_->setFormat(percent >= 0 ? QStringLiteral("%p% — %1").arg(status) : status);
        }
    }

    void EnablePostDownloadActions() {
        open_folder_button_->setEnabled(!download_dir_.isEmpty());
        retry_install_button_->setEnabled(!downloaded_pkgs_.empty());
        close_button_->setEnabled(true);
    }

  void InstallAllPkgs() {
        int installed = 0;
        for (size_t i = 0; i < downloaded_pkgs_.size(); ++i) {
            const auto& pkg_path = downloaded_pkgs_[i];
            const QString file_name = QString::fromStdWString(pkg_path.filename().wstring());
            SetStatus(QStringLiteral("Installing %1 (%2 of %3)…")
                          .arg(file_name)
                          .arg(i + 1)
                          .arg(downloaded_pkgs_.size()),
                      -1, true);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

            if (PkgInstaller::InstallPkgFile(parent_hwnd_, pkg_path)) {
                ++installed;
            }
        }

        if (installed == static_cast<int>(downloaded_pkgs_.size())) {
            SetStatus(QStringLiteral("Patch v%1 installed successfully (%2 PKG%3).")
                          .arg(ToQString(patch_.version))
                          .arg(installed)
                          .arg(installed == 1 ? QString() : QStringLiteral("s")),
                      100, false);
        } else if (installed > 0) {
            SetStatus(
                QStringLiteral("Installed %1 of %2 PKG(s). Remaining files are in:\n%3\n\nUse "
                               "File -> Install Packages (PKG) after installing the base game.")
                    .arg(installed)
                    .arg(downloaded_pkgs_.size())
                    .arg(download_dir_),
                100, false);
        } else {
            SetStatus(
                QStringLiteral("Patch PKG(s) downloaded but not installed.\n\nSaved to:\n%1\n\n"
                               "Install the base game, then use File -> Install Packages (PKG) or "
                               "click Install downloaded PKGs here.")
                    .arg(download_dir_),
                100, false);
        }
        EnablePostDownloadActions();
    }

    void RunJob() {
        std::vector<OrbisPatches::PatchPiece> pieces;
        std::string error;
        SetStatus(QStringLiteral("Fetching patch download links from ORBISPatches…"), -1, true);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        if (!OrbisPatches::PatchLinkFetcher::FetchPieces(
                titleid_, patch_.version, patch_.patch_key, pieces, error,
                reinterpret_cast<HWND>(winId()),
                []() { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); })) {
            SetStatus(ToQString(error), 0, false);
            close_button_->setEnabled(true);
            QMessageBox::warning(this, QStringLiteral("Patch Download"), ToQString(error));
            return;
        }

        const auto download_dir = PatchDownloadDirectory(titleid_, patch_.version);
        download_dir_ = QString::fromStdWString(download_dir.wstring());
        std::error_code ec;
        std::filesystem::create_directories(download_dir, ec);

        downloaded_pkgs_.clear();
        downloaded_pkgs_.reserve(pieces.size());

        for (size_t i = 0; i < pieces.size(); ++i) {
            const auto& piece = pieces[i];
            const auto destination = PiecePath(download_dir, piece, i);
            const QString piece_label =
                QStringLiteral("piece %1 of %2")
                    .arg(piece.order > 0 ? piece.order : static_cast<int>(i + 1))
                    .arg(pieces.size());

            SetStatus(QStringLiteral("Downloading %1…").arg(piece_label), 0, false);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

            const bool ok = OrbisPatches::Client::DownloadUrl(
                piece.pkg_url, destination,
                [this, piece_label](int64_t received, int64_t total) {
                    if (total > 0) {
                        SetStatus(QStringLiteral("Downloading %1 (%2 / %3)")
                                      .arg(piece_label, FormatBytes(received), FormatBytes(total)),
                                  static_cast<int>((received * 100) / total), false);
                    } else {
                        SetStatus(QStringLiteral("Downloading %1 (%2 received…)")
                                      .arg(piece_label, FormatBytes(received)),
                                  -1, true);
                    }
                    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                },
                error);

            if (!ok) {
                SetStatus(ToQString(error), 0, false);
                EnablePostDownloadActions();
                QMessageBox::warning(this, QStringLiteral("Patch Download"), ToQString(error));
                return;
            }

            downloaded_pkgs_.push_back(destination);
        }

        SetStatus(QStringLiteral("Download complete. Saved to:\n%1").arg(download_dir_), 100,
                  false);
        EnablePostDownloadActions();
        InstallAllPkgs();
    }

    OrbisPatches::PatchEntry patch_;
    std::string titleid_;
    HWND parent_hwnd_ = nullptr;
    QString download_dir_;
    std::vector<std::filesystem::path> downloaded_pkgs_;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* open_folder_button_ = nullptr;
    QPushButton* retry_install_button_ = nullptr;
    QPushButton* close_button_ = nullptr;
};

} // namespace

void Run(const OrbisPatches::PatchEntry& patch, const std::string& titleid, HWND parent_hwnd,
         QWidget* parent_widget) {
    auto* dialog = new PatchInstallDialog(patch, titleid, parent_hwnd, parent_widget);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

} // namespace PatchInstall

#include "patch_install_dialog.moc"
