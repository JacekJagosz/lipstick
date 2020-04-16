/***************************************************************************
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (c) 2012 Jolla Ltd.
**
** This file is part of lipstick.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/
#include <QGuiApplication>
#include <QDBusContext>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include "homewindow.h"
#include <QQmlContext>
#include <QScreen>
#include "utilities/closeeventeater.h"
#include "notifications/notificationmanager.h"
#include "notifications/lipsticknotification.h"
#include "homeapplication.h"
#include "shutdownscreen.h"
#include "lipstickqmlpath.h"

ShutdownScreen::ShutdownScreen(QObject *parent) :
    QObject(parent),
    QDBusContext(),
    m_window(0),
    m_systemState(new DeviceState::DeviceState(this))
{
    connect(m_systemState, SIGNAL(systemStateChanged(DeviceState::DeviceState::StateIndication)), this, SLOT(applySystemState(DeviceState::DeviceState::StateIndication)));
}

void ShutdownScreen::setWindowVisible(bool visible)
{
    if (visible) {
        if (m_window == 0) {
            m_window = new HomeWindow();
            m_window->setGeometry(QRect(QPoint(), QGuiApplication::primaryScreen()->size()));
            m_window->setCategory(QLatin1String("notification"));
            m_window->setWindowTitle("Shutdown");
            m_window->setContextProperty("initialSize", QGuiApplication::primaryScreen()->size());
            m_window->setContextProperty("shutdownScreen", this);
            m_window->setContextProperty("shutdownMode", m_shutdownMode);
            m_window->setSource(QmlPath::to("system/ShutdownScreen.qml"));
            m_window->installEventFilter(new CloseEventEater(this));
        }

        if (!m_window->isVisible()) {
            m_window->show();
            emit windowVisibleChanged();
        }
    } else if (m_window != 0 && m_window->isVisible()) {
        m_window->hide();
        emit windowVisibleChanged();
    }
}

bool ShutdownScreen::windowVisible() const
{
    return m_window != 0 && m_window->isVisible();
}

void ShutdownScreen::applySystemState(DeviceState::DeviceState::StateIndication what)
{
    switch (what) {
        case DeviceState::DeviceState::Shutdown:
            // To avoid early quitting on shutdown
            HomeApplication::instance()->restoreSignalHandlers();
            setWindowVisible(true);
            break;

        case DeviceState::DeviceState::ThermalStateFatal:
            //% "Temperature too high. Device shutting down."
            createAndPublishNotification("x-nemo.battery.temperature", qtTrId("qtn_shut_high_temp"));
            break;

        case DeviceState::DeviceState::ShutdownDeniedUSB:
            //% "USB cable plugged in. Unplug the USB cable to shutdown."
            createAndPublishNotification("device.added", qtTrId("qtn_shut_unplug_usb"));
            break;

        case DeviceState::DeviceState::BatteryStateEmpty:
            //% "Battery empty. Device shutting down."
            createAndPublishNotification("x-nemo.battery.shutdown", qtTrId("qtn_shut_batt_empty"));
            break;

        case DeviceState::DeviceState::Reboot:
            // Set shutdown mode unless already set explicitly
            if (m_shutdownMode.isEmpty()) {
                m_shutdownMode = "reboot";
                m_window->setContextProperty("shutdownMode", m_shutdownMode);
            }
            break;

        default:
            break;
    }
}

void ShutdownScreen::createAndPublishNotification(const QString &category, const QString &body)
{
    NotificationManager *manager = NotificationManager::instance();
    QVariantHash hints;
    hints.insert(LipstickNotification::HINT_CATEGORY, category);
    hints.insert(LipstickNotification::HINT_PREVIEW_BODY, body);
    manager->Notify(manager->systemApplicationName(), 0, QString(), QString(), QString(), QStringList(), hints, -1);
}

void ShutdownScreen::setShutdownMode(const QString &mode)
{
    if (!isPrivileged())
        return;

    m_shutdownMode = mode;
    applySystemState(DeviceState::DeviceState::Shutdown);
}

bool ShutdownScreen::isPrivileged()
{
    if (!calledFromDBus()) {
        // Local function calls are always privileged
        return true;
    }

    // Get the PID of the calling process
    pid_t pid = connection().interface()->servicePid(message().service());

    // The /proc/<pid> directory is owned by EUID:EGID of the process
    QFileInfo info(QString("/proc/%1").arg(pid));
    if (info.group() != "privileged" && info.owner() != "root") {
        sendErrorReply(QDBusError::AccessDenied,
                QString("PID %1 is not in privileged group").arg(pid));
        return false;
    }

    return true;
}
