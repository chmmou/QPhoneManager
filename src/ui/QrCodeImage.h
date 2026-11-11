#pragma once

#include <QQuickPaintedItem>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace qpm::ui {

/**
 * Draws `text` as a QR code, dark modules on white with the quiet zone the
 * standard asks for, scaled to the item and centred. Draws nothing and sets
 * `valid` to false when the text is empty or too long for a QR code.
 */
class QrCodeImage : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(QrCodeImage)

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
    Q_PROPERTY(bool valid READ isValid NOTIFY textChanged FINAL)

public:
    explicit QrCodeImage(QQuickItem *parent = nullptr);

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text);
    [[nodiscard]] bool isValid() const { return m_size > 0; }

    void paint(QPainter *painter) override;

    /**
     * The modules of `text` row by row, true for dark, and the side length in
     * modules; side 0 when the text cannot be encoded.
     */
    [[nodiscard]] static std::vector<bool> encode(const QString &text, int &side);

Q_SIGNALS:
    void textChanged();

private:
    QString m_text;
    std::vector<bool> m_modules;
    int m_size = 0;
};

} // namespace qpm::ui
