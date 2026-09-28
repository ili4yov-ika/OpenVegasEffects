#include "plugin/NativePlugin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QVector>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstring>
#include <array>
#include <cmath>
#include <utility>

namespace openvegas {
namespace plugin {

namespace {

// Minimal PE export-table reader. Only what is needed to answer "does this
// image export these names"; anything malformed simply yields no exports.
class PeImage
{
public:
    explicit PeImage(const QByteArray& bytes)
        : m_data(bytes)
    {
        parse();
    }

    bool exportsName(const char* name) const
    {
        return m_exports.contains(QString::fromLatin1(name));
    }

    bool valid() const { return m_valid; }

private:
    template <typename T>
    bool read(qint64 offset, T& out) const
    {
        if (offset < 0 || offset + static_cast<qint64>(sizeof(T)) > m_data.size()) {
            return false;
        }
        std::memcpy(&out, m_data.constData() + offset, sizeof(T));
        return true;
    }

    // Section table lookup: relative virtual address -> file offset.
    qint64 toFileOffset(quint32 rva) const
    {
        for (const auto& s : m_sections) {
            const quint32 size = qMax(s.virtualSize, s.rawSize);
            if (rva >= s.virtualAddress && rva < s.virtualAddress + size) {
                return static_cast<qint64>(s.rawPointer) + (rva - s.virtualAddress);
            }
        }
        return -1;
    }

    void parse()
    {
        quint16 mz = 0;
        if (!read(0, mz) || mz != 0x5A4D) { // "MZ"
            return;
        }
        quint32 peOffset = 0;
        if (!read(0x3C, peOffset)) {
            return;
        }
        quint32 peSig = 0;
        if (!read(peOffset, peSig) || peSig != 0x00004550) { // "PE\0\0"
            return;
        }
        quint16 sectionCount = 0;
        quint16 optionalHeaderSize = 0;
        if (!read(peOffset + 6, sectionCount) || !read(peOffset + 20, optionalHeaderSize)) {
            return;
        }
        quint16 magic = 0;
        if (!read(peOffset + 24, magic)) {
            return;
        }
        const bool pe32Plus = magic == 0x20B;
        const qint64 dataDirectory = peOffset + 24 + (pe32Plus ? 112 : 96);

        qint64 sectionOffset = peOffset + 24 + optionalHeaderSize;
        for (quint16 i = 0; i < sectionCount; ++i) {
            Section s;
            if (!read(sectionOffset + 8, s.virtualSize)
                || !read(sectionOffset + 12, s.virtualAddress)
                || !read(sectionOffset + 16, s.rawSize)
                || !read(sectionOffset + 20, s.rawPointer)) {
                return;
            }
            m_sections.append(s);
            sectionOffset += 40;
        }
        m_valid = true;

        quint32 exportRva = 0;
        if (!read(dataDirectory, exportRva) || exportRva == 0) {
            return;
        }
        const qint64 dir = toFileOffset(exportRva);
        if (dir < 0) {
            return;
        }
        quint32 nameCount = 0;
        quint32 namePointerRva = 0;
        if (!read(dir + 24, nameCount) || !read(dir + 32, namePointerRva)) {
            return;
        }
        const qint64 namePointers = toFileOffset(namePointerRva);
        if (namePointers < 0) {
            return;
        }
        for (quint32 i = 0; i < nameCount; ++i) {
            quint32 nameRva = 0;
            if (!read(namePointers + static_cast<qint64>(i) * 4, nameRva)) {
                return;
            }
            const qint64 nameOffset = toFileOffset(nameRva);
            if (nameOffset < 0 || nameOffset >= m_data.size()) {
                continue;
            }
            const int end = m_data.indexOf('\0', static_cast<int>(nameOffset));
            if (end < 0) {
                continue;
            }
            m_exports.insert(QString::fromLatin1(
                m_data.constData() + nameOffset, end - static_cast<int>(nameOffset)));
        }
    }

    struct Section
    {
        quint32 virtualSize = 0;
        quint32 virtualAddress = 0;
        quint32 rawSize = 0;
        quint32 rawPointer = 0;
    };

    QByteArray m_data;
    QVector<Section> m_sections;
    QSet<QString> m_exports;
    bool m_valid = false;
};

// Reads the identity block described in NativePlugin.h. The identifier is a
// plain ASCII string; the vendor and category that follow it are UTF-16LE, each
// NUL-terminated and separated by alignment padding.
class MetadataBlock
{
public:
    explicit MetadataBlock(const QByteArray& bytes)
    {
        parse(bytes);
    }

    QString identifier;
    QString vendor;
    QString category;
    QString displayName;

private:
    // Comparison key that ignores spacing, case and punctuation, so that the
    // identifier segment "360Blur" can be matched against the label "360° Blur".
    static QString fold(const QString& s)
    {
        QString out;
        out.reserve(s.size());
        for (const QChar c : s) {
            if (c.isLetterOrNumber()) {
                out.append(c.toLower());
            }
        }
        return out;
    }

    // The label is not at a fixed place: the block is a compiler string pool,
    // ordered by first use rather than by struct layout. What does hold is that
    // exactly one UTF-16 literal in the module folds to the identifier's last
    // segment - checked against the reference catalogue over all 321 shipped
    // plugins: 245 matched, 244 of them confirmed, 1 false positive.
    static QString findDisplayName(const QByteArray& d, const QString& shortName)
    {
        const QString target = fold(shortName);
        if (target.isEmpty()) {
            return QString();
        }
        QString best;
        for (int at = 0; at + 3 < d.size(); at += 2) {
            if (d.at(at) == '\0' || d.at(at + 1) != '\0') {
                continue; // not the start of a plain UTF-16 literal
            }
            int end = 0;
            const QString s = readUtf16(d, at, &end);
            const int chars = (end - at - 2) / 2;
            if (chars < 2 || chars > 60) {
                at = qMax(at, end - 4);
                continue;
            }
            if (!s.isEmpty() && fold(s) == target && (best.isEmpty() || s.size() < best.size())) {
                best = s;
            }
            at = qMax(at, end - 4);
        }
        return best;
    }

    // Accepts the reverse-DNS form the reference uses: lowercase scheme, at
    // least three dot-separated segments, ASCII throughout.
    static bool plausibleIdentifier(const QString& s)
    {
        if (s.size() < 8 || s.size() > 96 || s.count(QLatin1Char('.')) < 3) {
            return false;
        }
        for (const QChar c : s) {
            if (!(c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_')
                  || c == QLatin1Char('-'))) {
                return false;
            }
        }
        return true;
    }

    // Skips the NUL padding between two literals and returns the offset of the
    // next non-zero byte, aligned for a UTF-16 read.
    static int nextField(const QByteArray& d, int from)
    {
        int i = from;
        while (i < d.size() && d.at(i) == '\0') {
            ++i;
        }
        return i & 1 ? i - 1 : i;
    }

    // Reads one NUL-terminated UTF-16LE literal; `end` receives the offset just
    // past its terminator. Returns an empty string when the run is not text.
    static QString readUtf16(const QByteArray& d, int at, int* end)
    {
        int i = at;
        while (i + 1 < d.size() && !(d.at(i) == '\0' && d.at(i + 1) == '\0')) {
            i += 2;
        }
        *end = i + 2;
        if (i <= at) {
            return QString();
        }
        const QString s = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(d.constData() + at), (i - at) / 2);
        for (const QChar c : s) {
            if (c.unicode() < 0x20) {
                return QString();
            }
        }
        return s;
    }

    void parse(const QByteArray& d)
    {
        int at = 0;
        while ((at = d.indexOf("com.", at)) >= 0) {
            const int stop = d.indexOf('\0', at);
            if (stop < 0) {
                return;
            }
            const QString candidate = QString::fromLatin1(d.constData() + at, stop - at);
            if (!plausibleIdentifier(candidate)) {
                at = stop + 1;
                continue;
            }
            identifier = candidate;
            int next = nextField(d, stop + 1);
            int end = 0;
            vendor = readUtf16(d, next, &end);
            category = readUtf16(d, nextField(d, end), &end);
            displayName = findDisplayName(d, identifier.section(QLatin1Char('.'), -1));
            return;
        }
    }
};

} // namespace

QStringList nativePluginNameFilters()
{
    // Reference scanner globs (Tannen.dll): "*.hfpl" and "*.hfplx".
    return {QStringLiteral("*.hfpl"), QStringLiteral("*.hfplx")};
}

NativePluginInfo probeNativePlugin(const QString& filePath)
{
    NativePluginInfo info;
    info.filePath = filePath;
    const QFileInfo fi(filePath);
    info.baseName = fi.completeBaseName();
    info.category = fi.dir().dirName();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }
    const QByteArray bytes = file.readAll();
    const PeImage image(bytes);
    if (!image.valid()) {
        return info;
    }
    info.hasPluginInfo = image.exportsName("PluginInfo");
    info.hasNotify = image.exportsName("Notify");

