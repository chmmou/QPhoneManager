#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/// The kinds of entries a contact can hold several of.
enum class ContactValueKind { Phone, Email, Address, Website, Im, Event };

/// All kinds, in the order the phone writes them.
inline constexpr ContactValueKind allContactValueKinds[] = {
    ContactValueKind::Phone,   ContactValueKind::Email,   ContactValueKind::Im,
    ContactValueKind::Website, ContactValueKind::Address, ContactValueKind::Event,
};

/// Android type codes the phone uses, per kind. Other values may occur.
namespace phoneType {
inline constexpr int custom = 0;
inline constexpr int home = 1;
inline constexpr int mobile = 2;
inline constexpr int work = 3;
inline constexpr int faxWork = 4;
inline constexpr int faxHome = 5;
inline constexpr int pager = 6;
inline constexpr int other = 7;
inline constexpr int callback = 8;
inline constexpr int car = 9;
inline constexpr int companyMain = 10;
inline constexpr int isdn = 11;
inline constexpr int main = 12;
inline constexpr int otherFax = 13;
inline constexpr int radio = 14;
inline constexpr int telex = 15;
inline constexpr int tty = 16;
inline constexpr int workMobile = 17;
inline constexpr int workPager = 18;
inline constexpr int assistant = 19;
inline constexpr int mms = 20;
} // namespace phoneType

namespace emailType {
inline constexpr int custom = 0;
inline constexpr int home = 1;
inline constexpr int work = 2;
inline constexpr int other = 3;
inline constexpr int mobile = 4;
} // namespace emailType

namespace addressType {
inline constexpr int custom = 0;
inline constexpr int home = 1;
inline constexpr int work = 2;
inline constexpr int other = 3;
} // namespace addressType

namespace websiteType {
inline constexpr int custom = 0;
inline constexpr int homepage = 1;
inline constexpr int blog = 2;
inline constexpr int profile = 3;
inline constexpr int home = 4;
inline constexpr int work = 5;
inline constexpr int ftp = 6;
inline constexpr int other = 7;
} // namespace websiteType

/// For instant messaging the type is the protocol.
namespace imProtocol {
inline constexpr int custom = -1;
inline constexpr int aim = 0;
inline constexpr int msn = 1;
inline constexpr int yahoo = 2;
inline constexpr int skype = 3;
inline constexpr int qq = 4;
inline constexpr int googleTalk = 5;
inline constexpr int icq = 6;
inline constexpr int jabber = 7;
inline constexpr int netMeeting = 8;
} // namespace imProtocol

namespace eventType {
inline constexpr int custom = 0;
inline constexpr int anniversary = 1;
inline constexpr int other = 2;
inline constexpr int birthday = 3;
} // namespace eventType

/// Index of each part in ContactValue::address.
namespace addressPart {
inline constexpr int poBox = 0;
inline constexpr int extended = 1;
inline constexpr int street = 2;
inline constexpr int city = 3;
inline constexpr int region = 4;
inline constexpr int postcode = 5;
inline constexpr int country = 6;
inline constexpr int count = 7;
} // namespace addressPart

/// One phone number, mail address, postal address, website, messenger or date.
struct ContactValue
{
    /// The phone's key of this entry; 0 for an entry the phone does not know yet.
    quint64 dataId = 0;
    /// Android type code of the kind, for messengers the protocol.
    int type = 0;
    /// Own name of the type, used with the custom type.
    QString label;
    /// Number, address, link or messenger handle; for dates "yyyy-MM-dd".
    /// Empty for postal addresses, which use `address`.
    QString value;
    /// Postal addresses only: the seven parts, see addressPart.
    QStringList address;
    bool primary = false;

    bool operator==(const ContactValue &other) const = default;

    /// True when the entry carries no text.
    [[nodiscard]] bool isEmpty() const;
};

/**
 * A contact as the phone keeps it. Entries the phone does not know yet carry
 * the data id 0; a new contact carries the LUID 0.
 */
struct Contact
{
    /// The phone's raw contact id; 0 for a new contact.
    quint32 luid = 0;
    /// Both empty for the default account of the phone.
    QString accountType;
    QString accountName;

