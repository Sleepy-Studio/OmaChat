#pragma once

#include "Harness.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickWindow>
#include <QTest>
#include <cmath>
#include <cstdio>

namespace omachat::test {
// Shortcuts use native window activation, unlike direct QTest key delivery.
// Reacquire this fixture's surface immediately before shortcut input.
inline bool activateNativeWindow(QQuickWindow* window)
{
    if (!qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND"))
        return true;
    return waitFor([&] {
        if (window->isActive())
            return true;
        QProcess query;
        query.start("hyprctl", {"-j", "clients"});
        if (!query.waitForFinished(1000) || query.exitCode() != 0)
            return false;
        for (const auto& value : QJsonDocument::fromJson(query.readAllStandardOutput()).array()) {
            const auto client = value.toObject();
            if (client.value("pid").toInteger() == QCoreApplication::applicationPid()
                && client.value("title").toString() == window->title() && client.value("mapped").toBool()) {
                const QString address = "address:" + client.value("address").toString();
                QProcess::execute(
                    "hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(address)});
                break;
            }
        }
        return false;
    });
}
// One process creates successive windows. Resolve the current surface rather
// than asking the compositor to pick an arbitrary window with that PID.
inline bool prepareNativeWindow(QQuickWindow* window, int width, int height)
{
    if (!qEnvironmentVariableIsSet("OMACHAT_TEST_WAYLAND"))
        return std::abs(window->width() - width) <= 1 && std::abs(window->height() - height) <= 1;
    window->setMinimumSize(QSize(width, height));
    window->setMaximumSize(QSize(width, height));
    window->resize(width, height);
    static int sequence = 0;
    const QString title = QString("OmaChat crop test %1 %2").arg(QCoreApplication::applicationPid()).arg(++sequence);
    window->setTitle(title);
    if (!QTest::qWaitForWindowExposed(window))
        return false;
    QString configuredAddress;
    int settled = 0;
    QJsonObject observed;
    const bool ready = waitFor([&] {
        QProcess query;
        query.start("hyprctl", {"-j", "clients"});
        if (!query.waitForFinished(1000) || query.exitCode() != 0)
            return false;
        QString address;
        QJsonArray size;
        for (const auto& value : QJsonDocument::fromJson(query.readAllStandardOutput()).array()) {
            const auto client = value.toObject();
            if (client.value("pid").toInteger() == QCoreApplication::applicationPid()
                && client.value("title").toString() == title && client.value("mapped").toBool()) {
                address = "address:" + client.value("address").toString();
                size = client.value("size").toArray();
                observed = {{"compositor_size", size}, {"floating", client.value("floating")},
                    {"qt_width", window->width()}, {"qt_height", window->height()}, {"active", window->isActive()},
                    {"expected_width", width}, {"expected_height", height}, {"title", title}};
                break;
            }
        }
        if (address.isEmpty())
            return false;
        if (configuredAddress != address) {
            if (QProcess::execute("hyprctl",
                    {"eval", QString("hl.dispatch(hl.dsp.window.float({action='set',window='%1'}))").arg(address)})
                != 0)
                return false;
            configuredAddress = address;
            settled = 0;
        }
        const bool correctSize = size.size() == 2 && std::abs(size[0].toInt() - width) <= 1
            && std::abs(size[1].toInt() - height) <= 1 && std::abs(window->width() - width) <= 1
            && std::abs(window->height() - height) <= 1;
        if (!correctSize) {
            settled = 0;
            QProcess::execute("hyprctl",
                {"eval",
                    QString("hl.dispatch(hl.dsp.window.resize({x=%1,y=%2,relative=false,window='%3'}))")
                        .arg(width)
                        .arg(height)
                        .arg(address)});
            return false;
        }
        if (!window->isActive()) {
            settled = 0;
            QProcess::execute("hyprctl", {"eval", QString("hl.dispatch(hl.dsp.focus({window='%1'}))").arg(address)});
            return false;
        }
        // Observe multiple event-loop/compositor turns without issuing another
        // geometry command, so a queued configure cannot invalidate the check.
        return ++settled >= 3;
    });
    if (!ready)
        fprintf(
            stderr, "Native window setup: %s\n", QJsonDocument(observed).toJson(QJsonDocument::Compact).constData());
    return ready;
}
} // namespace omachat::test
