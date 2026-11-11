#pragma once

#include "core/codec/PbMetadata.h"
#include "core/codec/PhoneVCard.h"
#include "core/service/ContactsService.h"
#include "ui/contacts/ContactValueModel.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace qpm::core::store {
class ContactStore;
}

namespace qpm::ui {

class PhotoSource;

/**
 * The contact form: one contact at a time, new or loaded from the store,
 * with its fields as properties and its entries as models. save() sends only
 * what changed to the phone. When the contact changed on the phone since it
 * was loaded, the save stops, hasConflict turns true and both versions are
 * shown; keepOwnVersion() or takePhoneVersion() decides.
 *
 * Owned by ContactsArea, which hands in the store and the connection state.
 * Saving needs a connection; everything else works without one.
 */
class ContactEditor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ContactEditor)
    QML_UNCREATABLE("Provided by ContactsArea")

    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded FINAL)
    Q_PROPERTY(int luid READ luid NOTIFY loaded FINAL)
    Q_PROPERTY(QString givenName READ givenName WRITE setGivenName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString middleName READ middleName WRITE setMiddleName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString familyName READ familyName WRITE setFamilyName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString prefix READ prefix WRITE setPrefix NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString suffix READ suffix WRITE setSuffix NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString nickname READ nickname WRITE setNickname NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(
        QString organization READ organization WRITE setOrganization NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString department READ department WRITE setDepartment NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString jobTitle READ jobTitle WRITE setJobTitle NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString note READ note WRITE setNote NOTIFY fieldsChanged FINAL)
    /// Dates as typed; see dateExample for the form.
    Q_PROPERTY(QString birthday READ birthday WRITE setBirthday NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString anniversary READ anniversary WRITE setAnniversary NOTIFY fieldsChanged FINAL)
    /// False while a date field holds text that is no date.
    Q_PROPERTY(bool datesValid READ datesValid NOTIFY fieldsChanged FINAL)
    /// A date written the way the date fields take it.
    Q_PROPERTY(QString dateExample READ dateExample CONSTANT FINAL)
    /// Accounts a new contact can go to, the phone's default first.
    Q_PROPERTY(QStringList accountNames READ accountNames NOTIFY accountsChanged FINAL)
    Q_PROPERTY(int accountIndex READ accountIndex WRITE setAccountIndex NOTIFY fieldsChanged FINAL)
    /// The account of a loaded contact, which cannot change.
    Q_PROPERTY(QString accountText READ accountText NOTIFY loaded FINAL)
    /// Groups of the contact's account and the groups it is in.
    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY accountsChanged FINAL)
    Q_PROPERTY(QStringList selectedGroups READ selectedGroups NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString photoUrl READ photoUrl NOTIFY photoChanged FINAL)
    Q_PROPERTY(bool hasPhoto READ hasPhoto NOTIFY photoChanged FINAL)
    /// The picture being cropped; empty when none is chosen.
    Q_PROPERTY(QString candidateUrl READ candidateUrl NOTIFY candidateChanged FINAL)
    Q_PROPERTY(int candidateWidth READ candidateWidth NOTIFY candidateChanged FINAL)
    Q_PROPERTY(int candidateHeight READ candidateHeight NOTIFY candidateChanged FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *phones READ phones CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *emails READ emails CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *addresses READ addresses CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *websites READ websites CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *ims READ ims CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactValueModel *events READ events CONSTANT FINAL)
    /// Something differs from the loaded contact.
    Q_PROPERTY(bool modified READ isModified NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Connected, not busy and the form holds something to save.
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged FINAL)
    Q_PROPERTY(bool hasConflict READ hasConflict NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString ownVersionText READ ownVersionText NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString phoneVersionText READ phoneVersionText NOTIFY conflictChanged FINAL)