    const MetadataBlock meta(bytes);
    info.identifier = meta.identifier;
    info.vendor = meta.vendor;
    info.effectCategory = meta.category;
    info.displayName = meta.displayName;
    if (!info.identifier.isEmpty()) {
        info.shortName = info.identifier.section(QLatin1Char('.'), -1);
        // Reference: PluginFile::ReadPluginMetadata builds this key and passes it
        // to QCoreApplication::translate as the context for the plugin's own
        // strings.
        info.translationContext = QStringLiteral("Effect_") + info.shortName;
    }
    return info;
}

#ifdef Q_OS_WIN

namespace {

// The metadata block the reference passes to PluginInfo. Only the slots the
// shipped plugins actually write are named; the rest is left as raw bytes so
// the block keeps its size and alignment whatever else a plugin touches.
constexpr int kMetaBlockBytes = 0x200;
constexpr int kSlotType = 0x00;
constexpr int kSlotGuid = 0x04;
constexpr int kSlotName = 0x18;
constexpr int kSlotCategory = 0x20;
constexpr int kSlotVendor = 0x28;
constexpr int kSlotCopyright = 0x30;
constexpr int kSlotFlags = 0x38;
constexpr int kSlotMagic = 0x58;
constexpr int kSlotVersionMajor = 0x5c;
constexpr int kSlotVersionMinor = 0x60;
constexpr int kSlotIdentifier = 0x68;

constexpr quint32 kPackageMagic = 0x089E31C7;
constexpr int kBiffHostBytes = 336;
constexpr int kBiffApiBytes = 1296;
constexpr int kApiSlotHostName = 0x40;
constexpr int kApiSlotHostBuild = 0x48;
constexpr int kApiSlotPackageInfo = 0x1f8;

// Buffer sizes the reference hands over, in characters.
constexpr int kNameChars = 512;
constexpr int kCategoryChars = 300;
constexpr int kVendorChars = 100;
constexpr int kCopyrightChars = 400;
constexpr int kIdentifierBytes = 100;

using PluginInfoFn = int(__cdecl*)(void*, void*, void*);
using NotifyFn = int(__cdecl*)(void*, void*, int);

// tagBiffAPI+0x1f8. Plugins that need newer host facilities query this before
// constructing their object in Notify(0). 360Glow verifies exactly these three
// 32-bit fields and returns protocol error 7 when they do not match.
int queryHostPackageInfo(void* host, void* output)
{
    Q_UNUSED(host);
    if (!output) {
        return 0;
    }
    auto* fields = static_cast<quint32*>(output);
    fields[0] = kPackageMagic;
    fields[1] = 5;
    fields[2] = 0;
    return 1;
}

// tagBiffAPI+0x48 is the BIFF ABI generation, not the product's visible build
// number.  The 2023 plugin set accepts 2200 or 2500; returning the application's
// build 6000 lets Notify(0) appear to succeed but leaves the plugin's GL wrapper
// disabled (its internal compatibility sentinel stays -1), so uniforms are
// silently ignored during Notify(10).
int queryHostBuild(void* host)
{
    Q_UNUSED(host);
    return 2500;
}

// tagBiffAPI+0x40. Clone, GoPro Lens Reframe, Light Flares V2 and Puppet use
// this stable UTF-16 product identity during their compatibility check.
const wchar_t* queryHostName(void* host)
{
    Q_UNUSED(host);
    static const wchar_t name[] = L"VEGAS Effects";
    return name;
}

void installBootstrapServices(QByteArray& api)
{
    void* hostName = reinterpret_cast<void*>(&queryHostName);
    void* hostBuild = reinterpret_cast<void*>(&queryHostBuild);
    void* packageInfo = reinterpret_cast<void*>(&queryHostPackageInfo);
    std::memcpy(api.data() + kApiSlotHostName, &hostName, sizeof(hostName));
    std::memcpy(api.data() + kApiSlotHostBuild, &hostBuild, sizeof(hostBuild));
    std::memcpy(api.data() + kApiSlotPackageInfo, &packageInfo, sizeof(packageInfo));
}

thread_local int g_lastDiagnosticServiceOffset = -1;

int copyWideStringGuarded(const wchar_t* source, wchar_t* destination, int capacity);
int copyAnsiStringGuarded(const char* source, char* destination, int capacity);

struct DiagnosticServiceCall
{
    int offset = -1;
    std::array<quint64, 11> arguments {};
    std::array<wchar_t, 160> wideArgument1 {};
    std::array<char, 96> ansiArgument2 {};
    std::array<char, 160> ansiArgument3 {};
    // Combo registration passes its translated choices as one UTF-16 string
    // separated by '|'. Text editors pass their default and file editors their
    // filter through the same fifth argument. Snapshot it while the callback is
    // active because a plugin may free a dynamically assembled string before
    // Notify(2) returns.
    std::array<wchar_t, 512> argument4Wide {};
    int longArgument4Index = -1;
};

// Denoise registers a generated bank of more than four hundred controls mixed
// with group/service calls. Keep ample room so its tail is not silently lost.
thread_local std::array<DiagnosticServiceCall, 4096> g_diagnosticServiceCalls;
thread_local int g_diagnosticServiceCallCount = 0;
// Multiline defaults can contain complete XML presets (LightFlaresV2 is far
// larger than the ordinary 512-character labels). Keep a small separate pool
// so every one of the 4096 diagnostic records does not grow by 64 KiB.
thread_local std::array<std::array<wchar_t, 32768>, 16> g_diagnosticLongWideArguments;
thread_local int g_diagnosticLongWideArgumentCount = 0;

quint64 diagnosticFloatBits(float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int __cdecl diagnosticFloatSliderWithDisplayRange(
    quint64 host, const wchar_t* label, const char* unit, const char* key,
    int flags, float minimum, float maximum, float displayMinimum,
    float displayMaximum, float defaultValue, int displayMode)
{
    g_lastDiagnosticServiceOffset = 0x220;
    if (g_diagnosticServiceCallCount < int(g_diagnosticServiceCalls.size())) {
        DiagnosticServiceCall& call =
            g_diagnosticServiceCalls.at(std::size_t(g_diagnosticServiceCallCount++));
        call.offset = g_lastDiagnosticServiceOffset;
        call.arguments = {host, quint64(quintptr(label)), quint64(quintptr(unit)),
                          quint64(quintptr(key)), quint32(flags),
                          diagnosticFloatBits(minimum), diagnosticFloatBits(maximum),
                          diagnosticFloatBits(displayMinimum),
                          diagnosticFloatBits(displayMaximum),
                          diagnosticFloatBits(defaultValue), quint32(displayMode)};
        copyWideStringGuarded(label, call.wideArgument1.data(),
                              int(call.wideArgument1.size()));
        copyAnsiStringGuarded(unit, call.ansiArgument2.data(),
                              int(call.ansiArgument2.size()));
        copyAnsiStringGuarded(key, call.ansiArgument3.data(),
                              int(call.ansiArgument3.size()));
    }
    return 0;
}

int __cdecl diagnosticIntSliderWithDisplayRange(
    quint64 host, const wchar_t* label, const char* unit, const char* key,
    int flags, int minimum, int maximum, int displayMinimum,
    int displayMaximum, int defaultValue)
{
    g_lastDiagnosticServiceOffset = 0x228;
    if (g_diagnosticServiceCallCount < int(g_diagnosticServiceCalls.size())) {
        DiagnosticServiceCall& call =
            g_diagnosticServiceCalls.at(std::size_t(g_diagnosticServiceCallCount++));
        call.offset = g_lastDiagnosticServiceOffset;
        call.arguments = {host, quint64(quintptr(label)), quint64(quintptr(unit)),
                          quint64(quintptr(key)), quint32(flags), quint32(minimum),
                          quint32(maximum), quint32(displayMinimum),
                          quint32(displayMaximum), quint32(defaultValue), 0};
        copyWideStringGuarded(label, call.wideArgument1.data(),
                              int(call.wideArgument1.size()));
        copyAnsiStringGuarded(unit, call.ansiArgument2.data(),
                              int(call.ansiArgument2.size()));
        copyAnsiStringGuarded(key, call.ansiArgument3.data(),
                              int(call.ansiArgument3.size()));
    }
    return 0;
}

template <std::size_t Index>
quint64 diagnosticServiceStub(quint64 a0, quint64 a1, quint64 a2, quint64 a3,
                              quint64 a4, quint64 a5, quint64 a6, quint64 a7,
                              quint64 a8)
{
    g_lastDiagnosticServiceOffset = int(Index * sizeof(void*));
    if (g_diagnosticServiceCallCount < int(g_diagnosticServiceCalls.size())) {
        DiagnosticServiceCall& call =
            g_diagnosticServiceCalls.at(std::size_t(g_diagnosticServiceCallCount++));
        call.offset = g_lastDiagnosticServiceOffset;
        call.arguments = {a0, a1, a2, a3, a4, a5, a6, a7, a8};
        copyWideStringGuarded(reinterpret_cast<const wchar_t*>(quintptr(a1)),
                              call.wideArgument1.data(), int(call.wideArgument1.size()));
        copyAnsiStringGuarded(reinterpret_cast<const char*>(quintptr(a2)),
                              call.ansiArgument2.data(), int(call.ansiArgument2.size()));
        copyAnsiStringGuarded(reinterpret_cast<const char*>(quintptr(a3)),
                              call.ansiArgument3.data(), int(call.ansiArgument3.size()));
        if constexpr (Index * sizeof(void*) == 0x240
                      || Index * sizeof(void*) == 0x248
                      || Index * sizeof(void*) == 0x250
                      || Index * sizeof(void*) == 0x258
                      || Index * sizeof(void*) == 0x260) {
            if (g_diagnosticLongWideArgumentCount
                < int(g_diagnosticLongWideArguments.size())) {
                call.longArgument4Index = g_diagnosticLongWideArgumentCount++;
                auto& destination = g_diagnosticLongWideArguments.at(
                    std::size_t(call.longArgument4Index));
                copyWideStringGuarded(
                    reinterpret_cast<const wchar_t*>(quintptr(a4)),
                    destination.data(), int(destination.size()));
            } else {
                copyWideStringGuarded(
                    reinterpret_cast<const wchar_t*>(quintptr(a4)),
                    call.argument4Wide.data(), int(call.argument4Wide.size()));
            }
        } else if constexpr (Index * sizeof(void*) == 0x78
                             || Index * sizeof(void*) == 0x238) {
            copyWideStringGuarded(reinterpret_cast<const wchar_t*>(quintptr(a4)),
                                  call.argument4Wide.data(),
                                  int(call.argument4Wide.size()));
        }
    }
    return 0;
}

template <std::size_t... Index>
auto makeDiagnosticServiceTable(std::index_sequence<Index...>)
{
    return std::array<void*, sizeof...(Index)> {
        reinterpret_cast<void*>(&diagnosticServiceStub<Index>)...
    };
}

const auto kDiagnosticServiceTable =
    makeDiagnosticServiceTable(std::make_index_sequence<kBiffApiBytes / sizeof(void*)>{});

// Kept free of destructible objects: MSVC will not compile __try in a frame it
// would have to unwind.
int callPluginInfoGuarded(void* entry, void* block, unsigned long* faultCode)
{
    *faultCode = 0;
#ifdef _MSC_VER
    __try {
        return reinterpret_cast<PluginInfoFn>(entry)(nullptr, nullptr, block);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faultCode = GetExceptionCode();
        return -1;
    }
#else
    // MinGW has no reliable __try here; the call goes unguarded, which is why
    // the MSVC build is the supported one for plugin loading.
    return reinterpret_cast<PluginInfoFn>(entry)(nullptr, nullptr, block);
#endif
}

// Kept free of destructible objects: MSVC cannot mix __try with C++ unwinding
// in the same function frame.
int captureNotifyException(EXCEPTION_POINTERS* exception, unsigned long* faultCode,
                           void** instruction, quint64* accessAddress)
{
    *faultCode = exception->ExceptionRecord->ExceptionCode;
    *instruction = exception->ExceptionRecord->ExceptionAddress;
    *accessAddress = exception->ExceptionRecord->NumberParameters >= 2
                         ? quint64(exception->ExceptionRecord->ExceptionInformation[1])
                         : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

int callNotifyGuarded(void* entry, void* host, void* api, int message,
                      unsigned long* faultCode, void** instruction,
                      quint64* accessAddress)
{
    *faultCode = 0;
    *instruction = nullptr;
    *accessAddress = 0;
#ifdef _MSC_VER
    __try {
        return reinterpret_cast<NotifyFn>(entry)(host, api, message);
    } __except (captureNotifyException(GetExceptionInformation(), faultCode,
                                       instruction, accessAddress)) {
        return -1;
    }
#else
    return reinterpret_cast<NotifyFn>(entry)(host, api, message);
#endif
}

// Splits "Chroma Key {greenscreen green screen}" into the name and its search
// keywords - 138 of the 321 shipped plugins carry them.
void splitKeywords(const QString& raw, QString& name, QStringList& keywords)
{
    const int open = raw.indexOf(QLatin1Char('{'));
    if (open < 0) {
        name = raw.trimmed();
        return;
    }
    name = raw.left(open).trimmed();
    const int close = raw.indexOf(QLatin1Char('}'), open);
    const QString inside =
        close < 0 ? raw.mid(open + 1) : raw.mid(open + 1, close - open - 1);
    keywords = inside.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// Registration strings normally point into the loaded image, but malformed
// third-party modules must not be able to fault the scanner after Notify has
// returned.  Keep the SEH frame POD-only and construct QString afterwards.
int copyWideStringGuarded(const wchar_t* source, wchar_t* destination, int capacity)
{
    if (!source || !destination || capacity <= 0) {
        return 0;
    }
#ifdef _MSC_VER
    __try {
#endif
        int length = 0;
        while (length + 1 < capacity && source[length] != L'\0') {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = L'\0';
        return length;
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        destination[0] = L'\0';
        return 0;
    }
#endif
}

int copyAnsiStringGuarded(const char* source, char* destination, int capacity)
{
    if (!source || !destination || capacity <= 0) {
        return 0;
    }
#ifdef _MSC_VER
    __try {
#endif
        int length = 0;
        while (length + 1 < capacity && source[length] != '\0') {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = '\0';
        return length;
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        destination[0] = '\0';
        return 0;
    }
#endif
}

float lowFloat(quint64 value)
{
    const quint32 bits = quint32(value);
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

QString diagnosticArgument4(const DiagnosticServiceCall& call)
{
    if (call.longArgument4Index >= 0
        && call.longArgument4Index < int(g_diagnosticLongWideArguments.size())) {
        return QString::fromWCharArray(
            g_diagnosticLongWideArguments.at(
                std::size_t(call.longArgument4Index)).data());
    }
    return QString::fromWCharArray(call.argument4Wide.data());
}

QVector<EffectParameterSpec> parametersFromDiagnosticCalls()
{
    QVector<EffectParameterSpec> result;
    QSet<QString> keys;
    QStringList groups;
    for (int i = 0; i < g_diagnosticServiceCallCount; ++i) {
        const DiagnosticServiceCall& call = g_diagnosticServiceCalls.at(std::size_t(i));
        if (call.offset == 0x50) {
            const QString group = QString::fromWCharArray(call.wideArgument1.data()).trimmed();
            if (!group.isEmpty()) {
                groups.append(group);
            }
            continue;
        }
        if (call.offset == 0x58) {
            if (!groups.isEmpty()) {
                groups.removeLast();
            }
            continue;
        }
        EffectParameterSpec parameter;
        parameter.displayName = QString::fromWCharArray(call.wideArgument1.data()).trimmed();

        if (call.offset == 0x60 || call.offset == 0x68
            || call.offset == 0x220 || call.offset == 0x228) {
            parameter.unit = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.name = QString::fromUtf8(call.ansiArgument3.data()).trimmed();
            if (call.offset == 0x60 || call.offset == 0x220) {
                parameter.type = QStringLiteral("double");
                parameter.minimum = lowFloat(call.arguments[5]);
                parameter.maximum = lowFloat(call.arguments[6]);
                const int defaultIndex = call.offset == 0x220 ? 9 : 7;
                const float defaultValue = lowFloat(call.arguments[defaultIndex]);
                if (!std::isfinite(parameter.minimum)
                    || !std::isfinite(parameter.maximum)
                    || !std::isfinite(defaultValue)
                    || parameter.minimum > parameter.maximum) {
                    continue;
                }
                parameter.defaultValue = QString::number(defaultValue, 'g', 8);
                const float registeredStep = call.offset == 0x220
                                                 ? 0.0f : lowFloat(call.arguments[8]);
                const double displaySpan = call.offset == 0x220
                                               ? double(lowFloat(call.arguments[8])
                                                        - lowFloat(call.arguments[7]))
                                               : parameter.maximum - parameter.minimum;
                const double span = parameter.maximum - parameter.minimum;
                parameter.step = std::isfinite(registeredStep) && registeredStep > 0.0f
                                     ? registeredStep
                                     : (displaySpan > 0.0
                                            ? qMax(displaySpan / 100.0, 0.001)
                                            : span > 0.0 ? qMax(span / 100.0, 0.001) : 1.0);
                parameter.decimals = parameter.step < 0.01 ? 3
                                     : parameter.step < 0.1 ? 2
                                     : parameter.step < 1.0 ? 1 : 0;
            } else {
                parameter.type = QStringLiteral("int");
                parameter.minimum = qint32(call.arguments[5]);
                parameter.maximum = qint32(call.arguments[6]);
                const int defaultIndex = call.offset == 0x228 ? 9 : 7;
                parameter.defaultValue = QString::number(qint32(call.arguments[defaultIndex]));
                parameter.decimals = 0;
                const qint32 registeredStep = call.offset == 0x228
                                                  ? 0 : qint32(call.arguments[8]);
                parameter.step = registeredStep > 0 ? registeredStep : 1.0;
            }
        } else if (call.offset == 0x70) {
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("bool");
            // The caller writes one byte to the fifth stack argument; upper
            // bytes retain unrelated stack data, so only the low byte is ABI.
            parameter.defaultValue = quint8(call.arguments[4]) != 0
                                         ? QStringLiteral("true")
                                         : QStringLiteral("false");
        } else if (call.offset == 0x78) {
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            const wchar_t* choiceData = call.argument4Wide.data();
            const int choiceCapacity = int(call.argument4Wide.size());
            int choiceLength = 0;
            while (choiceLength < choiceCapacity && choiceData[choiceLength] != L'\0') {
                ++choiceLength;
            }
            if (choiceLength < choiceCapacity) {
                parameter.choices = QString::fromWCharArray(choiceData, choiceLength)
                                        .split(QLatin1Char('|'), Qt::SkipEmptyParts);
            }
            parameter.type = parameter.choices.isEmpty() ? QStringLiteral("int")
                                                         : QStringLiteral("string");
            parameter.minimum = 0.0;
            parameter.maximum = parameter.choices.isEmpty()
                                    ? 100000.0 : parameter.choices.size() - 1;
            parameter.decimals = 0;
            parameter.step = 1.0;
            const int defaultIndex = qint32(call.arguments[6]);
            parameter.defaultValue = defaultIndex >= 0
                                         && defaultIndex < parameter.choices.size()
                                         ? parameter.choices.at(defaultIndex)
                                         : QString::number(defaultIndex);
        } else if (call.offset == 0x90) {
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("color");
            const auto channel = [&call](int index) {
                return qBound(0, qRound(lowFloat(call.arguments[index]) * 255.0f), 255);
            };
            parameter.defaultValue = QStringLiteral("#%1%2%3")
                                         .arg(channel(4), 2, 16, QLatin1Char('0'))
                                         .arg(channel(5), 2, 16, QLatin1Char('0'))
                                         .arg(channel(6), 2, 16, QLatin1Char('0'));
        } else if (call.offset == 0x80 || call.offset == 0x338
                   || call.offset == 0x370) {
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            const int componentCount = call.offset == 0x80 ? 2 : 3;
            QStringList components;
            for (int component = 0; component < componentCount; ++component) {
                components.append(QString::number(lowFloat(call.arguments[4 + component]),
                                                  'g', 8));
            }
            parameter.defaultValue = components.join(QLatin1Char(','));
            parameter.type = call.offset == 0x80 ? QStringLiteral("point2d")
                           : call.offset == 0x338 ? QStringLiteral("orientation")
                                                  : QStringLiteral("point3d");
            parameter.decimals = 3;
            parameter.step = 0.1;
        } else if (call.offset == 0x88) {
            // CreateAngle(host, label, key, flags, degrees, defaultValue).
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("angle");
            parameter.defaultValue = QString::number(lowFloat(call.arguments[5]), 'g', 8);
            const bool degrees = quint8(call.arguments[4]) != 0;
            parameter.unit = degrees ? QString(QChar(0x00b0)) : QStringLiteral("rad");
            parameter.minimum = degrees ? -36000.0 : -1000.0;
            parameter.maximum = degrees ? 36000.0 : 1000.0;
            parameter.decimals = 2;
            parameter.step = degrees ? 1.0 : 0.01;
        } else if (call.offset == 0x230) {
            // CreateButton(host, wide label, ASCII key, flags) stores a bool
            // property. Expose it distinctly so Controls renders an action
            // button rather than an animatable check box.
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("button");
            parameter.defaultValue = QStringLiteral("false");
        } else if (call.offset == 0x4e0) {
            // CreateMaskPicker(host, wide label, ASCII key, flags).
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("mask");
            parameter.defaultValue = QStringLiteral("0");
            parameter.minimum = 0.0;
            parameter.maximum = 100000.0;
            parameter.decimals = 0;
            parameter.step = 1.0;
        } else if (call.offset == 0x98) {
            // Tannen::CreateLayerPicker(host, label, key, flags). The selected
            // value is an FXID, not the visible (editable) layer name.
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("layer");
            parameter.defaultValue = QStringLiteral("00000000-0000-0000-0000-000000000000");
        } else if (call.offset == 0x238) {
            // CreateLabel(host, wide label, ASCII key, flags, wide text).
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            parameter.type = QStringLiteral("label");
            parameter.defaultValue = diagnosticArgument4(call);
        } else if (call.offset == 0x240 || call.offset == 0x248
                   || call.offset == 0x250 || call.offset == 0x258
                   || call.offset == 0x260) {
            // Tannen.dll PluginFile::CreateAPI maps these slots to
            // CreateSingleLineEditor, CreateMultiLineEditor,
            // CreateSaveFilePathEditor, CreateOpenFilePathEditor and
            // CreateDirectoryPath. Text and directory controls end with the
            // default value; file controls add (wide filter, wide default).
            parameter.name = QString::fromUtf8(call.ansiArgument2.data()).trimmed();
            if (call.offset == 0x250 || call.offset == 0x258) {
                parameter.fileFilter =
                    diagnosticArgument4(call);
                // The shipped SDK generation calls these entries with five
                // arguments and supplies only the filter. Tannen's newer host
                // accepts a sixth default-value argument, but reading it from
                // an older caller's stack is unsafe. File properties start
                // empty in those modules.
                parameter.defaultValue.clear();
            } else {
                parameter.defaultValue =
                    diagnosticArgument4(call);
            }
            switch (call.offset) {
            case 0x248: parameter.type = QStringLiteral("multiline"); break;
            case 0x250: parameter.type = QStringLiteral("save-file"); break;
            case 0x258: parameter.type = QStringLiteral("open-file"); break;
            case 0x260: parameter.type = QStringLiteral("directory"); break;
            default: parameter.type = QStringLiteral("string"); break;
            }
        } else {
            continue;
        }

        if (parameter.name.isEmpty() || keys.contains(parameter.name)) {
            continue;
        }
        if (parameter.displayName.isEmpty()) {
            parameter.displayName = parameter.name;
        }
        parameter.group = groups.join(QStringLiteral(" / "));
        keys.insert(parameter.name);
        result.append(parameter);
    }
    return result;
}

} // namespace

NativePluginInfo loadNativePluginMetadata(const QString& filePath,
                                          const QString& dependencySearchPath)
{
    NativePluginInfo info = probeNativePlugin(filePath);
    if (!info.isValid()) {
        info.metadataError = QStringLiteral("missing plugin entry point");
        return info;
    }
    if (nativePluginLoadingDisabled()) {
        info.metadataError = QStringLiteral("OPENVEGAS_NO_NATIVE_PLUGIN_LOAD is set");
        return info;
    }

    // A cookie rather than SetDllDirectory: this runs while the rest of the
    // application is up, and SetDllDirectory is process-wide state that would
    // stay changed after the scan.
    DLL_DIRECTORY_COOKIE cookie = nullptr;
    if (!dependencySearchPath.isEmpty()) {
        const QString nativePath = QDir::toNativeSeparators(dependencySearchPath);
        cookie = AddDllDirectory(reinterpret_cast<const wchar_t*>(nativePath.utf16()));
    }

    const QString nativeFile = QDir::toNativeSeparators(filePath);
    HMODULE module = LoadLibraryExW(reinterpret_cast<const wchar_t*>(nativeFile.utf16()), nullptr,
                                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                                        | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                                        | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!module) {
        info.metadataError =
            QStringLiteral("LoadLibrary failed (%1)").arg(quint32(GetLastError()));
        if (cookie) {
            RemoveDllDirectory(cookie);
        }
        return info;
    }

    void* entry = reinterpret_cast<void*>(GetProcAddress(module, "PluginInfo"));
    if (!entry) {
        info.metadataError = QStringLiteral("PluginInfo is not exported");
        FreeLibrary(module);
        if (cookie) {
            RemoveDllDirectory(cookie);
        }
        return info;
    }

    QByteArray block(kMetaBlockBytes, '\0');
    QVector<wchar_t> nameBuffer(kNameChars, L'\0');
    QVector<wchar_t> categoryBuffer(kCategoryChars, L'\0');
    QVector<wchar_t> vendorBuffer(kVendorChars, L'\0');
    QVector<wchar_t> copyrightBuffer(kCopyrightChars, L'\0');
    QByteArray identifierBuffer(kIdentifierBytes, '\0');

    const auto putPointer = [&block](int offset, void* pointer) {
        std::memcpy(block.data() + offset, &pointer, sizeof(void*));
    };
    putPointer(kSlotName, nameBuffer.data());
    putPointer(kSlotCategory, categoryBuffer.data());
    putPointer(kSlotVendor, vendorBuffer.data());
    putPointer(kSlotCopyright, copyrightBuffer.data());
    putPointer(kSlotIdentifier, identifierBuffer.data());

    unsigned long faultCode = 0;
    callPluginInfoGuarded(entry, block.data(), &faultCode);

    if (faultCode == 0) {
        info.lifecycleChecked = true;
        void* notifyEntry = reinterpret_cast<void*>(GetProcAddress(module, "Notify"));
        QByteArray host(kBiffHostBytes, '\0');
        QByteArray api(kBiffApiBytes, '\0');
        installBootstrapServices(api);
        void* faultInstruction = nullptr;
        quint64 faultAccessAddress = 0;
        info.notifyLoadResult = callNotifyGuarded(
            notifyEntry, host.data(), api.data(), 0, &info.notifyFaultCode,
            &faultInstruction, &faultAccessAddress);
        if (info.notifyFaultCode != 0) {
            info.lifecycleError = QStringLiteral("Notify(0) faulted (0x%1)")
                                      .arg(quint32(info.notifyFaultCode), 8, 16,
                                           QLatin1Char('0'));
        } else if (info.notifyLoadResult != 1) {
            info.lifecycleError = QStringLiteral("Notify(0) returned %1")
                                      .arg(info.notifyLoadResult);
        } else {
            info.lifecycleCompatible = true;
            // Message 2 is the module-owned source of truth for its controls.
            // Unknown services remain harmless diagnostic stubs, while the
            // registration calls below are decoded from their captured ABI
            // arguments. A module may fault after registering a useful prefix;
            // preserve that prefix and report the fault independently.
            installNativePluginDiagnosticStubs(api);
            resetNativePluginDiagnosticServiceOffset();
            info.parametersChecked = true;
            void* parameterFaultInstruction = nullptr;
            quint64 parameterFaultAddress = 0;
            info.notifyParametersResult = callNotifyGuarded(
                notifyEntry, host.data(), api.data(), 2, &info.parametersFaultCode,
                &parameterFaultInstruction, &parameterFaultAddress);
            info.parameters = parametersFromDiagnosticCalls();
            if (info.parametersFaultCode != 0) {
                info.parametersError = QStringLiteral("Notify(2) faulted (0x%1)")
                                           .arg(quint32(info.parametersFaultCode), 8, 16,
                                                QLatin1Char('0'));
            } else if (info.notifyParametersResult != 1) {
                info.parametersError = QStringLiteral("Notify(2) returned %1")
                                           .arg(info.notifyParametersResult);
            }

            info.notifyUnloadResult = callNotifyGuarded(
                notifyEntry, host.data(), api.data(), 1, &info.notifyFaultCode,
                &faultInstruction, &faultAccessAddress);
            if (info.notifyFaultCode != 0) {
                info.lifecycleCompatible = false;
                info.lifecycleError = QStringLiteral("Notify(1) faulted (0x%1)")
                                          .arg(quint32(info.notifyFaultCode), 8, 16,
                                               QLatin1Char('0'));
            } else if (info.notifyUnloadResult != 1) {
                info.lifecycleCompatible = false;
                info.lifecycleError = QStringLiteral("Notify(1) returned %1")
                                          .arg(info.notifyUnloadResult);
            }
        }
    }

    FreeLibrary(module);
    if (cookie) {
        RemoveDllDirectory(cookie);
    }

    if (faultCode != 0) {
        info.metadataError =
            QStringLiteral("PluginInfo faulted (0x%1)").arg(quint32(faultCode), 8, 16, QLatin1Char('0'));
        return info;
    }

    const auto readInt = [&block](int offset) {
        qint32 value = 0;
        std::memcpy(&value, block.constData() + offset, sizeof(value));
        return int(value);
    };

    info.metadataFromModule = true;
    info.moduleType = readInt(kSlotType);
    info.flag0 = readInt(kSlotFlags);
    info.flag1 = readInt(kSlotFlags + 4);
    info.flag2 = readInt(kSlotFlags + 8);
    info.flag3 = readInt(kSlotFlags + 12);
    info.magicOk = quint32(readInt(kSlotMagic)) == kPackageMagic;
    info.versionMajor = readInt(kSlotVersionMajor);
    info.versionMinor = readInt(kSlotVersionMinor);

    // The GUID is laid out like a Windows GUID: first three fields
    // little-endian, the last eight bytes in order.
    const uchar* g = reinterpret_cast<const uchar*>(block.constData() + kSlotGuid);
    info.guid = QStringLiteral("%1%2%3%4-%5%6-%7%8-%9%10%11%12%13%14%15%16")
                    .arg(g[3], 2, 16, QLatin1Char('0'))
                    .arg(g[2], 2, 16, QLatin1Char('0'))
                    .arg(g[1], 2, 16, QLatin1Char('0'))
                    .arg(g[0], 2, 16, QLatin1Char('0'))
                    .arg(g[5], 2, 16, QLatin1Char('0'))
                    .arg(g[4], 2, 16, QLatin1Char('0'))
                    .arg(g[7], 2, 16, QLatin1Char('0'))
                    .arg(g[6], 2, 16, QLatin1Char('0'))
                    .arg(g[8], 2, 16, QLatin1Char('0'))
                    .arg(g[9], 2, 16, QLatin1Char('0'))
                    .arg(g[10], 2, 16, QLatin1Char('0'))
                    .arg(g[11], 2, 16, QLatin1Char('0'))
                    .arg(g[12], 2, 16, QLatin1Char('0'))
                    .arg(g[13], 2, 16, QLatin1Char('0'))
                    .arg(g[14], 2, 16, QLatin1Char('0'))
                    .arg(g[15], 2, 16, QLatin1Char('0'))
                    .toUpper();

    const auto wideString = [](const QVector<wchar_t>& buffer) {
        int length = 0;
        while (length < buffer.size() && buffer.at(length) != L'\0') {
            ++length;
        }
        return QString::fromWCharArray(buffer.constData(), length);
    };

    QString rawName = wideString(nameBuffer);
    QString displayName;
    QStringList keywords;
    splitKeywords(rawName, displayName, keywords);
    if (!displayName.isEmpty()) {
        info.displayName = displayName;
        info.keywords = keywords;
    }

    const QString category = wideString(categoryBuffer);
    if (!category.isEmpty()) {
        // "Keying|Matte Enhancement" - the part after the bar is a sub-category
        // the Effects panel nests under the first.
        info.effectCategory = category.section(QLatin1Char('|'), 0, 0).trimmed();
        info.subCategory = category.section(QLatin1Char('|'), 1).trimmed();
    }
    const QString vendor = wideString(vendorBuffer);
    if (!vendor.isEmpty()) {
        info.vendor = vendor;
    }
    info.copyright = wideString(copyrightBuffer);

    const QString identifier = QString::fromLatin1(identifierBuffer.constData());
    if (!identifier.isEmpty()) {
        info.identifier = identifier;
        info.shortName = identifier.section(QLatin1Char('.'), -1);
        info.translationContext = QStringLiteral("Effect_") + info.shortName;
    }

    return info;
}

NativePluginRuntime::NativePluginRuntime()
    : m_hostBlock(kBiffHostBytes, '\0')
    , m_apiBlock(kBiffApiBytes, '\0')
{
}

NativePluginRuntime::~NativePluginRuntime()
{
    unload();
}

bool NativePluginRuntime::load(const QString& filePath,
                               const QString& dependencySearchPath)
{
    unload();
    m_filePath = filePath;
    m_error.clear();
    m_loadResult = -1;
    m_unloadResult = -1;
    m_faultCode = 0;
    m_faultInstructionRva = 0;
    m_faultModulePath.clear();
    m_faultAccessAddress = 0;
    m_renderingCompatibilityRva = 0;
    m_hostBlock.fill('\0');
    m_apiBlock.fill('\0');
    installBootstrapServices(m_apiBlock);

    const NativePluginInfo staticInfo = probeNativePlugin(filePath);
    if (!staticInfo.isValid()) {
        m_error = QStringLiteral("missing PluginInfo or Notify export");
        return false;
    }
    if (nativePluginLoadingDisabled()) {
        m_error = QStringLiteral("OPENVEGAS_NO_NATIVE_PLUGIN_LOAD is set");
        return false;
    }

    if (!dependencySearchPath.isEmpty()) {
        const QString nativePath = QDir::toNativeSeparators(dependencySearchPath);
        m_dllDirectoryCookie = AddDllDirectory(
            reinterpret_cast<const wchar_t*>(nativePath.utf16()));
    }

    const QString nativeFile = QDir::toNativeSeparators(filePath);
    const HMODULE module = LoadLibraryExW(
        reinterpret_cast<const wchar_t*>(nativeFile.utf16()), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
            | LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (!module) {
        m_error = QStringLiteral("LoadLibrary failed (%1)").arg(quint32(GetLastError()));
        if (m_dllDirectoryCookie) {
            RemoveDllDirectory(static_cast<DLL_DIRECTORY_COOKIE>(m_dllDirectoryCookie));
            m_dllDirectoryCookie = nullptr;
        }
        return false;
    }

    m_module = module;
    m_notify = reinterpret_cast<void*>(GetProcAddress(module, "Notify"));
    if (!m_notify) {
        m_error = QStringLiteral("Notify is not exported");
        unload();
        return false;
    }

    m_loadResult = notify(0);
    if (m_faultCode != 0) {
        m_error = QStringLiteral("Notify(0) faulted (0x%1)")
                      .arg(quint32(m_faultCode), 8, 16, QLatin1Char('0'));
        // The plugin may have left a half-constructed object in api+8. Calling
        // message 1 on that state is less safe than releasing the module only.
        m_notify = nullptr;
        FreeLibrary(static_cast<HMODULE>(m_module));
        m_module = nullptr;
        if (m_dllDirectoryCookie) {
            RemoveDllDirectory(static_cast<DLL_DIRECTORY_COOKIE>(m_dllDirectoryCookie));
            m_dllDirectoryCookie = nullptr;
        }
        return false;
    }
    if (m_loadResult != 1) {
        m_error = QStringLiteral("Notify(0) returned %1").arg(m_loadResult);
        unload();
        return false;
    }
    return true;
}

int NativePluginRuntime::notify(int message)
{
    if (!m_module || !m_notify) {
        return -1;
    }
    m_faultCode = 0;
    void* faultInstruction = nullptr;
    quint64 faultAccessAddress = 0;
    const int result = callNotifyGuarded(m_notify, m_hostBlock.data(),
                                         m_apiBlock.data(), message, &m_faultCode,
                                         &faultInstruction, &faultAccessAddress);
    m_faultModulePath.clear();
    if (faultInstruction) {
        HMODULE faultModule = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(faultInstruction),
                              &faultModule)
            && faultModule) {
            m_faultInstructionRva = quint64(
                reinterpret_cast<quintptr>(faultInstruction)
                - reinterpret_cast<quintptr>(faultModule));
            std::array<wchar_t, 32768> modulePath {};
            const DWORD length = GetModuleFileNameW(
                faultModule, modulePath.data(), DWORD(modulePath.size()));
            if (length > 0 && length < modulePath.size()) {
                m_faultModulePath = QString::fromWCharArray(
                    modulePath.data(), qsizetype(length));
            }
        } else if (m_module) {
            m_faultInstructionRva = quint64(
                reinterpret_cast<quintptr>(faultInstruction)
                - reinterpret_cast<quintptr>(m_module));
        } else {
            m_faultInstructionRva = quint64(
                reinterpret_cast<quintptr>(faultInstruction));
        }
    } else {
        m_faultInstructionRva = 0;
    }
    m_faultAccessAddress = faultAccessAddress;
    if (m_faultCode != 0) {
        m_error = QStringLiteral("Notify(%1) faulted (0x%2)")
                      .arg(message)
                      .arg(quint32(m_faultCode), 8, 16, QLatin1Char('0'));
    }
    return result;
}

bool NativePluginRuntime::enableRenderingCompatibility()
{
    if (!m_module) {
        return false;
    }
    auto* base = static_cast<unsigned char*>(m_module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    QHash<quint64, int> references;
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (quint16 sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
         ++sectionIndex) {
        const IMAGE_SECTION_HEADER& section = sections[sectionIndex];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        const quint64 start = section.VirtualAddress;
        const quint64 size = section.Misc.VirtualSize;
        if (start + size > nt->OptionalHeader.SizeOfImage || size < 7) {
            continue;
        }
        const unsigned char* code = base + start;
        for (quint64 i = 0; i + 7 <= size; ++i) {
            // cmp dword ptr [rip+disp32], -1
            if (code[i] != 0x83 || code[i + 1] != 0x3d || code[i + 6] != 0xff) {
                continue;
            }
            qint32 displacement = 0;
            std::memcpy(&displacement, code + i + 2, sizeof(displacement));
            const qint64 target = qint64(start + i + 7) + displacement;
            if (target < 0 || quint64(target + 4) > nt->OptionalHeader.SizeOfImage) {
                continue;
            }
            bool writable = false;
            for (quint16 targetSection = 0;
                 targetSection < nt->FileHeader.NumberOfSections; ++targetSection) {
                const IMAGE_SECTION_HEADER& candidate = sections[targetSection];
                const quint64 candidateStart = candidate.VirtualAddress;
                const quint64 candidateEnd = candidateStart + candidate.Misc.VirtualSize;
                if (quint64(target) >= candidateStart && quint64(target + 4) <= candidateEnd) {
                    writable = (candidate.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
                    break;
                }
            }
            if (writable) {
                ++references[quint64(target)];
            }
        }
    }

    quint64 bestRva = 0;
    int bestReferences = 0;
    for (auto it = references.constBegin(); it != references.constEnd(); ++it) {
        qint32 value = 0;
        std::memcpy(&value, base + it.key(), sizeof(value));
        // LoadLibrary returns the already-mapped image when another render
        // thread owns an instance of the same module. In that case the shared
        // SDK sentinel is already 1; accept it as the same candidate instead
        // of treating the second runtime as incompatible.
        if ((value == -1 || value == 1) && it.value() > bestReferences) {
            bestRva = it.key();
            bestReferences = it.value();
        }
    }
    // A single comparison can be any ordinary cache.  The SDK sentinel is
    // shared by the uniform lookup/setter and compatibility paths and is
    // therefore referenced repeatedly (eight times in Invert).
    if (bestReferences < 3) {
        return false;
    }
    const qint32 compatible = 1;
    std::memcpy(base + bestRva, &compatible, sizeof(compatible));
    m_renderingCompatibilityRva = bestRva;
    return true;
}

void NativePluginRuntime::unload()
{
    if (m_module) {
        if (m_notify && m_loadResult == 1) {
            m_unloadResult = notify(1);
        }
        FreeLibrary(static_cast<HMODULE>(m_module));
    }
    m_module = nullptr;
    m_notify = nullptr;
    if (m_dllDirectoryCookie) {
        RemoveDllDirectory(static_cast<DLL_DIRECTORY_COOKIE>(m_dllDirectoryCookie));
        m_dllDirectoryCookie = nullptr;
    }
    m_hostBlock.fill('\0');
    m_apiBlock.fill('\0');
    m_renderingCompatibilityRva = 0;
}

NativePluginLifecycleProbe probeNativePluginLifecycle(
    const QString& filePath, const QString& dependencySearchPath)
{
    NativePluginLifecycleProbe result;
    NativePluginRuntime runtime;
    result.loaded = runtime.load(filePath, dependencySearchPath);
    result.loadResult = runtime.loadResult();
    result.faultCode = runtime.lastFaultCode();
    result.faultInstructionRva = runtime.lastFaultInstructionRva();
    result.faultAccessAddress = runtime.lastFaultAccessAddress();
    result.error = runtime.errorString();
    if (result.loaded) {
        runtime.unload();
        result.unloadResult = runtime.unloadResult();
        result.faultCode = runtime.lastFaultCode();
        result.faultInstructionRva = runtime.lastFaultInstructionRva();
        result.faultAccessAddress = runtime.lastFaultAccessAddress();
        if (!runtime.errorString().isEmpty()) {
            result.error = runtime.errorString();
        }
    }
    return result;
}

void installNativePluginDiagnosticStubs(QByteArray& apiBlock, int firstOffset,
                                        int lastOffset, bool installLegacyTable)
{
    // Some SDK generations keep the oldest registration helpers in a nested
    // table referenced by tagBiffAPI[0].  ColorCorrection reads +0x18 from it;
    // Levels reads +0x08, and ChromaKey/Derez use the same +0x18 helper after
    // registering their ordinary controls.  Leaving this pointer null made
    // those four modules fault before Notify(2) could finish.
    if (installLegacyTable && apiBlock.size() >= int(sizeof(void*))) {
        const void* legacyServices = static_cast<const void*>(kDiagnosticServiceTable.data());
        std::memcpy(apiBlock.data(), &legacyServices, sizeof(legacyServices));
    }

    const int slotCount = qMin(int(kDiagnosticServiceTable.size()),
                               apiBlock.size() / int(sizeof(void*)));
    const int firstSlot = qBound(0x30 / int(sizeof(void*)),
                                 firstOffset / int(sizeof(void*)), slotCount);
    const int requestedLastSlot = lastOffset < 0
                                      ? slotCount - 1
                                      : lastOffset / int(sizeof(void*));
    const int lastSlot = qBound(firstSlot - 1, requestedLastSlot, slotCount - 1);
    for (int slot = firstSlot; slot <= lastSlot; ++slot) {
        void* current = nullptr;
        std::memcpy(&current, apiBlock.constData() + slot * int(sizeof(void*)),
                    sizeof(current));
        if (!current) {
            const void* callback = kDiagnosticServiceTable.at(std::size_t(slot));
            std::memcpy(apiBlock.data() + slot * int(sizeof(void*)), &callback,
                        sizeof(callback));
        }
    }
    if (apiBlock.size() >= 0x230 && firstOffset <= 0x228
        && (lastOffset < 0 || lastOffset >= 0x220)) {
        const void* floatDisplay =
            reinterpret_cast<void*>(&diagnosticFloatSliderWithDisplayRange);
        const void* intDisplay =
            reinterpret_cast<void*>(&diagnosticIntSliderWithDisplayRange);
        if (firstOffset <= 0x220 && (lastOffset < 0 || lastOffset >= 0x220)) {
            std::memcpy(apiBlock.data() + 0x220, &floatDisplay, sizeof(floatDisplay));
        }
        if (firstOffset <= 0x228 && (lastOffset < 0 || lastOffset >= 0x228)) {
            std::memcpy(apiBlock.data() + 0x228, &intDisplay, sizeof(intDisplay));
        }
    }
}

void resetNativePluginDiagnosticServiceOffset()
{
    g_lastDiagnosticServiceOffset = -1;
    g_diagnosticServiceCallCount = 0;
    g_diagnosticLongWideArgumentCount = 0;
}

int lastNativePluginDiagnosticServiceOffset()
{
    return g_lastDiagnosticServiceOffset;
}

QString nativePluginDiagnosticTrace()
{
    QStringList events;
    for (int i = 0; i < g_diagnosticServiceCallCount; ++i) {
        const DiagnosticServiceCall& call = g_diagnosticServiceCalls.at(std::size_t(i));
        QStringList args;
        for (const quint64 value : call.arguments) {
            args.append(QString::number(value, 16));
        }
        events.append(QStringLiteral("0x%1(%2)")
                          .arg(call.offset, 0, 16)
                          .arg(args.join(QLatin1Char(','))));
    }
    return events.join(QLatin1Char(';'));
}

#else // !Q_OS_WIN

NativePluginInfo loadNativePluginMetadata(const QString& filePath,
                                          const QString& dependencySearchPath)
{
    Q_UNUSED(dependencySearchPath);
    NativePluginInfo info = probeNativePlugin(filePath);
    info.metadataError = QStringLiteral("native plugins are Windows modules");
    return info;
}

NativePluginRuntime::NativePluginRuntime()
    : m_hostBlock(336, '\0')
    , m_apiBlock(1296, '\0')
{
}

NativePluginRuntime::~NativePluginRuntime() = default;

bool NativePluginRuntime::load(const QString& filePath,
                               const QString& dependencySearchPath)
{
    Q_UNUSED(dependencySearchPath);
    m_filePath = filePath;
    m_error = QStringLiteral("native plugins are Windows modules");
    return false;
}

void NativePluginRuntime::unload()
{
}

int NativePluginRuntime::notify(int message)
{
    Q_UNUSED(message);
    return -1;
}

bool NativePluginRuntime::enableRenderingCompatibility()
{
    return false;
}

NativePluginLifecycleProbe probeNativePluginLifecycle(
    const QString& filePath, const QString& dependencySearchPath)
{
    Q_UNUSED(filePath);
    Q_UNUSED(dependencySearchPath);
    NativePluginLifecycleProbe result;
    result.error = QStringLiteral("native plugins are Windows modules");
    return result;
}

void installNativePluginDiagnosticStubs(QByteArray& apiBlock, int, int, bool)
{
    Q_UNUSED(apiBlock);
}

void resetNativePluginDiagnosticServiceOffset()
{
}

int lastNativePluginDiagnosticServiceOffset()
{
    return -1;
}

QString nativePluginDiagnosticTrace()
{
    return QString();
}

#endif // Q_OS_WIN

QJsonObject nativePluginToJson(const NativePluginInfo& info)
{
    QJsonObject object;
    object.insert(QStringLiteral("baseName"), info.baseName);
    object.insert(QStringLiteral("category"), info.category);
    object.insert(QStringLiteral("hasPluginInfo"), info.hasPluginInfo);
    object.insert(QStringLiteral("hasNotify"), info.hasNotify);
    object.insert(QStringLiteral("identifier"), info.identifier);
    object.insert(QStringLiteral("vendor"), info.vendor);
    object.insert(QStringLiteral("effectCategory"), info.effectCategory);
    object.insert(QStringLiteral("shortName"), info.shortName);
    object.insert(QStringLiteral("translationContext"), info.translationContext);
    object.insert(QStringLiteral("displayName"), info.displayName);
    object.insert(QStringLiteral("fromModule"), info.metadataFromModule);
    object.insert(QStringLiteral("lifecycleChecked"), info.lifecycleChecked);
    object.insert(QStringLiteral("lifecycleCompatible"), info.lifecycleCompatible);
    object.insert(QStringLiteral("notifyLoadResult"), info.notifyLoadResult);
    object.insert(QStringLiteral("notifyUnloadResult"), info.notifyUnloadResult);
    object.insert(QStringLiteral("notifyFaultCode"), int(info.notifyFaultCode));
    object.insert(QStringLiteral("lifecycleError"), info.lifecycleError);
    object.insert(QStringLiteral("parametersChecked"), info.parametersChecked);
    object.insert(QStringLiteral("notifyParametersResult"), info.notifyParametersResult);
    object.insert(QStringLiteral("parametersFaultCode"), int(info.parametersFaultCode));
    object.insert(QStringLiteral("parametersError"), info.parametersError);
    QJsonArray parameters;
    for (const EffectParameterSpec& parameter : info.parameters) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), parameter.name);
        item.insert(QStringLiteral("displayName"), parameter.displayName);
        item.insert(QStringLiteral("type"), parameter.type);
        item.insert(QStringLiteral("defaultValue"), parameter.defaultValue);
        item.insert(QStringLiteral("unit"), parameter.unit);
        item.insert(QStringLiteral("fileFilter"), parameter.fileFilter);
        item.insert(QStringLiteral("minimum"), parameter.minimum);
        item.insert(QStringLiteral("maximum"), parameter.maximum);
        item.insert(QStringLiteral("decimals"), parameter.decimals);
        item.insert(QStringLiteral("step"), parameter.step);
        item.insert(QStringLiteral("choices"), QJsonArray::fromStringList(parameter.choices));
        item.insert(QStringLiteral("group"), parameter.group);
        parameters.append(item);
    }
    object.insert(QStringLiteral("parameters"), parameters);
    object.insert(QStringLiteral("moduleType"), info.moduleType);
    object.insert(QStringLiteral("guid"), info.guid);
    object.insert(QStringLiteral("magicOk"), info.magicOk);
    object.insert(QStringLiteral("versionMajor"), info.versionMajor);
    object.insert(QStringLiteral("versionMinor"), info.versionMinor);
    object.insert(QStringLiteral("flags"), QJsonArray{info.flag0, info.flag1, info.flag2,
                                                      info.flag3});
    object.insert(QStringLiteral("copyright"), info.copyright);
    object.insert(QStringLiteral("keywords"), QJsonArray::fromStringList(info.keywords));
    object.insert(QStringLiteral("subCategory"), info.subCategory);
    return object;
}

NativePluginInfo nativePluginFromJson(const QJsonObject& object)
{
    NativePluginInfo info;
    info.baseName = object.value(QStringLiteral("baseName")).toString();
    info.category = object.value(QStringLiteral("category")).toString();
    info.hasPluginInfo = object.value(QStringLiteral("hasPluginInfo")).toBool();
    info.hasNotify = object.value(QStringLiteral("hasNotify")).toBool();
    info.identifier = object.value(QStringLiteral("identifier")).toString();
    info.vendor = object.value(QStringLiteral("vendor")).toString();
    info.effectCategory = object.value(QStringLiteral("effectCategory")).toString();
    info.shortName = object.value(QStringLiteral("shortName")).toString();
    info.translationContext = object.value(QStringLiteral("translationContext")).toString();
    info.displayName = object.value(QStringLiteral("displayName")).toString();
    info.metadataFromModule = object.value(QStringLiteral("fromModule")).toBool();
    info.lifecycleChecked = object.value(QStringLiteral("lifecycleChecked")).toBool();
    info.lifecycleCompatible = object.value(QStringLiteral("lifecycleCompatible")).toBool();
    info.notifyLoadResult = object.value(QStringLiteral("notifyLoadResult")).toInt(-1);
    info.notifyUnloadResult = object.value(QStringLiteral("notifyUnloadResult")).toInt(-1);
    info.notifyFaultCode =
        static_cast<unsigned long>(object.value(QStringLiteral("notifyFaultCode")).toInt());
    info.lifecycleError = object.value(QStringLiteral("lifecycleError")).toString();
    info.parametersChecked = object.value(QStringLiteral("parametersChecked")).toBool();
    info.notifyParametersResult =
        object.value(QStringLiteral("notifyParametersResult")).toInt(-1);
    info.parametersFaultCode = static_cast<unsigned long>(
        object.value(QStringLiteral("parametersFaultCode")).toInt());
    info.parametersError = object.value(QStringLiteral("parametersError")).toString();
    const QJsonArray parameters = object.value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue& value : parameters) {
        const QJsonObject item = value.toObject();
        EffectParameterSpec parameter;
        parameter.name = item.value(QStringLiteral("name")).toString();
        parameter.displayName = item.value(QStringLiteral("displayName")).toString();
        parameter.type = item.value(QStringLiteral("type")).toString(QStringLiteral("string"));
        parameter.defaultValue = item.value(QStringLiteral("defaultValue")).toString();
        parameter.unit = item.value(QStringLiteral("unit")).toString();
        parameter.fileFilter = item.value(QStringLiteral("fileFilter")).toString();
        parameter.minimum = item.value(QStringLiteral("minimum")).toDouble(parameter.minimum);
        parameter.maximum = item.value(QStringLiteral("maximum")).toDouble(parameter.maximum);
        parameter.decimals = item.value(QStringLiteral("decimals")).toInt(parameter.decimals);
        parameter.step = item.value(QStringLiteral("step")).toDouble(parameter.step);
        for (const QJsonValue& choice : item.value(QStringLiteral("choices")).toArray()) {
            parameter.choices.append(choice.toString());
        }
        parameter.group = item.value(QStringLiteral("group")).toString();
        if (!parameter.name.isEmpty()) {
            info.parameters.append(parameter);
        }
    }
    info.moduleType = object.value(QStringLiteral("moduleType")).toInt(-1);
    info.guid = object.value(QStringLiteral("guid")).toString();
    info.magicOk = object.value(QStringLiteral("magicOk")).toBool();
    info.versionMajor = object.value(QStringLiteral("versionMajor")).toInt();
    info.versionMinor = object.value(QStringLiteral("versionMinor")).toInt();
    const QJsonArray flags = object.value(QStringLiteral("flags")).toArray();
    if (flags.size() == 4) {
        info.flag0 = flags.at(0).toInt();
        info.flag1 = flags.at(1).toInt();
        info.flag2 = flags.at(2).toInt();
        info.flag3 = flags.at(3).toInt();
    }
    info.copyright = object.value(QStringLiteral("copyright")).toString();
    const QJsonArray keywords = object.value(QStringLiteral("keywords")).toArray();
    for (const QJsonValue& value : keywords) {
        info.keywords.append(value.toString());
    }
    info.subCategory = object.value(QStringLiteral("subCategory")).toString();
    return info;
}

QString displayNameForIdentifier(const QString& identifier, const QString& fallback)
{
    const QString tail = identifier.section(QLatin1Char('.'), -1);
    if (tail.isEmpty()) {
        return fallback;
    }

    // Split on case transitions so "HighpassSharpen" reads as two words, while
    // runs of capitals stay together ("HSL", "RGB") unless a lowercase letter
    // follows, which starts the next word ("HSLMaster" -> "HSL Master").
    QString out;
    for (int i = 0; i < tail.size(); ++i) {
        const QChar c = tail.at(i);
        if (i > 0) {
            const QChar prev = tail.at(i - 1);
            const bool boundary =
                (c.isUpper() && (prev.isLower() || prev.isDigit()))
                || (c.isUpper() && prev.isUpper() && i + 1 < tail.size()
                    && tail.at(i + 1).isLower())
                || (c.isDigit() && prev.isLetter())
                || (c.isLetter() && prev.isDigit());
            if (boundary) {
                out.append(QLatin1Char(' '));
            }
        }
        out.append(c);
    }
    return out.isEmpty() ? fallback : out;
}

PluginKind kindForModuleType(int type)
{
    // Verified against every shipped plugin: the type a module reports and the
    // folder it ships in agree for all 321. Note this is not the mapping the
    // earlier notes recorded - three of the six were transposed there.
    switch (type) {
    case 0: return PluginKind::Effect2D;         // 241, Plugins/2D
    case 1: return PluginKind::VideoTransition;  //  15, Plugins/VideoTransitions
    case 2: return PluginKind::AudioTransition;  //   2, Plugins/AudioTransitions
    case 3: return PluginKind::AudioEffect;      //  16, Plugins/Audio
    case 4: return PluginKind::GeometryEffect;   //   4, Plugins/Geometry
    case 5: return PluginKind::BehaviorEffect;   //  43, Plugins/Behavior
    default: break;
    }
    return PluginKind::Unknown;
}

bool nativePluginLoadingDisabled()
{
    return qEnvironmentVariableIsSet("OPENVEGAS_NO_NATIVE_PLUGIN_LOAD")
           && qgetenv("OPENVEGAS_NO_NATIVE_PLUGIN_LOAD") != "0";
}

PluginKind kindForNativeCategory(const QString& folderName)
{
    // Folder names as shipped by the reference, matching its plugin wrapper
    // classes (Plugin2DEffect, PluginAudioEffect, PluginAudioTransition,
    // PluginBehaviorEffect, PluginGeometryEffect, PluginVideoTransition).
    const QString f = folderName.toLower();
    if (f == QLatin1String("2d"))               return PluginKind::Effect2D;
    if (f == QLatin1String("audio"))            return PluginKind::AudioEffect;
    if (f == QLatin1String("audiotransitions")) return PluginKind::AudioTransition;
    if (f == QLatin1String("behavior"))         return PluginKind::BehaviorEffect;
    if (f == QLatin1String("geometry"))         return PluginKind::GeometryEffect;
    if (f == QLatin1String("videotransitions")) return PluginKind::VideoTransition;
    return PluginKind::Unknown;
}

} // namespace plugin
} // namespace openvegas