    QString familyName;
    QString givenName;
    QString middleName;
    QString prefix;
    QString suffix;
    QString displayName;
    QString nickname;
    quint64 nicknameId = 0;
    QString organization;
    QString department;
    QString title;
    QString note;

    QList<ContactValue> phones;
    QList<ContactValue> emails;
    QList<ContactValue> addresses;
    QList<ContactValue> websites;
    QList<ContactValue> ims;
    /// Dates other than birthday and anniversary.
    QList<ContactValue> events;

    QDate birthday;
    QDate anniversary;
    QStringList groups;
    /// "<uri>;<title>" as the phone sends it; empty for the default ringtone.
    QString ringtone;
    /// JPEG or PNG; empty without a photo.
    QByteArray photo;

    bool operator==(const Contact &other) const = default;

    [[nodiscard]] const QList<ContactValue> &values(ContactValueKind kind) const;
    [[nodiscard]] QList<ContactValue> &values(ContactValueKind kind);
};

/// Most entries of one kind the phone takes in one write.
inline constexpr qsizetype maxValuesPerWrite = 20;
/// Largest photo sent to the phone, in bytes.
inline constexpr qsizetype maxPhotoBytesToSend = 200 * 1024;
/// Most entries of one kind read from any source.
inline constexpr qsizetype maxValuesPerKind = 500;
/// Largest photo read from any source, in bytes.
inline constexpr qsizetype maxPhotoBytes = 16 * 1024 * 1024;

/**
 * The name to show for a contact: the display name, else the name parts,
 * else the company, the first number or the first mail address. Empty when
 * the contact has none of these.
 */
[[nodiscard]] QString displayNameOf(const Contact &contact);

/// Lower-case key that sorts contacts by their shown name.
[[nodiscard]] QString sortKeyOf(const Contact &contact);

/**
 * True when the two contacts hold the same entries. Photos count as equal
 * when both have one or both have none, because the phone may send the same
 * photo in different sizes.
 */
[[nodiscard]] bool sameContent(const Contact &a, const Contact &b);

/**
 * True when the contact can be saved: it has a name, or a number, a mail
 * address or a company.
 */
[[nodiscard]] bool isSavable(const Contact &contact);

/**
 * Reads one vCard in the form the phone client sends: no VERSION line, no
 * folding, CRLF line ends, X-SYNCMLREF keys, quoted-printable only for control
 * characters and "=". Unknown lines are skipped. Fails with
 * ErrorCode::InvalidInput without BEGIN:VCARD and END:VCARD, and with
 * ErrorCode::LimitExceeded for more than maxValuesPerKind entries of a kind or
 * a photo above maxPhotoBytes.
 */
[[nodiscard]] Result<Contact> parsePhoneVCard(const QByteArray &data);

/**
 * Writes a new contact for "telecom/pb/luid/.vcf": every entry without data
 * id, the account as X-ACCOUNT when one is set. Fails with
 * ErrorCode::InvalidInput when the contact is not savable and with
 * ErrorCode::LimitExceeded for more than maxValuesPerWrite entries of a kind
 * or a photo above maxPhotoBytesToSend.
 */
[[nodiscard]] Result<QByteArray> encodeNewContact(const Contact &contact);

/**
 * Writes the change from `phone`, the contact as the phone has it now, to
 * `edited` for "telecom/pb/luid/<LUID>.vcf". Name lines are always sent;
 * other entries only when they changed. An entry left out or emptied in
 * `edited` is sent with its data id and an empty value, which deletes it on
 * the phone. Entries of `edited` whose data id the phone does not know are
 * sent as new ones. Fails like encodeNewContact().
 */
[[nodiscard]] Result<QByteArray> encodeContactUpdate(const Contact &phone, const Contact &edited);

/**
 * Writes the contact the way the phone sends it, with data ids, X-IRMC-LUID
 * and a blank line after the photo; parsePhoneVCard() reads it back
 * unchanged. For backups, tests and a stand-in of the phone.
 */
[[nodiscard]] QByteArray encodeAsPhoneSendsIt(const Contact &contact);

} // namespace qpm::core::codec