public:
    /// Largest photo written, so that the contact fits into one packet to the phone.
    static constexpr qsizetype maxPhotoBytesForWrite = 40 * 1024;

    ContactEditor(core::service::ContactsService *service, std::shared_ptr<PhotoSource> photos,
                  QObject *parent = nullptr);

    void setStore(const std::shared_ptr<core::store::ContactStore> &store);
    void setConnected(bool connected);

    [[nodiscard]] bool isNew() const { return m_original.luid == 0; }
    [[nodiscard]] int luid() const { return int(m_original.luid); }
    [[nodiscard]] QString givenName() const { return m_contact.givenName; }
    void setGivenName(const QString &value);
    [[nodiscard]] QString middleName() const { return m_contact.middleName; }
    void setMiddleName(const QString &value);
    [[nodiscard]] QString familyName() const { return m_contact.familyName; }
    void setFamilyName(const QString &value);
    [[nodiscard]] QString prefix() const { return m_contact.prefix; }
    void setPrefix(const QString &value);
    [[nodiscard]] QString suffix() const { return m_contact.suffix; }
    void setSuffix(const QString &value);
    [[nodiscard]] QString nickname() const { return m_contact.nickname; }
    void setNickname(const QString &value);
    [[nodiscard]] QString organization() const { return m_contact.organization; }
    void setOrganization(const QString &value);
    [[nodiscard]] QString department() const { return m_contact.department; }
    void setDepartment(const QString &value);
    [[nodiscard]] QString jobTitle() const { return m_contact.title; }
    void setJobTitle(const QString &value);
    [[nodiscard]] QString note() const { return m_contact.note; }
    void setNote(const QString &value);
    [[nodiscard]] QString birthday() const { return m_birthday; }
    void setBirthday(const QString &value);
    [[nodiscard]] QString anniversary() const { return m_anniversary; }
    void setAnniversary(const QString &value);
    [[nodiscard]] bool datesValid() const;
    [[nodiscard]] QString dateExample() const;
    [[nodiscard]] QStringList accountNames() const;
    [[nodiscard]] int accountIndex() const { return m_accountIndex; }
    void setAccountIndex(int index);
    [[nodiscard]] QString accountText() const;
    [[nodiscard]] QStringList groupNames() const;
    [[nodiscard]] QStringList selectedGroups() const { return m_contact.groups; }
    [[nodiscard]] QString photoUrl() const { return m_photoUrl; }
    [[nodiscard]] bool hasPhoto() const { return !m_contact.photo.isEmpty(); }
    [[nodiscard]] QString candidateUrl() const { return m_candidateUrl; }
    [[nodiscard]] int candidateWidth() const { return m_candidateSize.width(); }
    [[nodiscard]] int candidateHeight() const { return m_candidateSize.height(); }
    [[nodiscard]] ContactValueModel *phones() const { return m_phones; }
    [[nodiscard]] ContactValueModel *emails() const { return m_emails; }
    [[nodiscard]] ContactValueModel *addresses() const { return m_addresses; }
    [[nodiscard]] ContactValueModel *websites() const { return m_websites; }
    [[nodiscard]] ContactValueModel *ims() const { return m_ims; }
    [[nodiscard]] ContactValueModel *events() const { return m_events; }
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool hasConflict() const { return m_conflict.has_value(); }
    [[nodiscard]] QString ownVersionText() const;
    [[nodiscard]] QString phoneVersionText() const;

    /// Empties the form for a new contact in the phone's default account.
    Q_INVOKABLE void newContact();
    /**
     * Like newContact(), with `number` as a mobile number. A `name` of two or
     * more words is split at the last space into given names and family name;
     * a single word becomes the given name.
     */
    void newContactWith(const QString &name, const QString &number);
    /// Fills the form with the stored contact `luid`; false and an error when it is unknown.
    Q_INVOKABLE bool load(int luid);
    /// Starts saving; false and an error when the form cannot be saved now.
    Q_INVOKABLE bool save();
    /// After a conflict: writes this form over the phone's version.
    Q_INVOKABLE bool keepOwnVersion();
    /// After a conflict: drops this form and keeps the phone's version.
    Q_INVOKABLE bool takePhoneVersion();
    Q_INVOKABLE void setGroupSelected(const QString &group, bool selected);
    /// Adds a new group for this contact; the phone creates it on saving.
    Q_INVOKABLE bool addGroup(const QString &group);
    /**
     * Reads the picture at the local file `url` for cropping. Fails with an
     * error for anything that is no readable picture or is too large.
     */
    Q_INVOKABLE bool loadPhotoCandidate(const QUrl &url);
    /**
     * Cuts the square of side `side` at `x`, `y` in candidate pixels out of
     * the candidate and makes it the photo, scaled down and compressed until
     * it fits maxPhotoBytesForWrite.
     */
    Q_INVOKABLE bool applyCrop(int x, int y, int side);
    Q_INVOKABLE void clearPhoto();
    /// Forgets the picture being cropped.
    Q_INVOKABLE void dropCandidate();

    /// The contact as the form holds it; for tests.
    [[nodiscard]] core::codec::Contact edited() const;

Q_SIGNALS:
    void loaded();
    void fieldsChanged();
    void accountsChanged();
    void photoChanged();
    void candidateChanged();
    void busyChanged();
    void canSaveChanged();
    void conflictChanged();
    /// The contact `luid` is on the phone and in the store as the form showed it.
    void saved(int luid);
    void errorOccurred(const QString &message);

private:
    void setText(QString &field, const QString &value);
    void show(const core::codec::Contact &contact);
    void setBusy(bool busy);
    void startSave(core::service::SaveMode mode);
    void reloadAccounts();
    [[nodiscard]] QDate parseDate(const QString &text, bool *ok) const;
    [[nodiscard]] QString formatDate(const QDate &date) const;
    [[nodiscard]] QString dateFormat() const;

    core::service::ContactsService *m_service = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    std::weak_ptr<core::store::ContactStore> m_store;
    core::codec::Contact m_original;
    core::codec::Contact m_contact;
    core::codec::PbMetadata m_metadata;
    QList<core::codec::PbAccount> m_accounts;
    std::optional<core::service::SaveOutcome> m_conflict;
    QString m_birthday;
    QString m_anniversary;
    QString m_photoUrl;
    QString m_candidateUrl;
    QSize m_candidateSize;
    QImage m_candidate;
    ContactValueModel *m_phones = nullptr;
    ContactValueModel *m_emails = nullptr;
    ContactValueModel *m_addresses = nullptr;
    ContactValueModel *m_websites = nullptr;
    ContactValueModel *m_ims = nullptr;
    ContactValueModel *m_events = nullptr;
    int m_accountIndex = 0;
    bool m_photoChanged = false;
    bool m_connected = false;
    bool m_busy = false;
};

} // namespace qpm::ui
