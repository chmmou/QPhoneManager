#pragma once

#include "core/codec/PhoneVCard.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * One contact as the detail view shows it: name, photo and work in the
 * properties, and one row per entry, grouped by a translated heading. A row
 * names the action it offers: "call" and "message" for numbers, "mail" for
 * mail addresses, "link" for websites, or nothing. Filled by ContactsArea.
 */
class ContactDetailModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ContactDetailModel)
    QML_UNCREATABLE("Provided by ContactsArea")

    Q_PROPERTY(bool hasContact READ hasContact NOTIFY contactChanged FINAL)
    Q_PROPERTY(int luid READ luid NOTIFY contactChanged FINAL)
    Q_PROPERTY(QString displayName READ displayName NOTIFY contactChanged FINAL)
    /// Job title, company and department in one line; empty without them.
    Q_PROPERTY(QString work READ work NOTIFY contactChanged FINAL)
    Q_PROPERTY(QString photoUrl READ photoUrl NOTIFY contactChanged FINAL)
    Q_PROPERTY(QString initials READ initials NOTIFY contactChanged FINAL)
    Q_PROPERTY(QString note READ note NOTIFY contactChanged FINAL)
    /// The account title and name; empty for the phone's own account.
    Q_PROPERTY(QString account READ account NOTIFY contactChanged FINAL)
    Q_PROPERTY(QString groups READ groups NOTIFY contactChanged FINAL)

public:
    enum Role {
        SectionRole = Qt::UserRole + 1,
        LabelRole,
        ValueRole,
        ActionRole,
        ActionValueRole,
        /// True for the first row under its heading.
        FirstInSectionRole,
    };

    explicit ContactDetailModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool hasContact() const { return m_hasContact; }
    [[nodiscard]] int luid() const { return int(m_contact.luid); }
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString work() const;
    [[nodiscard]] QString photoUrl() const { return m_photoUrl; }
    [[nodiscard]] QString initials() const;
    [[nodiscard]] QString note() const { return m_contact.note; }
    [[nodiscard]] QString account() const { return m_account; }
    [[nodiscard]] QString groups() const;

    /// Shows `contact`; `accountTitle` is the account's human-readable kind.
    void setContact(const core::codec::Contact &contact, const QString &photoUrl,
                    const QString &accountTitle);
    void clear();

    /// The contact shown; empty without one.
    [[nodiscard]] const core::codec::Contact &contact() const { return m_contact; }

Q_SIGNALS:
    void contactChanged();

private:
    struct Row
    {
        QString section;
        QString label;
        QString value;
        QString action;
        QString actionValue;
    };

    QList<Row> m_rows;
    core::codec::Contact m_contact;
    QString m_photoUrl;
    QString m_account;
    bool m_hasContact = false;
};

} // namespace qpm::ui
