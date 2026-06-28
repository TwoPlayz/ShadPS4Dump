#include "orbispatches/patch_browser_dialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <thread>

#include "install/pkg_router.h"
#include "orbispatches/orbispatches_client.h"
#include "orbispatches/patch_install_dialog.h"

namespace PatchBrowser {

namespace {

QString ToQString(const std::string& text) {
    return QString::fromUtf8(text.c_str(), static_cast<int>(text.size()));
}

QWidget* FindLauncherParentWidget(HWND parent_hwnd) {
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        for (QWidget* widget : app->topLevelWidgets()) {
            if (auto* main_window = qobject_cast<QMainWindow*>(widget)) {
                return main_window;
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

class PatchBrowserDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PatchBrowserDialog(QWidget* parent, HWND parent_hwnd)
        : QDialog(parent), parent_hwnd_(parent_hwnd) {
        setWindowTitle(QStringLiteral("Download Patches (ORBISPatches)"));
        resize(760, 520);

        auto* root = new QVBoxLayout(this);

        auto* search_row = new QHBoxLayout();
        search_edit_ = new QLineEdit(this);
        search_edit_->setPlaceholderText(QStringLiteral("Search by game name or title ID (e.g. CUSA01163)"));
        auto* search_button = new QPushButton(QStringLiteral("Search"), this);
        search_row->addWidget(search_edit_);
        search_row->addWidget(search_button);
        root->addLayout(search_row);

        status_label_ = new QLabel(QStringLiteral("Search for a game, then pick a patch version."), this);
        status_label_->setWordWrap(true);
        root->addWidget(status_label_);

        auto* columns = new QHBoxLayout();
        games_list_ = new QListWidget(this);
        games_list_->setMinimumWidth(280);
        patches_list_ = new QListWidget(this);
        columns->addWidget(games_list_, 1);
        columns->addWidget(patches_list_, 2);
        root->addLayout(columns, 1);

        auto* buttons = new QHBoxLayout();
        download_button_ = new QPushButton(QStringLiteral("Download && Install"), this);
        auto* close_button = new QPushButton(QStringLiteral("Close"), this);
        buttons->addWidget(download_button_);
        buttons->addStretch();
        buttons->addWidget(close_button);
        root->addLayout(buttons);

        download_button_->setEnabled(false);

        connect(search_button, &QPushButton::clicked, this, &PatchBrowserDialog::OnSearch);
        connect(search_edit_, &QLineEdit::returnPressed, this, &PatchBrowserDialog::OnSearch);
        connect(games_list_, &QListWidget::currentRowChanged, this,
                &PatchBrowserDialog::OnGameSelectionChanged);
        connect(patches_list_, &QListWidget::currentRowChanged, this,
                &PatchBrowserDialog::OnPatchSelectionChanged);
        connect(download_button_, &QPushButton::clicked, this, &PatchBrowserDialog::OnDownload);
        connect(close_button, &QPushButton::clicked, this, &QDialog::accept);
    }

private slots:
    void OnSearch() {
        const QString term = search_edit_->text().trimmed();
        if (term.isEmpty()) {
            return;
        }

        SetBusy(true, QStringLiteral("Searching ORBISPatches..."));
        games_list_->clear();
        patches_list_->clear();
        games_.clear();
        patches_.clear();
        selected_titleid_.clear();

        std::thread([this, term]() {
            std::string error;
            std::vector<OrbisPatches::SearchResult> results;
            const std::string term_utf8 = term.toUtf8().constData();

            if (term.startsWith(QStringLiteral("CUSA"), Qt::CaseInsensitive) && term.size() == 9) {
                OrbisPatches::SearchResult direct;
                direct.titleid = term_utf8;
                direct.name = term_utf8;
                direct.region = "";
                results.push_back(std::move(direct));
            } else {
                results = OrbisPatches::Client::Search(term_utf8, error);
            }

            QMetaObject::invokeMethod(
                this,
                [this, results = std::move(results), error = std::move(error)]() mutable {
                    games_ = std::move(results);
                    for (const auto& game : games_) {
                        QString label = ToQString(game.name);
                        if (!game.region.empty()) {
                            label += QStringLiteral(" [") + ToQString(game.region) + QLatin1Char(']');
                        }
                        label += QStringLiteral(" — ") + ToQString(game.titleid);
                        games_list_->addItem(label);
                    }

                    if (games_.empty()) {
                        SetBusy(false, error.empty()
                                           ? QStringLiteral("No games found.")
                                           : ToQString(error));
                        return;
                    }

                    SetBusy(false, QStringLiteral("Select a game to load patch versions."));
                    games_list_->setCurrentRow(0);
                },
                Qt::QueuedConnection);
        }).detach();
    }

    void OnGameSelectionChanged(int row) {
        patches_list_->clear();
        patches_.clear();
        selected_titleid_.clear();
        selected_game_name_.clear();

        if (row < 0 || row >= static_cast<int>(games_.size())) {
            SetBusy(false, QStringLiteral("Select a game to load patch versions."));
            return;
        }

        const auto& game = games_[static_cast<size_t>(row)];
        selected_titleid_ = game.titleid;
        selected_game_name_ = game.name;
        SetBusy(true, QStringLiteral("Loading patches for %1...").arg(ToQString(game.titleid)));

        std::thread([this, titleid = game.titleid]() {
            std::string error;
            auto params = OrbisPatches::Client::FetchLoadParams(titleid, error);
            std::vector<OrbisPatches::PatchEntry> patches;
            if (params) {
                patches = OrbisPatches::Client::LoadPatches(params->titleid, params->key, error);
            } else if (error.empty()) {
                error = "Failed to load patch list";
            }

            QMetaObject::invokeMethod(
                this,
                [this, patches = std::move(patches), error = std::move(error)]() mutable {
                    patches_ = std::move(patches);
                    for (const auto& patch : patches_) {
                        QString label = QStringLiteral("v%1").arg(ToQString(patch.version));
                        if (patch.is_latest) {
                            label += QStringLiteral(" (latest)");
                        }
                        label += QStringLiteral(" — %1 — FW %2 — %3")
                                     .arg(ToQString(patch.filesize))
                                     .arg(ToQString(patch.required_firmware))
                                     .arg(ToQString(patch.creation_date));
                        patches_list_->addItem(label);
                    }

                    if (patches_.empty()) {
                        SetBusy(false, error.empty()
                                           ? QStringLiteral("No patches listed for this title.")
                                           : ToQString(error));
                        return;
                    }

                    SetBusy(false, QStringLiteral("%1 patch version(s) available. Select one, then Download & Install.")
                                          .arg(patches_.size()));
                    patches_list_->setCurrentRow(0);
                },
                Qt::QueuedConnection);
        }).detach();
    }

    void OnPatchSelectionChanged(int row) {
        const bool has_patch = row >= 0 && row < static_cast<int>(patches_.size());
        if (download_button_) {
            download_button_->setEnabled(has_patch);
        }
    }

    void OnDownload() {
        const int row = patches_list_->currentRow();
        if (row < 0 || row >= static_cast<int>(patches_.size()) || selected_titleid_.empty()) {
            return;
        }

        const auto& patch = patches_[static_cast<size_t>(row)];
        bool download_only = false;

        if (!PkgRouter::IsBaseGameInstalled(selected_titleid_)) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(QStringLiteral("Base Game Required"));
            box.setText(QStringLiteral("The base game for %1 is not installed.")
                            .arg(ToQString(selected_titleid_)));
            box.setInformativeText(
                QStringLiteral("Patch PKGs need the base game installed first.\n\n"
                               "Choose Download Only to save the patch files for later, or Cancel."));
            auto* download_button = box.addButton(QStringLiteral("Download Only"),
                                                  QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();

            if (box.clickedButton() != download_button) {
                return;
            }
            download_only = true;
        }

        PatchInstall::Run(patch, selected_titleid_, selected_game_name_, parent_hwnd_, this,
                          download_only);
        accept();
    }

    void SetBusy(bool busy, const QString& message) {
        status_label_->setText(message);
        search_edit_->setEnabled(!busy);
        games_list_->setEnabled(!busy);
        patches_list_->setEnabled(!busy);
        if (download_button_) {
            download_button_->setEnabled(!busy && patches_list_->currentRow() >= 0);
        }
    }

private:
    HWND parent_hwnd_ = nullptr;
    QLineEdit* search_edit_ = nullptr;
    QListWidget* games_list_ = nullptr;
    QListWidget* patches_list_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPushButton* download_button_ = nullptr;
    std::vector<OrbisPatches::SearchResult> games_;
    std::vector<OrbisPatches::PatchEntry> patches_;
    std::string selected_titleid_;
    std::string selected_game_name_;
};

} // namespace

void RunDialog(HWND parent) {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }

    auto show = [parent]() {
        QWidget* launcher = FindLauncherParentWidget(parent);
        auto* dialog = new PatchBrowserDialog(launcher, parent);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    };

    if (QThread::currentThread() == app->thread()) {
        show();
    } else {
        QMetaObject::invokeMethod(app, show, Qt::QueuedConnection);
    }
}

} // namespace PatchBrowser

#include "patch_browser_dialog.moc"
