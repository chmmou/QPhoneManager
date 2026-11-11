#include "ui/contacts/ContactTypes.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::codec::ContactValueKind;
namespace codec = core::codec;

struct Choice
{
    int code;
    QString name;
};

QList<Choice> choices(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return {
            {codec::phoneType::mobile, ContactTypes::tr("Mobile")},
            {codec::phoneType::home, ContactTypes::tr("Home")},
            {codec::phoneType::work, ContactTypes::tr("Work")},
            {codec::phoneType::workMobile, ContactTypes::tr("Work mobile")},
            {codec::phoneType::main, ContactTypes::tr("Main number")},
            {codec::phoneType::companyMain, ContactTypes::tr("Company")},
            {codec::phoneType::faxWork, ContactTypes::tr("Work fax")},
            {codec::phoneType::faxHome, ContactTypes::tr("Home fax")},
            {codec::phoneType::otherFax, ContactTypes::tr("Other fax")},
            {codec::phoneType::pager, ContactTypes::tr("Pager")},
            {codec::phoneType::workPager, ContactTypes::tr("Work pager")},
            {codec::phoneType::car, ContactTypes::tr("Car")},
            {codec::phoneType::callback, ContactTypes::tr("Callback")},
            {codec::phoneType::assistant, ContactTypes::tr("Assistant")},
            {codec::phoneType::isdn, ContactTypes::tr("ISDN")},
            {codec::phoneType::radio, ContactTypes::tr("Radio")},
            {codec::phoneType::telex, ContactTypes::tr("Telex")},
            {codec::phoneType::tty, ContactTypes::tr("Text telephone")},
            {codec::phoneType::mms, ContactTypes::tr("MMS")},
            {codec::phoneType::other, ContactTypes::tr("Other")},
            {codec::phoneType::custom, ContactTypes::tr("Own name")},
        };
    case ContactValueKind::Email:
        return {
            {codec::emailType::home, ContactTypes::tr("Home")},
            {codec::emailType::work, ContactTypes::tr("Work")},
            {codec::emailType::mobile, ContactTypes::tr("Mobile")},
            {codec::emailType::other, ContactTypes::tr("Other")},
            {codec::emailType::custom, ContactTypes::tr("Own name")},
        };
    case ContactValueKind::Address:
        return {
            {codec::addressType::home, ContactTypes::tr("Home")},
            {codec::addressType::work, ContactTypes::tr("Work")},
            {codec::addressType::other, ContactTypes::tr("Other")},
            {codec::addressType::custom, ContactTypes::tr("Own name")},
        };
    case ContactValueKind::Website:
        return {
            {codec::websiteType::homepage, ContactTypes::tr("Homepage")},
            {codec::websiteType::home, ContactTypes::tr("Home")},
            {codec::websiteType::work, ContactTypes::tr("Work")},
            {codec::websiteType::blog, ContactTypes::tr("Blog")},
            {codec::websiteType::profile, ContactTypes::tr("Profile")},
            {codec::websiteType::ftp, ContactTypes::tr("FTP")},
            {codec::websiteType::other, ContactTypes::tr("Other")},
            {codec::websiteType::custom, ContactTypes::tr("Own name")},
        };
    case ContactValueKind::Im:
        return {
            {codec::imProtocol::jabber, ContactTypes::tr("Jabber")},
            {codec::imProtocol::skype, ContactTypes::tr("Skype")},
            {codec::imProtocol::googleTalk, ContactTypes::tr("Google Talk")},
            {codec::imProtocol::icq, ContactTypes::tr("ICQ")},
            {codec::imProtocol::aim, ContactTypes::tr("AIM")},
            {codec::imProtocol::msn, ContactTypes::tr("Windows Live")},
            {codec::imProtocol::yahoo, ContactTypes::tr("Yahoo")},
            {codec::imProtocol::qq, ContactTypes::tr("QQ")},
            {codec::imProtocol::netMeeting, ContactTypes::tr("NetMeeting")},
            {codec::imProtocol::custom, ContactTypes::tr("Own name")},
        };
    case ContactValueKind::Event:
        break;
    }
    return {
        {codec::eventType::birthday, ContactTypes::tr("Birthday")},
        {codec::eventType::anniversary, ContactTypes::tr("Anniversary")},
        {codec::eventType::other, ContactTypes::tr("Other")},
        {codec::eventType::custom, ContactTypes::tr("Own name")},
    };
}

