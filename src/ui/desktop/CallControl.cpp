#include "ui/desktop/CallControl.h"

#include "core/service/CallMonitor.h"
#include "ui/PhoneNumberText.h"

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::service::CallMonitor;

} // namespace

CallControl::CallControl(CallMonitor *monitor, QObject *parent)
    : QObject(parent), m_monitor(monitor)
{
    Q_ASSERT(monitor);
    connect(monitor, &CallMonitor::callChanged, this, &CallControl::callChanged);
    connect(monitor, &CallMonitor::mutedChanged, this, &CallControl::mutedChanged);
    connect(monitor, &CallMonitor::speakerChanged, this, &CallControl::speakerChanged);
}

CallControl::State CallControl::state() const
{
    switch (m_monitor->state()) {
    case CallMonitor::State::Ringing:
        return State::Ringing;
    case CallMonitor::State::Dialling:
        return State::Dialling;
    case CallMonitor::State::Active:
        return State::Active;
    case CallMonitor::State::Idle:
        break;
    }
    return State::Idle;
}

QString CallControl::callerText() const
{
    if (!m_monitor->name().isEmpty())
        return m_monitor->name();
    if (!m_monitor->number().isEmpty())
        return PhoneNumberText::display(m_monitor->number());
    return tr("Unknown number");
}

QString CallControl::numberText() const
{
    if (m_monitor->name().isEmpty())
        return {};
    return PhoneNumberText::display(m_monitor->number());
}

QString CallControl::statusText() const
{
    switch (state()) {
    case State::Ringing:
        return tr("Incoming call");
    case State::Dialling:
        return tr("Calling");
    case State::Active:
        return tr("Call in progress");
    case State::Idle:
        break;
    }
    return tr("No call");
}

bool CallControl::isMuted() const
{
    return m_monitor->isMuted();
}

bool CallControl::isSpeakerOn() const
{
    return m_monitor->isSpeakerOn();
}

bool CallControl::answer()
{
    if (state() != State::Ringing)
        return false;
    m_monitor->answer().then(this, [this](const Result<void> &answered) {
        if (!answered.hasValue())
            Q_EMIT errorOccurred(messageFor(answered.error()));
    });
    return true;
}

bool CallControl::hangUp()
{
    if (!isActive())
        return false;
    m_monitor->hangUp().then(this, [this](const Result<void> &ended) {
        if (!ended.hasValue())
            Q_EMIT errorOccurred(messageFor(ended.error()));
    });
    return true;
}

bool CallControl::setMuted(bool muted)
{
    if (state() != State::Active)
        return false;
    m_monitor->setMuted(muted).then(this, [this](const Result<void> &switched) {
        if (!switched.hasValue())
            Q_EMIT errorOccurred(messageFor(switched.error()));
    });
    return true;
}

bool CallControl::setSpeakerOn(bool on)
{
    if (state() != State::Active)
        return false;
    m_monitor->setSpeaker(on).then(this, [this](const Result<void> &switched) {
        if (!switched.hasValue())
            Q_EMIT errorOccurred(messageFor(switched.error()));
    });
    return true;
}

QString CallControl::messageFor(const Error &error) const
{
    switch (error.code()) {
    case ErrorCode::ConnectionClosed:
        return tr("The phone is not connected. Use the phone itself for this call.");
    case ErrorCode::PermissionDenied:
        return tr("MyPhoneExplorer Client may not control calls. Allow it the phone permission "
                  "in the Android settings.");
    case ErrorCode::Timeout:
        return tr("The phone does not answer. Use the phone itself for this call.");
    default:
        break;
    }
    return tr("The phone did not do that. Use the phone itself for this call.");
}

} // namespace qpm::ui
