#pragma once

#include "core/codec/CallLogCodec.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::ui {

/**
 * The call log of the current phone, newest first, narrowed by kind and by
 * the search of the area. Each entry shows the name of the stored contact
 * with the same number, else the name the phone keeps with the call, else the
 * number. Filled by CallsArea.
 */
class CallListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CallListModel)
    QML_UNCREATABLE("Provided by CallsArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Entries stored for the phone, before any filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    Q_PROPERTY(Kind kindFilter READ kindFilter WRITE setKindFilter NOTIFY kindFilterChanged FINAL)

public:
    /// The kind of a call; All is only a filter.
    enum class Kind { All, Incoming, Outgoing, Missed };
    Q_ENUM(Kind)

    enum Role {
        IdRole = Qt::UserRole + 1,
        NumberRole,
        /// The contact's name, else the name kept by the phone; may be empty.
        NameRole,
        /// The name, else the number, else a text for a hidden number.
        TitleRole,
        KindRole,
        /// "Incoming call", "Outgoing call" or "Missed call", translated.
        KindTextRole,
        StartRole,
        /// The time of day, for the row under its day.
        TimeTextRole,
        DurationRole,
        /// "1:05" or "1:02:03"; empty for a missed call.
        DurationTextRole,
        /// The day the entries are listed under: today, yesterday or a date.
        SectionRole,
        /// A stored contact has this number.
        InContactsRole,
    };

    explicit CallListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int totalCount() const { return int(m_all.size()); }
    [[nodiscard]] Kind kindFilter() const { return m_kindFilter; }
    void setKindFilter(Kind kind);

    /// Replaces every entry, which must come newest first; the filters stay.
    void setCalls(QList<core::codec::CallEntry> calls);
    /// Contact names by ContactStore::numberKey().
    void setContactNames(QHash<QString, QString> names);
    /// Shows only entries whose name or number holds `text`; digits also match spaced numbers.
    void setSearchText(const QString &text);

    /// The row of the entry `id`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(int id) const;
    /// The id in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int idAt(int row) const;
    /// The ids of every row shown.
    [[nodiscard]] QList<quint32> shownIds() const;
    /// The entries of every row shown, in the order of the rows.
    [[nodiscard]] QList<core::codec::CallEntry> shownEntries() const;
    /// The stored entry `id`, also when it is filtered out.
    [[nodiscard]] std::optional<core::codec::CallEntry> entry(quint32 id) const;
    /// The contact's name for `number`; empty when no stored contact has it.
    [[nodiscard]] QString contactNameFor(const QString &number) const;
    /// What the list shows as the title of `entry`.
    [[nodiscard]] QString titleOf(const core::codec::CallEntry &entry) const;

    [[nodiscard]] static Kind kindOf(core::codec::CallDirection direction);
    [[nodiscard]] static QString kindText(core::codec::CallDirection direction);
    /// "0:42", "12:05" or "1:02:03".
    [[nodiscard]] static QString durationText(qint64 seconds);
    /// "Today", "Yesterday" or the date in the long form of the current locale.
    [[nodiscard]] static QString dayText(const QDate &day);

Q_SIGNALS:
    void countChanged();
    void kindFilterChanged();

private:
    void refilter();
    [[nodiscard]] bool matches(const core::codec::CallEntry &entry) const;

    QList<core::codec::CallEntry> m_all;
    QList<qsizetype> m_rows;
    QHash<QString, QString> m_contactNames;
    QString m_searchText;
    QString m_searchDigits;
    Kind m_kindFilter = Kind::All;
};

} // namespace qpm::ui
