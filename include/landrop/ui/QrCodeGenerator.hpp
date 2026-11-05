#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if defined(LANDROP_BUILD_QT)
#include <QPixmap>
#include <QColor>
#endif

namespace landrop::ui {

enum class QrEccLevel : uint8_t {
    Low = 0,      // ~7% error correction
    Medium = 1,   // ~15% error correction (default)
    Quartile = 2, // ~25% error correction
    High = 3      // ~30% error correction
};

/**
 * High-performance, zero-dependency ISO/IEC 18004 QR Code Matrix Generator.
 */
class QrCodeGenerator {
public:
    static QrCodeGenerator encodeText(const std::string& text, QrEccLevel ecc = QrEccLevel::Medium);
    static QrCodeGenerator encodeBinary(const std::vector<uint8_t>& data, QrEccLevel ecc = QrEccLevel::Medium);

    QrCodeGenerator();
    QrCodeGenerator(int version, QrEccLevel ecc, const std::vector<uint8_t>& dataCodewords, int mask = -1);

    int getVersion() const { return m_version; }
    int getSize() const { return m_size; }
    QrEccLevel getEccLevel() const { return m_ecc; }
    int getMask() const { return m_mask; }

    /**
     * Returns true if module at (x, y) is black/dark, false if white/light.
     * Coordinate (0, 0) is top-left.
     */
    bool getModule(int x, int y) const;

    // Export helpers
    std::string toSvg(int border = 4, const std::string& fgHex = "#000000", const std::string& bgHex = "#ffffff") const;
    std::string toAscii(bool invert = false) const;

#if defined(LANDROP_BUILD_QT)
    QPixmap toQPixmap(int targetPixelSize = 256, int border = 4,
                      const QColor& fg = Qt::black, const QColor& bg = Qt::white) const;
#endif

private:
    int m_version{1};
    int m_size{21};
    QrEccLevel m_ecc{QrEccLevel::Medium};
    int m_mask{0};
    std::vector<std::vector<bool>> m_modules; // [y][x], true = dark
};

} // namespace landrop::ui
