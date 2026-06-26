#include "hook/qt_menu_hook.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include "hook/window_finder.h"
#include "install/pkg_installer.h"
#include "orbispatches/patch_browser_dialog.h"

namespace QtMenuHook {

namespace {

QString NormalizeMenuLabel(const QString& text) {
    QString out = text;
    out.remove(QLatin1Char('&'));
    return out;
}

QMenu* FindFileMenu(QMenuBar* bar) {
    if (!bar) {
        return nullptr;
    }

    for (QAction* action : bar->actions()) {
        if (!action || !action->menu()) {
            continue;
        }
        if (NormalizeMenuLabel(action->text()).compare(QLatin1String("File"),
                                                       Qt::CaseInsensitive) == 0) {
            return action->menu();
        }
    }

    const auto actions = bar->actions();
    if (!actions.isEmpty() && actions.front()->menu()) {
        return actions.front()->menu();
    }
    return nullptr;
}

bool MenuHasAction(QMenu* file_menu, const char* prefix) {
    for (QAction* action : file_menu->actions()) {
        if (NormalizeMenuLabel(action->text()).startsWith(QLatin1String(prefix),
                                                          Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void InstallOnMainWindow(QMainWindow* main_window) {
    if (!main_window) {
        return;
    }

    QMenu* file_menu = FindFileMenu(main_window->menuBar());
    if (!file_menu) {
        return;
    }

    const bool has_install = MenuHasAction(file_menu, "Install Packages");
    const bool has_patches = MenuHasAction(file_menu, "Download Patches");
    if (has_install && has_patches) {
        return;
    }

    QAction* exit_action = nullptr;
    for (QAction* action : file_menu->actions()) {
        if (NormalizeMenuLabel(action->text()).compare(QLatin1String("Exit"),
                                                       Qt::CaseInsensitive) == 0) {
            exit_action = action;
            break;
        }
    }

    auto* install_action = new QAction(QStringLiteral("Install Packages (PKG)"), main_window);
    QObject::connect(install_action, &QAction::triggered, main_window, [main_window]() {
        PkgInstaller::RunInstallDialog(reinterpret_cast<HWND>(main_window->winId()));
    });

    auto* patch_action = new QAction(QStringLiteral("Download Patches (ORBISPatches)..."), main_window);
    QObject::connect(patch_action, &QAction::triggered, main_window, [main_window]() {
        PatchBrowser::RunDialog(reinterpret_cast<HWND>(main_window->winId()));
    });

    if (exit_action) {
        if (!has_patches) {
            file_menu->insertAction(exit_action, patch_action);
        }
        if (!has_install) {
            file_menu->insertAction(exit_action, install_action);
        }
        if (!has_install || !has_patches) {
            file_menu->insertSeparator(exit_action);
        }
    } else {
        file_menu->addSeparator();
        if (!has_patches) {
            file_menu->addAction(patch_action);
        }
        if (!has_install) {
            file_menu->addAction(install_action);
        }
    }
}

void InstallOnGuiThread() {
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        return;
    }

    for (QWidget* widget : app->topLevelWidgets()) {
        if (auto* main_window = qobject_cast<QMainWindow*>(widget)) {
            InstallOnMainWindow(main_window);
        }
    }
}

} // namespace

bool TryInstallMenuItem() {
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return false;
    }

    if (QThread::currentThread() == app->thread()) {
        InstallOnGuiThread();
        return true;
    }

    QTimer::singleShot(0, app, InstallOnGuiThread);
    return true;
}

} // namespace QtMenuHook
