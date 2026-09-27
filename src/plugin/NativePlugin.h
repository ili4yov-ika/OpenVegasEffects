#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "plugin/EffectSpec.h"
#include "plugin/PluginId.h"

namespace openvegas {
namespace plugin {

// Probe for the reference's native plugin format. Recovered from Tannen.dll
// (biff::tannen::PluginFile):
//
//   * plugins are ordinary Windows DLLs carrying the extension ".hfpl" or
//     ".hfplx" and live in per-category sub-folders of "Plugins";
//   * every plugin must export exactly two entry points -
//         int PluginInfo(BiffHost*, tagBiffAPI*, PluginMetadata* out);
//         int Notify(void*, void*, int message);
//     the host rejects the file with "Missing API" when either is absent;
//   * the metadata block starts with a package magic and a major/minor pair;
//     the host refuses packages whose version is newer than its own build.
//
// This port validates a candidate by reading the PE export table directly
// instead of calling LoadLibrary, so scanning never executes foreign code and
// never fails on an architecture mismatch.
//
// Every plugin shipped with the reference also carries an identity block in
// .rdata, which probeNativePlugin() recovers the same way:
//
//   ASCII  "com.FXHOME.HitFilm.<Id>"  - the plugin's unique identifier; note it
//                                       does not always match the file name
//                                       (360LightsaberV2Auto.hfpl declares
//                                       ...HitFilm.360LightswordAuto)
//   UTF-16 "FXhome"                   - vendor
//   UTF-16 "<Category>"               - the category the Effects panel groups it
//                                       under; the flat "2D" folder expands into
//                                       21 of these ("Keying", "Color Grading",
//                                       "Lights & Flares", ...)
//
// Further literals follow (parameter labels, units such as "px"/"ms", and enum
// options), but static inspection cannot tell those roles apart - that needs the
// PluginInfo struct layout from Tannen.dll - so they are not exposed here.
struct NativePluginInfo
{
    QString filePath;
    QString baseName;   // file name without the extension
    QString category;   // parent folder, e.g. "2D", "Audio", "VideoTransitions"
    bool hasPluginInfo = false;
    bool hasNotify = false;

    // Recovered from the module's .rdata metadata block (see below). Empty when
    // the file carries no such block.
    QString identifier;      // "com.FXHOME.HitFilm.AudioEcho"
    QString vendor;          // "FXhome"
    QString effectCategory;  // "Audio", "360° Video", "Transitions - Video", ...

    // Trailing segment of the identifier: "AudioEcho". The reference derives it
    // the same way, by cutting at the last dot - which is why the plugin shipped
    // as 360LightsaberV2Auto.hfpl is keyed as 360LightswordAuto.
    QString shortName;

    // Qt translation context the reference uses for this plugin's own strings:
    // "Effect_" + shortName. Its catalogues carry one such context per plugin
    // (321 of them in VegasEffects_ja.qm), holding the display name, the
    // category, parameter labels, units and enum choices.
    QString translationContext;

    // Name shown in the effect browser, e.g. "360° Blur". Recovered from the
    // module's string pool; empty when it could not be identified, in which case
    // displayNameForIdentifier() provides a readable fallback.
    QString displayName;

    // --- filled only by loadNativePluginMetadata() ------------------------
    //
    // The fields above are scraped from .rdata without running anything. These
    // come from the module's own PluginInfo entry point and are what the
    // reference actually keys on.

    bool metadataFromModule = false;   // PluginInfo really ran
    QString metadataError;             // why it did not, when it did not

    // The first uncached scan also exercises the lightweight Notify(0/1)
    // lifecycle while the DLL is already resident. No GL context is needed at
    // this stage. The result is cached with the metadata.
    bool lifecycleChecked = false;
    bool lifecycleCompatible = false;
    int notifyLoadResult = -1;
    int notifyUnloadResult = -1;
    unsigned long notifyFaultCode = 0;
    QString lifecycleError;

    // Notify(2) asks a module to register its controls in tagBiffAPI.  The
    // common scalar/integer/bool/enum/RGB registrations are decoded and cached
    // here, so the inspector can be built from the DLL instead of a handwritten
    // table.  A fault leaves any controls registered before it available.
    bool parametersChecked = false;
    int notifyParametersResult = -1;
    unsigned long parametersFaultCode = 0;
    QString parametersError;
    QVector<EffectParameterSpec> parameters;

