#include "hook/qt_directory_hook.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "config/shad_config.h"

namespace QtDirectoryHook {

namespace {

QString NormalizeLabel(const QString& text) {
    QString out = text;
    out.remove(QLatin1Char('&'));
    return out.trimmed();
}

bool IsChooseDirectoryDialog(const QDialog* dialog) {
    if (!dialog) {
        return false;
    }

    const QString title = NormalizeLabel(dialog->windowTitle());
    if (title.contains(QStringLiteral("Choose directory"), Qt::CaseInsensitive)) {
        return true;
    }

    const QList<QGroupBox*> groups = dialog->findChildren<QGroupBox*>();
    if (groups.size() < 3) {
        return false;
    }

    bool has_games = false;
    bool has_dlc = false;
    for (const QGroupBox* group : groups) {
        const QString label = NormalizeLabel(group->title());
        if (label.contains(QStringLiteral("games"), Qt::CaseInsensitive)) {
            has_games = true;
        }
        if (label.contains(QStringLiteral("DLC"), Qt::CaseInsensitive)) {
            has_dlc = true;
        }
    }
    return has_games && has_dlc;
}

void SavePatchDirectory(const QString& native_path) {
    const QString trimmed = native_path.trimmed();
    if (trimmed.isEmpty() || !QDir::isAbsolutePath(trimmed)) {
        return;
    }

    QDir dir(trimmed);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return;
    }

    ShadConfig::SaveUpdatePatchesDir(std::filesystem::path(trimmed.toStdWString()));
}

void InjectUpdatePatchesRow(QDialog* dialog) {
    if (!dialog || dialog->property("pkgPluginDirectoryPatched").toBool()) {
        return;
    }

    auto* layout = qobject_cast<QVBoxLayout*>(dialog->layout());
    if (!layout) {
        return;
    }

    if (!IsChooseDirectoryDialog(dialog)) {
        return;
    }

    auto* group = new QGroupBox(QStringLiteral("Directory with update patches"), dialog);
    auto* row = new QHBoxLayout(group);

    auto* path_edit = new QLineEdit(group);
    path_edit->setObjectName(QStringLiteral("pkgPluginUpdatePatchesDir"));
    path_edit->setMinimumWidth(400);
    const auto configured = ShadConfig::GetUpdatePatchesDir();
    path_edit->setText(
        QDir::toNativeSeparators(QString::fromStdWString(configured.wstring())));

    row->addWidget(path_edit);

    auto* browse = new QPushButton(QStringLiteral("Browse"), group);
    QObject::connect(browse, &QPushButton::clicked, dialog, [dialog, path_edit]() {
        const QString selected = QFileDialog::getExistingDirectory(
            dialog, QStringLiteral("Directory with update patches"),
            path_edit->text());
        if (!selected.isEmpty()) {
            path_edit->setText(QDir::toNativeSeparators(selected));
        }
    });
    row->addWidget(browse);

    int insert_index = layout->count();
    for (int i = 0; i < layout->count(); ++i) {
        if (layout->itemAt(i)->spacerItem() != nullptr) {
            insert_index = i;
            break;
        }
    }
    layout->insertWidget(insert_index, group);

    if (auto* button_box = dialog->findChild<QDialogButtonBox*>()) {
        QObject::connect(button_box, &QDialogButtonBox::accepted, dialog,
                         [dialog, path_edit]() {
                             QTimer::singleShot(0, dialog, [dialog, path_edit]() {
                                 if (dialog->result() != QDialog::Accepted) {
                                     return;
                                 }
                                 SavePatchDirectory(path_edit->text());
                             });
                         });
    }

    dialog->setProperty("pkgPluginDirectoryPatched", true);
    dialog->adjustSize();
}

class DirectoryDialogHook final : public QObject {
    Q_OBJECT

public:
    explicit DirectoryDialogHook(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (auto* dialog = qobject_cast<QDialog*>(watched)) {
                InjectUpdatePatchesRow(dialog);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void InstallOnGuiThread() {
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        return;
    }

    static bool installed = false;
    if (installed) {
        return;
    }

    auto* hook = new DirectoryDialogHook(app);
    app->installEventFilter(hook);
    installed = true;
}

} // namespace

void TryInstall() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }

    if (QThread::currentThread() == app->thread()) {
        InstallOnGuiThread();
        return;
    }

    QTimer::singleShot(0, app, InstallOnGuiThread);
}

} // namespace QtDirectoryHook

#include "qt_directory_hook.moc"