int otherCode(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return codec::phoneType::other;
    case ContactValueKind::Email:
        return codec::emailType::other;
    case ContactValueKind::Address:
        return codec::addressType::other;
    case ContactValueKind::Website:
        return codec::websiteType::other;
    case ContactValueKind::Im:
        return codec::imProtocol::custom;
    case ContactValueKind::Event:
        break;
    }
    return codec::eventType::other;
}

} // namespace

QList<int> ContactTypes::codes(ContactValueKind kind)
{
    QList<int> result;
    const QList<Choice> all = choices(kind);
    for (const Choice &choice : all)
        result.append(choice.code);
    return result;
}

QStringList ContactTypes::names(ContactValueKind kind)
{
    QStringList result;
    const QList<Choice> all = choices(kind);
    for (const Choice &choice : all)
        result.append(choice.name);
    return result;
}

int ContactTypes::indexOf(ContactValueKind kind, int type)
{
    const QList<int> list = codes(kind);
    const qsizetype index = list.indexOf(type);
    return int(index >= 0 ? index : list.indexOf(otherCode(kind)));
}

int ContactTypes::customCode(ContactValueKind kind)
{
    return kind == ContactValueKind::Im ? codec::imProtocol::custom : 0;
}

QString ContactTypes::label(ContactValueKind kind, int type, const QString &customLabel)
{
    if (type == customCode(kind) && !customLabel.trimmed().isEmpty())
        return customLabel.trimmed();
    return names(kind).value(indexOf(kind, type));
}

QString ContactTypes::title(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return tr("Phone");
    case ContactValueKind::Email:
        return tr("Email");
    case ContactValueKind::Address:
        return tr("Address");
    case ContactValueKind::Website:
        return tr("Website");
    case ContactValueKind::Im:
        return tr("Messenger");
    case ContactValueKind::Event:
        break;
    }
    return tr("Dates");
}

QString ContactTypes::errorText(const core::Error &error)
{
    using core::ErrorCode;
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
    case ErrorCode::Timeout:
        return tr("The phone is not connected. Connect it, then try again.");
    case ErrorCode::PermissionDenied:
    case ErrorCode::AccessDenied:
        return tr("The app on the phone may not access contacts. Allow it in the Android "
                  "settings.");
    case ErrorCode::NotFound:
        return tr("This contact is no longer on the phone. Sync to update the list.");
    case ErrorCode::LimitExceeded:
        return tr("The phone takes up to 20 entries of each kind and a small photo. Remove some "
                  "entries or choose another photo.");
    case ErrorCode::InvalidInput:
        return tr("Enter a name, a number, a mail address or a company.");
    case ErrorCode::IoFailure:
        return tr("The contacts cannot be saved on this computer. Check the storage folder in the "
                  "settings.");
    case ErrorCode::NotSupported:
        return tr("The phone does not support this. Update MyPhoneExplorer Client on the phone.");
    case ErrorCode::WrongPin:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::WifiDisabled:
    case ErrorCode::Rejected:
    case ErrorCode::ProtocolError:
        break;
    }
    return tr("The phone did not take the change. Sync, then try again.");
}

QString ContactTypes::summary(const core::codec::Contact &contact)
{
    QStringList lines;
    lines.append(core::codec::displayNameOf(contact));
    const auto add = [&lines](const QString &label, const QString &value) {
        if (!value.trimmed().isEmpty())
            lines.append(tr("%1: %2").arg(label, value.trimmed()));
    };
    add(tr("Nickname"), contact.nickname);
    add(tr("Company"),
        QStringList({contact.organization, contact.department}).join(u' ').trimmed());
    add(tr("Job title"), contact.title);
    for (const ContactValueKind kind : core::codec::allContactValueKinds) {
        for (const core::codec::ContactValue &entry : contact.values(kind)) {
            const QString value = kind == ContactValueKind::Address
                                      ? entry.address.join(u' ').simplified()
                                      : entry.value;
            add(label(kind, entry.type, entry.label), value);
        }
    }
    if (contact.birthday.isValid())
        add(tr("Birthday"), QLocale().toString(contact.birthday, QLocale::ShortFormat));
    if (contact.anniversary.isValid())
        add(tr("Anniversary"), QLocale().toString(contact.anniversary, QLocale::ShortFormat));
    add(tr("Groups"), contact.groups.join(QLatin1StringView(", ")));
    add(tr("Note"), contact.note);
    lines.append(contact.photo.isEmpty() ? tr("No photo") : tr("With photo"));
    return lines.join(u'\n');
}

} // namespace qpm::ui