    // Reference PluginMetadata +0x00. Its wrapper classes map onto it as
    // 0 = 2D effect, 1 = video transition, 2 = audio transition,
    // 3 = audio effect, 4 = geometry effect, 5 = behaviour effect - verified
    // against all 321 shipped plugins, whose folder matches the type exactly.
    int moduleType = -1;

    // +0x04: a 16-byte GUID, unique per plugin across the whole set (321
    // distinct values for 321 files). This is the FXID the reference's
    // PluginByID() looks a plugin up by, which is why a duplicated
    // "com.FXHOME.HitFilm.*" identifier does not confuse it.
    QString guid;

    // +0x58 magic (0x089E31C7) and +0x5c/+0x60 package version. The reference
    // refuses anything whose major is 22 or newer, or 21 with minor >= 2; every
    // shipped plugin reports 5.0.
    bool magicOk = false;
    int versionMajor = 0;
    int versionMinor = 0;

    // +0x38..+0x44. Their meaning is not recovered; recorded because they are
    // part of the block and vary across the set.
    int flag0 = 0;
    int flag1 = 0;
    int flag2 = 0;
    int flag3 = 0;

    QString copyright;

    // The name a plugin reports carries its search keywords in braces:
    // "360° Blur {360° 2°° equirectangular equidistant}". The braces are cut
    // off the display name and kept here - 138 of the 321 shipped plugins have
    // them, and they are what the Effects panel's search matches on.
    QStringList keywords;

    // A category can name a sub-category after a "|": "Keying|Matte
    // Enhancement", "Transitions - Video|Wipe".
    QString subCategory;

    bool isValid() const { return hasPluginInfo && hasNotify; }
};

// Calls the module's own PluginInfo entry point and returns what it reports.
//
// The contract was established by calling the real plugins rather than read off
// a decompilation, because the decompiled shape is misleading: PluginMetadata
// does not hold its strings inline. Its string fields are POINTERS the caller
// fills in with its own buffers, which the plugin then wcsncpy/strncpy's into:
//
//     +0x18  wchar_t* name        512 wide characters
//     +0x20  wchar_t* category    300
//     +0x28  wchar_t* vendor      100
//     +0x30  wchar_t* copyright   400
//     +0x68  char*    identifier  100 bytes, ANSI
//
// (Calling it with those slots left null faults inside wcsncpy, which is how
// the shape was found.) The host and api arguments are never touched by any of
// the 321 shipped plugins, so null is passed for both - this port has neither
// the BiffHost nor the tagBiffAPI layout, and does not need them here.
//
// `dependencySearchPath`, when not empty, is added to the module search path:
// four of the shipped plugins link opencv_world460.dll and one more module from
// the application folder, and without it LoadLibrary fails for a reason that has
// nothing to do with the plugin.
//
// This runs third-party code. The call is guarded, and a plugin that faults is
// reported through metadataError rather than taking the process with it. Set
// OPENVEGAS_NO_NATIVE_PLUGIN_LOAD=1 to skip loading entirely and keep only what
// static inspection can see.
NativePluginInfo loadNativePluginMetadata(const QString& filePath,
                                          const QString& dependencySearchPath = QString());

// Owns one live native module and the persistent memory blocks passed to its
// Notify entry point. The reference allocates a 336-byte BiffHost and a
// 1296-byte tagBiffAPI. Notify(..., 0) creates the plugin-side object (Invert
// stores it at tagBiffAPI+0x08); Notify(..., 1) destroys that object before the
// DLL is released.
class NativePluginRuntime
{
public:
    NativePluginRuntime();
    ~NativePluginRuntime();

    NativePluginRuntime(const NativePluginRuntime&) = delete;
    NativePluginRuntime& operator=(const NativePluginRuntime&) = delete;

    bool load(const QString& filePath,
              const QString& dependencySearchPath = QString());
    void unload();

    bool isLoaded() const { return m_module != nullptr; }
    QString filePath() const { return m_filePath; }
    QString errorString() const { return m_error; }
    int loadResult() const { return m_loadResult; }
    int unloadResult() const { return m_unloadResult; }
    unsigned long lastFaultCode() const { return m_faultCode; }
    quint64 lastFaultInstructionRva() const { return m_faultInstructionRva; }
    QString lastFaultModulePath() const { return m_faultModulePath; }
    quint64 lastFaultAccessAddress() const { return m_faultAccessAddress; }

