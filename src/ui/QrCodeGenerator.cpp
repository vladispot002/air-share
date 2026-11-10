#include "landrop/ui/QrCodeGenerator.hpp"
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <array>
#include <cassert>

#if defined(LANDROP_BUILD_QT)
#include <QPainter>
#include <QImage>
#endif

namespace landrop::ui {

namespace {

// Galois Field GF(256) tables with primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11D)
struct GaloisTables {
    std::array<uint8_t, 256> exp{};
    std::array<uint8_t, 256> log{};

    constexpr GaloisTables() {
        uint8_t val = 1;
        for (size_t i = 0; i < 255; i++) {
            exp[i] = val;
            log[val] = static_cast<uint8_t>(i);
            int next = (val << 1);
            if (next & 0x100) next ^= 0x11D;
            val = static_cast<uint8_t>(next);
        }
        exp[255] = exp[0];
        log[0] = 0;
    }
};

static constexpr GaloisTables GF;

inline uint8_t gfMul(uint8_t x, uint8_t y) {
    if (x == 0 || y == 0) return 0;
    return GF.exp[(static_cast<int>(GF.log[x]) + GF.log[y]) % 255];
}

// Computes Reed-Solomon generator polynomial of degree degree
std::vector<uint8_t> rsComputeGenerator(int degree) {
    std::vector<uint8_t> g(degree + 1, 0);
    g[degree] = 1;

    for (int i = 0; i < degree; i++) {
        uint8_t factor = GF.exp[i];
        for (size_t j = 0; j < g.size() - 1; j++) {
            g[j] = gfMul(g[j], factor) ^ g[j + 1];
        }
        g.back() = gfMul(g.back(), factor);
    }
    return g;
}

// Computes Reed-Solomon error correction codewords
std::vector<uint8_t> rsComputeRemainder(const std::vector<uint8_t>& data, int degree) {
    auto gen = rsComputeGenerator(degree);
    std::vector<uint8_t> rem(degree, 0);

    for (uint8_t b : data) {
        uint8_t factor = b ^ rem[0];
        for (int i = 0; i < degree - 1; i++) {
            rem[i] = rem[i + 1] ^ gfMul(gen[i], factor);
        }
        rem[degree - 1] = gfMul(gen[degree - 1], factor);
    }
    return rem;
}

// ISO 18004 Table 9: Number of data codewords per version and ECC level (Versions 1..10)
// Format: { {L, M, Q, H}, totalCodewords, ecCodewordsPerBlock, numBlocksGroup1, dataCodewordsGroup1, numBlocksGroup2, dataCodewordsGroup2 }
struct VersionInfo {
    int totalCodewords;
    int ecCodewordsPerBlock;
    int numBlocksGroup1;
    int dataCodewordsGroup1;
    int numBlocksGroup2;
    int dataCodewordsGroup2;
};

// Index 0 = Version 1, Index 9 = Version 10
// Table for ECC Level Medium (standard default)
static const VersionInfo VERSION_INFO_M[10] = {
    { 26, 10, 1, 16, 0, 0 },    // V1
    { 44, 16, 1, 28, 0, 0 },    // V2
    { 70, 26, 1, 44, 0, 0 },    // V3
    { 100, 18, 2, 32, 0, 0 },   // V4
    { 134, 24, 2, 43, 0, 0 },   // V5
    { 172, 16, 4, 27, 0, 0 },   // V6
    { 196, 18, 4, 31, 0, 0 },   // V7
    { 242, 22, 2, 38, 2, 39 },  // V8
    { 292, 22, 3, 36, 2, 37 },  // V9
    { 346, 26, 4, 43, 1, 44 }   // V10
};

// Alignment pattern locations (ISO 18004 Annex E)
static const std::vector<int> ALIGN_POS[11] = {
    {},                     // V0
    {},                     // V1
    {6, 18},                // V2
    {6, 22},                // V3
    {6, 26},                // V4
    {6, 30},                // V5
    {6, 34},                // V6
    {6, 22, 38},            // V7
    {6, 24, 42},            // V8
    {6, 26, 46},            // V9
    {6, 28, 50}             // V10
};

// ISO/IEC 18004 Table 13: 18-bit Version Information (BCH(18, 6)) for Versions 7..10
static const uint32_t VERSION_INFO_BITS[11] = {
    0, 0, 0, 0, 0, 0, 0,
    0x07C94, // Version 7
    0x085BC, // Version 8
    0x09A99, // Version 9
    0x0A4D3  // Version 10
};

// Bit buffer helper
class BitBuffer {
public:
    std::vector<bool> bits;

