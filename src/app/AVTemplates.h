#pragma once

#include <QString>
#include <QVector>

namespace openvegas {
namespace app {

// A composite shot format template - the reference's AVTemplate (TemplateManager,
// biff::ui::common): a size, a frame rate, a pixel aspect and a sample rate a
// shot can be set to from Composite Shot Properties' "Template:".
//
// The reference keeps each in a "<GUID>.hft" file under its templates folder
// (registry Paths\Templates + "\AV", DataLocation_Resolve_Folder(1)), as
//   <Templates><Template Version="0"><SystemTemplate/><ID/><Name/><Width/>
//   <Height/><FrameRate/><PAR/><PARValue/><AudioSampleRate/>[<Duration/>]
//   </Template></Templates>
// (written by FUN_14044ff70 / FUN_1404507a0, read by FUN_14044f770 and
// FUN_1404503d0). Its system templates come with its installer, which this
// port does not have: the built-in ones below are the port's own list of
// common formats. User templates are read from and saved to userFolder() in
// the reference's format.
struct AVTemplate
{
    QString id;
    QString name;
    bool system = false;
    int width = 1920;
    int height = 1080;
    double frameRate = 30.0;
    int pixelAspect = 0;          // Composition::PixelAspect
    double pixelAspectValue = 1.0;
    int audioSampleRate = 48000;
    QString filePath;             // empty for a built-in one
};

// The port's built-in formats; "fullhd30", "fullhd60" and "uhd30" are the
// ids Options' Default Template has always stored.
QVector<AVTemplate> builtInTemplates();
// Where user templates live (overridable for tests).
QString userTemplateFolder();
void setUserTemplateFolderOverride(const QString& folder);
// Templates of every *.hft file in `folder`.
QVector<AVTemplate> loadTemplates(const QString& folder);
// Built-in ones first, then the user's.
QVector<AVTemplate> allTemplates();
AVTemplate templateById(const QString& id);
// Writes `t` as "<id>.hft" in the user folder (a new id when it has none);
// returns the template as stored, or one without an id on failure.
AVTemplate saveUserTemplate(AVTemplate t);
bool deleteUserTemplate(const AVTemplate& t);

} // namespace app
} // namespace openvegas