    // Sends one protocol message using the same host/API addresses. Returns -1
    // when the module is not loaded or its code raises a structured exception.
    int notify(int message);

    // The shipped SDK wraps its GL calls behind a per-module compatibility
    // sentinel.  Outside the original signed host its handshake leaves that
    // sentinel at -1: Notify still reports success, but all uniform setters
    // become no-ops.  Locate the shared SDK sentinel by its repeated x64
    // references and mark the already-compatible host path active.  This only
    // changes the mapped data page; the plugin file is never modified.
    bool enableRenderingCompatibility();
    quint64 renderingCompatibilityRva() const { return m_renderingCompatibilityRva; }

    // Used while reconstructing and installing the remaining host callbacks.
    // The storage stays valid until unload().
    QByteArray& hostBlock() { return m_hostBlock; }
    QByteArray& apiBlock() { return m_apiBlock; }
    const QByteArray& hostBlock() const { return m_hostBlock; }
    const QByteArray& apiBlock() const { return m_apiBlock; }

    // Diagnostics and ABI probes sometimes need to correlate live global
    // objects with RVAs recovered from the PE image.  The application renderer
    // does not dereference this directly.
    const void* moduleBase() const { return m_module; }

private:
    void* m_module = nullptr;
    void* m_notify = nullptr;
    void* m_dllDirectoryCookie = nullptr;
    QString m_filePath;
    QString m_error;
    QByteArray m_hostBlock;
    QByteArray m_apiBlock;
    int m_loadResult = -1;
    int m_unloadResult = -1;
    unsigned long m_faultCode = 0;
    quint64 m_faultInstructionRva = 0;
    QString m_faultModulePath;
    quint64 m_faultAccessAddress = 0;
    quint64 m_renderingCompatibilityRva = 0;
};

struct NativePluginLifecycleProbe
{
    bool loaded = false;
    int loadResult = -1;
    int unloadResult = -1;
    unsigned long faultCode = 0;
    quint64 faultInstructionRva = 0;
    quint64 faultAccessAddress = 0;
    QString error;
};

NativePluginLifecycleProbe probeNativePluginLifecycle(
    const QString& filePath, const QString& dependencySearchPath = QString());

// Diagnostic instrumentation for ABI recovery. Empty pointer-sized service
// slots from +0x30 onward are replaced with harmless zero-returning stubs that
// remember which offset was invoked. This is used only by hfpl_runtime_probe;
// production instances install concrete services instead.
void installNativePluginDiagnosticStubs(QByteArray& apiBlock,
                                        int firstOffset = 0x30,
                                        int lastOffset = -1,
                                        bool installLegacyTable = true);
void resetNativePluginDiagnosticServiceOffset();
int lastNativePluginDiagnosticServiceOffset();
QString nativePluginDiagnosticTrace();

// True when the environment asks for static inspection only.
bool nativePluginLoadingDisabled();

// Metadata round-trip for the scan cache. Loading a module costs about 60 ms -
// most of it the module's own DllMain - so 321 of them is nearly twenty seconds
// of startup. The reference solves this by scanning on a background thread;
// this port remembers what it read instead, keyed on the file's size and
// modification time, so the cost is paid once per plugin rather than once per
// launch.
QJsonObject nativePluginToJson(const NativePluginInfo& info);
NativePluginInfo nativePluginFromJson(const QJsonObject& object);

// Reference remap of PluginMetadata's type field onto a wrapper class.
PluginKind kindForModuleType(int type);

// Splits the trailing segment of an identifier into words for display:
// "com.FXHOME.HitFilm.HighpassSharpen" -> "Highpass Sharpen". Acronyms are kept
// together ("HSL" stays "HSL"). Falls back to `fallback` for an empty id.
QString displayNameForIdentifier(const QString& identifier, const QString& fallback);

// File name filters used by the reference plugin scanner.
QStringList nativePluginNameFilters();

// Reads `filePath` and reports which of the two required entry points it
// exports. Returns a record with both flags false when the file is not a
// readable PE image.
NativePluginInfo probeNativePlugin(const QString& filePath);

// Maps a reference "Plugins/<folder>" name onto our PluginKind.
PluginKind kindForNativeCategory(const QString& folderName);

} // namespace plugin
} // namespace openvegas