    void appendBits(uint32_t val, int len) {
        for (int i = len - 1; i >= 0; i--) {
            bits.push_back(((val >> i) & 1) != 0);
        }
    }

    std::vector<uint8_t> toBytes() const {
        std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
        for (size_t i = 0; i < bits.size(); i++) {
            if (bits[i]) {
                bytes[i / 8] |= static_cast<uint8_t>(1 << (7 - (i % 8)));
            }
        }
        return bytes;
    }
};

} // anonymous namespace

QrCodeGenerator::QrCodeGenerator()
    : m_version(1), m_size(21), m_ecc(QrEccLevel::Medium), m_mask(0),
      m_modules(21, std::vector<bool>(21, false)) {}

bool QrCodeGenerator::getModule(int x, int y) const {
    if (x >= 0 && x < m_size && y >= 0 && y < m_size) {
        return m_modules[static_cast<size_t>(y)][static_cast<size_t>(x)];
    }
    return false;
}

QrCodeGenerator QrCodeGenerator::encodeText(const std::string& text, QrEccLevel ecc) {
    std::vector<uint8_t> data(text.begin(), text.end());
    return encodeBinary(data, ecc);
}

QrCodeGenerator QrCodeGenerator::encodeBinary(const std::vector<uint8_t>& data, QrEccLevel ecc) {
    (void)ecc; // Currently optimized for standard Medium level across V1-V10

    // Find smallest version (1..10) that fits 8-bit byte mode payload
    int version = 1;
    for (; version <= 10; version++) {
        const auto& vi = VERSION_INFO_M[version - 1];
        int totalData = vi.numBlocksGroup1 * vi.dataCodewordsGroup1 + vi.numBlocksGroup2 * vi.dataCodewordsGroup2;
        int charCountBits = (version <= 9) ? 8 : 16;
        int neededBits = 4 + charCountBits + static_cast<int>(data.size()) * 8;
        if ((neededBits + 7) / 8 <= totalData) {
            break;
        }
    }

    if (version > 10) {
        throw std::length_error("Data exceeds maximum QR capacity for Versions 1-10 (max 213 bytes for Medium ECC)");
    }

    const auto& vi = VERSION_INFO_M[version - 1];
    int totalDataCodewords = vi.numBlocksGroup1 * vi.dataCodewordsGroup1 + vi.numBlocksGroup2 * vi.dataCodewordsGroup2;

    BitBuffer bb;
    // 1. Mode indicator (0100 for 8-bit Byte mode)
    bb.appendBits(0x04, 4);

    // 2. Character count indicator
    int charCountBits = (version <= 9) ? 8 : 16;
    bb.appendBits(static_cast<uint32_t>(data.size()), charCountBits);

    // 3. Payload bits
    for (uint8_t b : data) {
        bb.appendBits(b, 8);
    }

    // 4. Terminator (up to 4 zero bits)
    int remainingBits = totalDataCodewords * 8 - static_cast<int>(bb.bits.size());
    int termCount = std::min(4, std::max(0, remainingBits));
    bb.appendBits(0, termCount);

    // 5. Pad to byte boundary
    if (bb.bits.size() % 8 != 0) {
        bb.appendBits(0, 8 - (bb.bits.size() % 8));
    }

    // 6. Pad codewords (0xEC, 0x11 alternating)
    std::vector<uint8_t> dataCodewords = bb.toBytes();
    static const uint8_t PAD_BYTES[2] = { 0xEC, 0x11 };
    size_t padIdx = 0;
    while (dataCodewords.size() < static_cast<size_t>(totalDataCodewords)) {
        dataCodewords.push_back(PAD_BYTES[padIdx]);
        padIdx ^= 1;
    }

    return QrCodeGenerator(version, QrEccLevel::Medium, dataCodewords);
}

QrCodeGenerator::QrCodeGenerator(int version, QrEccLevel ecc, const std::vector<uint8_t>& dataCodewords, int mask)
    : m_version(version), m_size(version * 4 + 17), m_ecc(ecc), m_mask(mask) {
    m_modules.assign(m_size, std::vector<bool>(m_size, false));
    std::vector<std::vector<bool>> isFunction(m_size, std::vector<bool>(m_size, false));

    auto setFunction = [&](int x, int y, bool val) {
        if (x >= 0 && x < m_size && y >= 0 && y < m_size) {
            m_modules[y][x] = val;
            isFunction[y][x] = true;
        }
    };

    // Draw finder pattern (7x7 box)
    auto drawFinder = [&](int ox, int oy) {
        for (int y = -1; y <= 7; y++) {
            for (int x = -1; x <= 7; x++) {
                int px = ox + x;
                int py = oy + y;
                if (px < 0 || px >= m_size || py < 0 || py >= m_size) continue;
                bool isBorder = (x == 0 || x == 6 || y == 0 || y == 6);
                bool isCenter = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
                bool dark = (x >= 0 && x <= 6 && y >= 0 && y <= 6) && (isBorder || isCenter);
                setFunction(px, py, dark);
            }
        }
    };

    drawFinder(0, 0);
    drawFinder(m_size - 7, 0);
    drawFinder(0, m_size - 7);

    // Timing patterns (ISO/IEC 18004:2015 Section 6.3.5: strictly between separators from 8 to size - 9)
    for (int i = 8; i <= m_size - 9; i++) {
        setFunction(i, 6, (i % 2 == 0));
        setFunction(6, i, (i % 2 == 0));
    }

    // Alignment patterns
    const auto& align = ALIGN_POS[m_version];
    for (size_t i = 0; i < align.size(); i++) {
        for (size_t j = 0; j < align.size(); j++) {
            int cx = align[i];
            int cy = align[j];
            // Exclude finders
            if ((cx <= 8 && cy <= 8) || (cx >= m_size - 9 && cy <= 8) || (cx <= 8 && cy >= m_size - 9)) {
                continue;
            }
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    bool dark = (std::max(std::abs(dx), std::abs(dy)) != 1);
                    setFunction(cx + dx, cy + dy, dark);
                }
            }
        }
    }

    // Reserve format bits (skipping row 6 and col 6 which belong to timing patterns)
    for (int i = 0; i <= 8; i++) {
        if (i != 6) {
            setFunction(8, i, false);
            setFunction(i, 8, false);
        }
    }
    // Column 8 format bits reservation: rows (m_size - 7) through (m_size - 1)
    // Note: row (m_size - 8) is the Dark Module, NOT format information
    for (int i = m_size - 7; i < m_size; i++) {
        setFunction(8, i, false);
    }
    // Row 8 format bits reservation: columns (m_size - 8) through (m_size - 1)
    for (int i = m_size - 8; i < m_size; i++) {
        setFunction(i, 8, false);
    }

    // Dark module (ISO/IEC 18004:2015 Section 6.3.6: module at (8, size - 8) is ALWAYS dark)
    setFunction(8, 4 * m_version + 9, true);

    // Version Information for Versions 7..10 (ISO/IEC 18004:2015 Section 6.5.2)
    if (m_version >= 7) {
        uint32_t vBits = VERSION_INFO_BITS[m_version];
        for (int k = 0; k < 18; k++) {
            bool bit = ((vBits >> k) & 1) != 0;
            // Bottom-left block: 6 cols x 3 rows at (0..5, size - 11 .. size - 9)
            setFunction(k / 3, m_size - 11 + (k % 3), bit);
            // Top-right block: 3 cols x 6 rows at (size - 11 .. size - 9, 0..5)
            setFunction(m_size - 11 + (k % 3), k / 3, bit);
        }
    }

    // Interleave data and error correction blocks
    const auto& vi = VERSION_INFO_M[m_version - 1];
    int numBlocks = vi.numBlocksGroup1 + vi.numBlocksGroup2;
    std::vector<std::vector<uint8_t>> dataBlocks(numBlocks);
    std::vector<std::vector<uint8_t>> ecBlocks(numBlocks);

    size_t offset = 0;
    for (int b = 0; b < numBlocks; b++) {
        int dcCount = (b < vi.numBlocksGroup1) ? vi.dataCodewordsGroup1 : vi.dataCodewordsGroup2;
        dataBlocks[b].assign(dataCodewords.begin() + offset, dataCodewords.begin() + offset + dcCount);
        offset += dcCount;
        ecBlocks[b] = rsComputeRemainder(dataBlocks[b], vi.ecCodewordsPerBlock);
    }

    std::vector<uint8_t> finalSequence;
    finalSequence.reserve(vi.totalCodewords);

    // Interleave data codewords
    int maxDataCodewords = std::max(vi.dataCodewordsGroup1, vi.dataCodewordsGroup2);
    for (int i = 0; i < maxDataCodewords; i++) {
        for (int b = 0; b < numBlocks; b++) {
            if (i < static_cast<int>(dataBlocks[b].size())) {
                finalSequence.push_back(dataBlocks[b][i]);
            }
        }
    }

    // Interleave EC codewords
    for (int i = 0; i < vi.ecCodewordsPerBlock; i++) {
        for (int b = 0; b < numBlocks; b++) {
            finalSequence.push_back(ecBlocks[b][i]);
        }
    }

    // Matrix placement (zig-zag right-to-left 2-column bands)
    BitBuffer finalBits;
    for (uint8_t b : finalSequence) {
        finalBits.appendBits(b, 8);
    }
    // Remaining remainder bits
    static const int REMAINDER_BITS[11] = { 0, 0, 7, 7, 7, 7, 7, 0, 0, 0, 0 };
    finalBits.appendBits(0, REMAINDER_BITS[m_version]);

    size_t bitIdx = 0;
    for (int right = m_size - 1; right > 0; right -= 2) {
        if (right == 6) right--; // Skip vertical timing pattern column

        for (int vert = 0; vert < m_size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upwards = ((right + 1) & 2) == 0;
                int y = upwards ? (m_size - 1 - vert) : vert;

                if (!isFunction[y][x]) {
                    bool val = false;
                    if (bitIdx < finalBits.bits.size()) {
                        val = finalBits.bits[bitIdx++];
                    }
                    m_modules[y][x] = val;
                }
            }
        }
    }

    // Format info mask evaluation: standard mask 0 is clean default
    int bestMask = (mask >= 0 && mask <= 7) ? mask : 0;
    m_mask = bestMask;

    // Apply mask to non-function modules
    for (int y = 0; y < m_size; y++) {
        for (int x = 0; x < m_size; x++) {
            if (isFunction[y][x]) continue;
            bool invert = false;
            switch (bestMask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (y / 2 + x / 3) % 2 == 0; break;
                case 5: invert = ((x * y) % 2 + (x * y) % 3) == 0; break;
                case 6: invert = (((x * y) % 2 + (x * y) % 3) % 2) == 0; break;
                case 7: invert = (((x + y) % 2 + (x * y) % 3) % 2) == 0; break;
            }
            if (invert) {
                m_modules[y][x] = !m_modules[y][x];
            }
        }
    }

    // Write format information (Medium ECC + mask)
    // Precomputed BCH(15, 5) masked with 0x5412 for ECC=Medium (bits 00)
    static const uint16_t FORMAT_INFO_M[8] = {
        0x5412, 0x5125, 0x5E7C, 0x5B4B, 0x45F9, 0x40CE, 0x4F97, 0x4AA0
    };
    uint16_t formatBits = FORMAT_INFO_M[bestMask];

    // Top-left format bits
    for (int i = 0; i <= 5; i++) setFunction(8, i, ((formatBits >> (14 - i)) & 1) != 0);
    setFunction(8, 7, ((formatBits >> 8) & 1) != 0);
    setFunction(8, 8, ((formatBits >> 7) & 1) != 0);
    setFunction(7, 8, ((formatBits >> 6) & 1) != 0);
    for (int i = 9; i <= 14; i++) setFunction(14 - i, 8, ((formatBits >> (14 - i)) & 1) != 0);

    // Split format bits (bottom-left and top-right)
    for (int i = 0; i <= 7; i++) setFunction(m_size - 1 - i, 8, ((formatBits >> i) & 1) != 0);
    for (int i = 8; i <= 14; i++) setFunction(8, m_size - 15 + i, ((formatBits >> i) & 1) != 0);
}

