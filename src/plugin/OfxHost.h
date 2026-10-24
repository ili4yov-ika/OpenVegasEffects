#pragma once

#include "core/Identifier.h"
#include "plugin/EffectSpec.h"
#include "plugin/NativeEffectRender.h"

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

// An OpenFX image effect host, as the reference's Tannen has one
// (OFXGlobalHost, OFXPlugin2DEffectWrapper / OFXPluginTransitionWrapper):
// plug-ins are found in the OpenFX folders (OFX_PLUGIN_PATH, Common
// Files\OFX\Plugins), described once - in a separate process when a sandbox
// executable is set, as the reference's VegasEffectsOFXSandbox does - and
// remembered in a cache, then appear as effects (filter, general and
// generator contexts) and transitions (transition context). They render on
// the CPU, 8-bit or float RGBA, premultiplied, origin bottom left.

namespace openvegas {
namespace plugin {

// One image effect of an .ofx binary, as describe and describeInContext
// tell it.
struct OfxPluginRecord
{
    QString binaryPath;
    int index = 0;                    // its place in OfxGetPlugin
    QString identifier;               // reverse domain name
    int versionMajor = 1;
    int versionMinor = 0;
    QString label;
    QString grouping;                 // "Blur/Gaussian"
    QString description;
    QStringList contexts;             // the OpenFX contexts it supports
    QString context;                  // the one this host instantiates for an effect
    bool transition = false;          // also usable as a transition
    QVector<EffectParameterSpec> parameters;
    QStringList ofxTypes;             // each parameter's OpenFX type
    QStringList normalisedDefaults;   // "1" where a default is in normalised coordinates
    QString error;                    // why it cannot be used, when it cannot

    bool isValid() const { return !identifier.isEmpty() && error.isEmpty(); }
};

// The FXID the reference gives an OpenFX plug-in (Tannen FUN_1802ff1a0): a
// SHA-1 of "<identifier> <major version> <1 effect | 0 transition>" laid out
// as a GUID with its version 5 and variant bits.
QString ofxPluginGuid(const QString& identifier, int versionMajor, bool transition);

// The folders searched for plug-ins: OFX_PLUGIN_PATH, then the platform's
// standard one (C:\Program Files\Common Files\OFX\Plugins on Windows).
QStringList ofxPluginSearchPaths();
// The plug-in binaries of the bundles in those folders:
// <name>.ofx.bundle/Contents/<Win64|MacOS|Linux-x86-64>/<name>.ofx.
QStringList findOfxBinaries(const QStringList& roots);

// Loads a binary and describes every image effect in it, in this process.
QVector<OfxPluginRecord> describeOfxBinary(const QString& binaryPath, QString* error = nullptr);
// The same in a child process when a sandbox executable is set (one that
// handles --ofx-describe), so a plug-in that crashes cannot take the
// application with it; in this process otherwise.
QVector<OfxPluginRecord> describeOfxBinarySafely(const QString& binaryPath, QString* error = nullptr);
void setOfxSandboxExecutable(const QString& executable);
// The sandbox side: describes `binaryPath` and writes the records as JSON to
// `outputPath`. Returns the process exit code.
int runOfxDescribeSandbox(const QString& binaryPath, const QString& outputPath);

// Describes binaries through the plug-in cache (the reference keeps one in
// "FXhome/VEGAS Effects/ofxPluginCache"): a binary whose size and time are
// those cached is not loaded again; the others go through the sandbox.
QVector<OfxPluginRecord> describeOfxBinariesCached(const QStringList& binaryPaths);
QString ofxPluginCachePath();
bool deleteOfxPluginCache();

QByteArray ofxRecordsToJson(const QVector<OfxPluginRecord>& records);
QVector<OfxPluginRecord> ofxRecordsFromJson(const QByteArray& json);

// The effect spec of a plug-in, as an effect or as a transition.
EffectSpec ofxEffectSpec(const OfxPluginRecord& record, bool transition);
// The id effects carry for a plug-in: "ofx:<identifier>", or
// "ofx-transition:<identifier>".
core::Identifier ofxEffectId(const QString& identifier, bool transition);

// Plug-ins the renderer can run, by their effect id.
void registerOfxPlugin(const core::Identifier& id, const OfxPluginRecord& record, bool transition);
void clearOfxPlugins();
bool isOfxPlugin(const core::Identifier& id);

// Renders a plug-in over an RGBA8888 image (straight alpha), with the
// effect's values in the order of its spec's parameters.
bool applyOfxEffectToImage(QImage& image, const core::Identifier& id, const QStringList& values,
                           const NativeFrameTime& time = {});
// A transition from `from` to `to` at `progress` (0..1) into `output`.
bool applyOfxTransition(QImage& output, const QImage& from, const QImage& to, double progress,
                        const core::Identifier& id, const QStringList& values, const NativeFrameTime& time = {});

// The messages plug-ins posted (kOfxMessageError and the like), for tests
// and the log.
QStringList ofxMessages();

} // namespace plugin
} // namespace openvegas