std::string QrCodeGenerator::toSvg(int border, const std::string& fgHex, const std::string& bgHex) const {
    if (border < 0) border = 0;
    int totalSize = m_size + 2 * border;

    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
       << "viewBox=\"0 0 " << totalSize << " " << totalSize << "\" stroke=\"none\">\n"
       << "<rect width=\"100%\" height=\"100%\" fill=\"" << bgHex << "\"/>\n"
       << "<path fill=\"" << fgHex << "\" d=\"";

    for (int y = 0; y < m_size; y++) {
        for (int x = 0; x < m_size; x++) {
            if (getModule(x, y)) {
                ss << "M" << (x + border) << "," << (y + border) << "h1v1h-1z ";
            }
        }
    }
    ss << "\"/>\n</svg>\n";
    return ss.str();
}

std::string QrCodeGenerator::toAscii(bool invert) const {
    std::ostringstream ss;
    // Uses unicode half-block characters
    for (int y = 0; y < m_size; y += 2) {
        for (int x = 0; x < m_size; x++) {
            bool top = getModule(x, y);
            bool bottom = (y + 1 < m_size) ? getModule(x, y + 1) : false;
            if (invert) {
                top = !top;
                bottom = !bottom;
            }
            if (top && bottom) ss << "█";
            else if (top && !bottom) ss << "▀";
            else if (!top && bottom) ss << "▄";
            else ss << " ";
        }
        ss << "\n";
    }
    return ss.str();
}

} // namespace landrop::ui
