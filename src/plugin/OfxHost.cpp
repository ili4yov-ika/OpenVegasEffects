#include "plugin/OfxHost.h"

#include "core/Log.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRecursiveMutex>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "ofxCore.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxOld.h"
#include "ofxParam.h"
#include "ofxParametricParam.h"
#include "ofxProgress.h"
#include "ofxProperty.h"
#include "ofxTimeLine.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// The host side of OpenFX 1.4 for image effects. Plug-ins see the suites
// below; every handle they get is a pointer to one of this file's structures
// (a property set, an effect, a parameter set, a parameter, a clip). One
// recursive mutex serialises all plug-in calls: rendering takes it for the
// whole render, which keeps plug-ins that are not thread safe safe.

namespace openvegas {
namespace plugin {

namespace {

// ------------------------------------------------------------- properties

struct PropertyValue
{
    enum Kind { Pointer, String, Double, Int };
    Kind kind = Int;
    std::vector<void*> pointers;
    std::vector<QByteArray> strings;
    std::vector<double> doubles;
    std::vector<int> ints;

    int size() const
    {
        switch (kind) {
        case Pointer: return int(pointers.size());
        case String: return int(strings.size());
        case Double: return int(doubles.size());
        default: return int(ints.size());
        }
    }
};

struct PropertySet
{
    std::map<QByteArray, PropertyValue> values;

    PropertyValue* find(const char* name)
    {
        const auto it = values.find(QByteArray(name));
        return it == values.end() ? nullptr : &it->second;
    }
    // Defines a property of a kind with values (the host's own defaults).
    void setStrings(const char* name, const std::vector<QByteArray>& v)
    {
        PropertyValue& p = values[QByteArray(name)];
        p.kind = PropertyValue::String; p.strings = v;
    }
    void setString(const char* name, const QByteArray& v) { setStrings(name, {v}); }
    void setDoubles(const char* name, const std::vector<double>& v)
    {
        PropertyValue& p = values[QByteArray(name)];
        p.kind = PropertyValue::Double; p.doubles = v;
    }
    void setDouble(const char* name, double v) { setDoubles(name, {v}); }
    void setInts(const char* name, const std::vector<int>& v)
    {
        PropertyValue& p = values[QByteArray(name)];
        p.kind = PropertyValue::Int; p.ints = v;
    }
    void setInt(const char* name, int v) { setInts(name, {v}); }
    void setPointer(const char* name, void* v)
    {
        PropertyValue& p = values[QByteArray(name)];
        p.kind = PropertyValue::Pointer; p.pointers = {v};
    }
    QByteArray string(const char* name, int index = 0, const QByteArray& fallback = {})
    {
        PropertyValue* p = find(name);
        return p && p->kind == PropertyValue::String && index < int(p->strings.size()) ? p->strings[size_t(index)]
                                                                                       : fallback;
    }
    QList<QByteArray> stringList(const char* name)
    {
        QList<QByteArray> out;
        if (PropertyValue* p = find(name); p && p->kind == PropertyValue::String)
            for (const QByteArray& s : p->strings) out.append(s);
        return out;
    }
    double number(const char* name, int index = 0, double fallback = 0.0)
    {
        PropertyValue* p = find(name);
        if (!p) return fallback;
        if (p->kind == PropertyValue::Double && index < int(p->doubles.size())) return p->doubles[size_t(index)];
        if (p->kind == PropertyValue::Int && index < int(p->ints.size())) return p->ints[size_t(index)];
        return fallback;
    }
    void* pointer(const char* name)
    {
        PropertyValue* p = find(name);
        return p && p->kind == PropertyValue::Pointer && !p->pointers.empty() ? p->pointers[0] : nullptr;
    }
};

PropertySet* props(OfxPropertySetHandle handle) { return reinterpret_cast<PropertySet*>(handle); }
OfxPropertySetHandle handleOf(PropertySet* set) { return reinterpret_cast<OfxPropertySetHandle>(set); }

// Values are resized as plug-ins set them by index; a property the host does
// not know is created on first set (plug-ins set their own, and the
// specification allows it for descriptors).
template <typename T>
OfxStatus setAt(OfxPropertySetHandle set, const char* name, int index, T value, PropertyValue::Kind kind,
                std::vector<T> PropertyValue::*member)
{
    if (!set || !name || index < 0) return kOfxStatErrBadHandle;
    PropertyValue& p = props(set)->values[QByteArray(name)];
    if (p.size() == 0) p.kind = kind;
    if (p.kind != kind) {
        // An int written to a double property and the other way round are
        // the same number.
        if (kind == PropertyValue::Int && p.kind == PropertyValue::Double) {
            if (int(p.doubles.size()) <= index) p.doubles.resize(size_t(index) + 1);
            p.doubles[size_t(index)] = double(*reinterpret_cast<int*>(&value));
            return kOfxStatOK;
        }
        if (kind == PropertyValue::Double && p.kind == PropertyValue::Int) {
            if (int(p.ints.size()) <= index) p.ints.resize(size_t(index) + 1);
            p.ints[size_t(index)] = int(*reinterpret_cast<double*>(&value));
            return kOfxStatOK;
        }
        return kOfxStatErrValue;
    }
    std::vector<T>& values = p.*member;
    if (int(values.size()) <= index) values.resize(size_t(index) + 1);
    values[size_t(index)] = value;
    return kOfxStatOK;
}

OfxStatus propSetPointer(OfxPropertySetHandle s, const char* n, int i, void* v)
{ return setAt<void*>(s, n, i, v, PropertyValue::Pointer, &PropertyValue::pointers); }
OfxStatus propSetString(OfxPropertySetHandle s, const char* n, int i, const char* v)
{ return setAt<QByteArray>(s, n, i, QByteArray(v ? v : ""), PropertyValue::String, &PropertyValue::strings); }
OfxStatus propSetDouble(OfxPropertySetHandle s, const char* n, int i, double v)
{ return setAt<double>(s, n, i, v, PropertyValue::Double, &PropertyValue::doubles); }
OfxStatus propSetInt(OfxPropertySetHandle s, const char* n, int i, int v)
{ return setAt<int>(s, n, i, v, PropertyValue::Int, &PropertyValue::ints); }
OfxStatus propSetPointerN(OfxPropertySetHandle s, const char* n, int c, void* const* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propSetPointer(s, n, i, v[i])) return r; return kOfxStatOK; }
OfxStatus propSetStringN(OfxPropertySetHandle s, const char* n, int c, const char* const* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propSetString(s, n, i, v[i])) return r; return kOfxStatOK; }
OfxStatus propSetDoubleN(OfxPropertySetHandle s, const char* n, int c, const double* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propSetDouble(s, n, i, v[i])) return r; return kOfxStatOK; }
OfxStatus propSetIntN(OfxPropertySetHandle s, const char* n, int c, const int* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propSetInt(s, n, i, v[i])) return r; return kOfxStatOK; }

PropertyValue* readable(OfxPropertySetHandle s, const char* n, int index, OfxStatus* status)
{
    if (!s || !n) { *status = kOfxStatErrBadHandle; return nullptr; }
    PropertyValue* p = props(s)->find(n);
    if (!p) { *status = kOfxStatErrUnknown; return nullptr; }
    if (index < 0 || index >= p->size()) { *status = kOfxStatErrBadIndex; return nullptr; }
    *status = kOfxStatOK;
    return p;
}

OfxStatus propGetPointer(OfxPropertySetHandle s, const char* n, int i, void** v)
{
    OfxStatus st; PropertyValue* p = readable(s, n, i, &st);
    if (!p) return st;
    if (p->kind != PropertyValue::Pointer) return kOfxStatErrValue;
    *v = p->pointers[size_t(i)];
    return kOfxStatOK;
}
OfxStatus propGetString(OfxPropertySetHandle s, const char* n, int i, char** v)
{
    OfxStatus st; PropertyValue* p = readable(s, n, i, &st);
    if (!p) return st;
    if (p->kind != PropertyValue::String) return kOfxStatErrValue;
    *v = const_cast<char*>(p->strings[size_t(i)].constData());
    return kOfxStatOK;
}
OfxStatus propGetDouble(OfxPropertySetHandle s, const char* n, int i, double* v)
{
    OfxStatus st; PropertyValue* p = readable(s, n, i, &st);
    if (!p) return st;
    if (p->kind == PropertyValue::Double) *v = p->doubles[size_t(i)];
    else if (p->kind == PropertyValue::Int) *v = p->ints[size_t(i)];
    else return kOfxStatErrValue;
    return kOfxStatOK;
}
OfxStatus propGetInt(OfxPropertySetHandle s, const char* n, int i, int* v)
{
    OfxStatus st; PropertyValue* p = readable(s, n, i, &st);
    if (!p) return st;
    if (p->kind == PropertyValue::Int) *v = p->ints[size_t(i)];
    else if (p->kind == PropertyValue::Double) *v = int(p->doubles[size_t(i)]);
    else return kOfxStatErrValue;
    return kOfxStatOK;
}
OfxStatus propGetPointerN(OfxPropertySetHandle s, const char* n, int c, void** v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propGetPointer(s, n, i, v + i)) return r; return kOfxStatOK; }
OfxStatus propGetStringN(OfxPropertySetHandle s, const char* n, int c, char** v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propGetString(s, n, i, v + i)) return r; return kOfxStatOK; }
OfxStatus propGetDoubleN(OfxPropertySetHandle s, const char* n, int c, double* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propGetDouble(s, n, i, v + i)) return r; return kOfxStatOK; }
OfxStatus propGetIntN(OfxPropertySetHandle s, const char* n, int c, int* v)
{ for (int i = 0; i < c; ++i) if (OfxStatus r = propGetInt(s, n, i, v + i)) return r; return kOfxStatOK; }
OfxStatus propReset(OfxPropertySetHandle s, const char* n)
{
    if (!s || !n) return kOfxStatErrBadHandle;
    return props(s)->find(n) ? kOfxStatOK : kOfxStatErrUnknown;
}
OfxStatus propGetDimension(OfxPropertySetHandle s, const char* n, int* count)
{
    if (!s || !n) return kOfxStatErrBadHandle;
    PropertyValue* p = props(s)->find(n);
    if (!p) return kOfxStatErrUnknown;
    *count = p->size();
    return kOfxStatOK;
}

const OfxPropertySuiteV1 kPropertySuite = {
    propSetPointer, propSetString, propSetDouble, propSetInt,
    propSetPointerN, propSetStringN, propSetDoubleN, propSetIntN,
    propGetPointer, propGetString, propGetDouble, propGetInt,
    propGetPointerN, propGetStringN, propGetDoubleN, propGetIntN,
    propReset, propGetDimension,
};

// --------------------------------------------- effects, clips, parameters

struct Param
{
    QByteArray name;
    QByteArray type;
    PropertySet props;
    std::vector<double> numbers;    // ints are kept as doubles
    QByteArray text;                // string and custom parameters

    int dimension() const
    {
        if (type == kOfxParamTypeRGBA) return 4;
        if (type == kOfxParamTypeRGB || type == kOfxParamTypeDouble3D || type == kOfxParamTypeInteger3D) return 3;
        if (type == kOfxParamTypeDouble2D || type == kOfxParamTypeInteger2D) return 2;
        if (type == kOfxParamTypeGroup || type == kOfxParamTypePage || type == kOfxParamTypePushButton
            || type == kOfxParamTypeString || type == kOfxParamTypeCustom || type == kOfxParamTypeParametric)
            return 0;
        return 1;
    }
    bool integral() const
    {
        return type == kOfxParamTypeInteger || type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice
               || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D;
    }
    bool textual() const { return type == kOfxParamTypeString || type == kOfxParamTypeCustom; }
    // The values as the descriptor's defaults give them.
    void resetToDefault()
    {
        numbers.assign(size_t(dimension()), 0.0);
        for (int i = 0; i < dimension(); ++i) numbers[size_t(i)] = props.number(kOfxParamPropDefault, i, 0.0);
        if (textual()) text = props.string(kOfxParamPropDefault);
    }
};

struct ParamSet
{
    PropertySet props;
    std::vector<std::unique_ptr<Param>> params;

    Param* find(const char* name)
    {
        for (auto& p : params) if (p->name == name) return p.get();
        return nullptr;
    }
};

struct Effect;

// One image an effect reads or writes during a render: its pixels, bottom
// row first, and the property set plug-ins see.
struct Image
{
    PropertySet props;
    QByteArray pixels;
};

struct Clip
{
    QByteArray name;
    PropertySet props;
    Effect* effect = nullptr;
    // During a render: the picture this clip delivers (an input) or the
    // buffer it receives (the output).
    QImage input;
    std::shared_ptr<Image> output;
};

struct PluginRuntime;

struct Effect
{
    PropertySet props;
    ParamSet params;
    std::vector<std::unique_ptr<Clip>> clips;
    PluginRuntime* runtime = nullptr;
    bool instance = false;
    QByteArray context;
    double time = 0.0;
    double duration = 1.0;
    QByteArray depth = kOfxBitDepthByte;
    std::vector<std::shared_ptr<Image>> images;   // handed out by clipGetImage

    Clip* clip(const char* name)
    {
        for (auto& c : clips) if (c->name == name) return c.get();
        return nullptr;
    }
};

Effect* effectOf(OfxImageEffectHandle h) { return reinterpret_cast<Effect*>(h); }
Clip* clipOf(OfxImageClipHandle h) { return reinterpret_cast<Clip*>(h); }
ParamSet* paramSetOf(OfxParamSetHandle h) { return reinterpret_cast<ParamSet*>(h); }
Param* paramOf(OfxParamHandle h) { return reinterpret_cast<Param*>(h); }

// The defaults a parameter descriptor starts with, for its type.
void defineParamDefaults(Param& p)
{
    PropertySet& s = p.props;
    s.setString(kOfxPropType, kOfxTypeParameter);
    s.setString(kOfxPropName, p.name);
    s.setString(kOfxPropLabel, p.name);
    s.setString(kOfxPropShortLabel, p.name);
    s.setString(kOfxPropLongLabel, p.name);
    s.setString(kOfxParamPropType, p.type);
    s.setInt(kOfxParamPropSecret, 0);
    s.setString(kOfxParamPropHint, "");
    s.setString(kOfxParamPropScriptName, p.name);
    s.setString(kOfxParamPropParent, "");
    s.setInt(kOfxParamPropEnabled, 1);
    s.setPointer(kOfxParamPropDataPtr, nullptr);
    s.setInt(kOfxParamPropCanUndo, 1);
    s.setInt(kOfxParamPropEvaluateOnChange, 1);
    s.setInt(kOfxParamPropPersistant, 1);
    s.setString(kOfxParamPropCacheInvalidation, kOfxParamInvalidateValueChange);
    s.setInt(kOfxParamPropAnimates, p.textual() || p.type == kOfxParamTypeBoolean
                                            || p.type == kOfxParamTypeChoice ? 0 : 1);
    s.setInt(kOfxParamPropIsAnimating, 0);
    s.setInt(kOfxParamPropIsAutoKeying, 0);
    const int dimension = p.dimension();
    if (p.type == kOfxParamTypeGroup) {
        s.setInt(kOfxParamPropGroupOpen, 1);
    } else if (p.type == kOfxParamTypePage) {
        s.setStrings(kOfxParamPropPageChild, {});
    } else if (p.textual()) {
        s.setString(kOfxParamPropDefault, "");
        s.setString(kOfxParamPropStringMode, kOfxParamStringIsSingleLine);
        s.setInt(kOfxParamPropStringFilePathExists, 1);
    } else if (dimension > 0) {
        if (p.integral()) {
            s.setInts(kOfxParamPropDefault, std::vector<int>(size_t(dimension), 0));
            s.setInts(kOfxParamPropMin, std::vector<int>(size_t(dimension), INT_MIN));
            s.setInts(kOfxParamPropMax, std::vector<int>(size_t(dimension), INT_MAX));
            s.setInts(kOfxParamPropDisplayMin, std::vector<int>(size_t(dimension), INT_MIN));
            s.setInts(kOfxParamPropDisplayMax, std::vector<int>(size_t(dimension), INT_MAX));
            if (p.type == kOfxParamTypeChoice) s.setStrings(kOfxParamPropChoiceOption, {});
        } else {
            const bool colour = p.type == kOfxParamTypeRGBA || p.type == kOfxParamTypeRGB;
            s.setDoubles(kOfxParamPropDefault, std::vector<double>(size_t(dimension), 0.0));
            s.setDoubles(kOfxParamPropMin, std::vector<double>(size_t(dimension), colour ? 0.0 : -DBL_MAX));
            s.setDoubles(kOfxParamPropMax, std::vector<double>(size_t(dimension), colour ? 1.0 : DBL_MAX));
            s.setDoubles(kOfxParamPropDisplayMin, std::vector<double>(size_t(dimension), colour ? 0.0 : -DBL_MAX));
            s.setDoubles(kOfxParamPropDisplayMax, std::vector<double>(size_t(dimension), colour ? 1.0 : DBL_MAX));
            s.setDouble(kOfxParamPropIncrement, 1.0);
            s.setInt(kOfxParamPropDigits, 2);
            s.setString(kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
            s.setString(kOfxParamPropDefaultCoordinateSystem, kOfxParamCoordinatesCanonical);
            if (dimension > 1) {
                const char* labels[] = {"x", "y", "z", "w"};
                const char* colours[] = {"r", "g", "b", "a"};
                std::vector<QByteArray> names;
                for (int i = 0; i < dimension; ++i) names.push_back(colour ? colours[i] : labels[i]);
                s.setStrings(kOfxParamPropDimensionLabel, names);
            }
        }
    }
}

// --------------------------------------------------------- image effect suite

OfxStatus getPropertySet(OfxImageEffectHandle e, OfxPropertySetHandle* out)
{
    if (!e) return kOfxStatErrBadHandle;
    *out = handleOf(&effectOf(e)->props);
    return kOfxStatOK;
}
OfxStatus getParamSet(OfxImageEffectHandle e, OfxParamSetHandle* out)
{
    if (!e) return kOfxStatErrBadHandle;
    *out = reinterpret_cast<OfxParamSetHandle>(&effectOf(e)->params);
    return kOfxStatOK;
}
OfxStatus clipDefine(OfxImageEffectHandle e, const char* name, OfxPropertySetHandle* out)
{
    if (!e || !name) return kOfxStatErrBadHandle;
    Effect* effect = effectOf(e);
    Clip* clip = effect->clip(name);
    if (!clip) {
        effect->clips.push_back(std::make_unique<Clip>());
        clip = effect->clips.back().get();
        clip->name = name;
        clip->effect = effect;
        PropertySet& s = clip->props;
        s.setString(kOfxPropType, kOfxTypeClip);
        s.setString(kOfxPropName, name);
        s.setString(kOfxPropLabel, name);
        s.setStrings(kOfxImageEffectPropSupportedComponents, {});
        s.setInt(kOfxImageEffectPropTemporalClipAccess, 0);
        s.setInt(kOfxImageClipPropOptional, 0);
        s.setString(kOfxImageClipPropFieldExtraction, kOfxImageFieldDoubled);
        s.setInt(kOfxImageClipPropIsMask, 0);
        s.setInt(kOfxImageEffectPropSupportsTiles, 1);
    }
    if (out) *out = handleOf(&clip->props);
    return kOfxStatOK;
}
OfxStatus clipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* out)
{
    if (!e || !name) return kOfxStatErrBadHandle;
    Clip* c = effectOf(e)->clip(name);
    if (!c) return kOfxStatErrBadHandle;
    if (clip) *clip = reinterpret_cast<OfxImageClipHandle>(c);
    if (out) *out = handleOf(&c->props);
    return kOfxStatOK;
}
OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* out)
{
    if (!clip) return kOfxStatErrBadHandle;
    *out = handleOf(&clipOf(clip)->props);
    return kOfxStatOK;
}

int bytesPerComponent(const QByteArray& depth)
{
    if (depth == kOfxBitDepthFloat) return 4;
    if (depth == kOfxBitDepthShort) return 2;
    return 1;
}

// A straight-alpha RGBA8888 picture as premultiplied pixels of a depth,
// bottom row first.
QByteArray toOfxPixels(const QImage& image, const QByteArray& depth)
{
    const int w = image.width(), h = image.height(), bpc = bytesPerComponent(depth);
    QByteArray out(qsizetype(w) * h * 4 * bpc, Qt::Uninitialized);
    for (int y = 0; y < h; ++y) {
        const uchar* src = image.constScanLine(h - 1 - y);
        char* row = out.data() + qsizetype(y) * w * 4 * bpc;
        for (int x = 0; x < w; ++x) {
            const double a = src[x * 4 + 3] / 255.0;
            for (int c = 0; c < 4; ++c) {
                const double v = c == 3 ? a : src[x * 4 + c] / 255.0 * a;
                if (bpc == 1) reinterpret_cast<uchar*>(row)[x * 4 + c] = uchar(std::lround(v * 255.0));
                else if (bpc == 2) reinterpret_cast<quint16*>(row)[x * 4 + c] = quint16(std::lround(v * 65535.0));
                else reinterpret_cast<float*>(row)[x * 4 + c] = float(v);
            }
        }
    }
    return out;
}

// The other way: premultiplied pixels of a depth back to straight RGBA8888.
QImage fromOfxPixels(const QByteArray& pixels, int w, int h, const QByteArray& depth)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    const int bpc = bytesPerComponent(depth);
    if (pixels.size() < qsizetype(w) * h * 4 * bpc) return {};
    for (int y = 0; y < h; ++y) {
        const char* row = pixels.constData() + qsizetype(y) * w * 4 * bpc;
        uchar* dst = image.scanLine(h - 1 - y);
        for (int x = 0; x < w; ++x) {
            double v[4];
            for (int c = 0; c < 4; ++c) {
                if (bpc == 1) v[c] = reinterpret_cast<const uchar*>(row)[x * 4 + c] / 255.0;
                else if (bpc == 2) v[c] = reinterpret_cast<const quint16*>(row)[x * 4 + c] / 65535.0;
                else v[c] = reinterpret_cast<const float*>(row)[x * 4 + c];
                if (!std::isfinite(v[c])) v[c] = 0.0;
                v[c] = qBound(0.0, v[c], 1.0);
            }
            for (int c = 0; c < 3; ++c)
                dst[x * 4 + c] = uchar(std::lround((v[3] > 0.0 ? qMin(1.0, v[c] / v[3]) : 0.0) * 255.0));
            dst[x * 4 + 3] = uchar(std::lround(v[3] * 255.0));
        }
    }
    return image;
}

void describeImage(Image& image, int w, int h, const QByteArray& depth, const QByteArray& id)
{
    PropertySet& s = image.props;
    s.setString(kOfxPropType, kOfxTypeImage);
    s.setString(kOfxImageEffectPropPixelDepth, depth);
    s.setString(kOfxImageEffectPropComponents, kOfxImageComponentRGBA);
    s.setString(kOfxImageEffectPropPreMultiplication, kOfxImagePreMultiplied);
    s.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
    s.setDouble(kOfxImagePropPixelAspectRatio, 1.0);
    s.setPointer(kOfxImagePropData, image.pixels.data());
    s.setInts(kOfxImagePropBounds, {0, 0, w, h});
    s.setInts(kOfxImagePropRegionOfDefinition, {0, 0, w, h});
    s.setInt(kOfxImagePropRowBytes, w * 4 * bytesPerComponent(depth));
    s.setString(kOfxImagePropField, kOfxImageFieldNone);
    s.setString(kOfxImagePropUniqueIdentifier, id);
}

OfxStatus clipGetImage(OfxImageClipHandle handle, OfxTime, const OfxRectD*, OfxPropertySetHandle* out)
{
    if (!handle || !out) return kOfxStatErrBadHandle;
    Clip* clip = clipOf(handle);
    Effect* effect = clip->effect;
    if (clip->output) {
        *out = handleOf(&clip->output->props);
        return kOfxStatOK;
    }
    if (clip->input.isNull()) return kOfxStatFailed;
    auto image = std::make_shared<Image>();
    image->pixels = toOfxPixels(clip->input, effect->depth);
    describeImage(*image, clip->input.width(), clip->input.height(), effect->depth,
                  clip->name + QByteArray::number(effect->time));
    effect->images.push_back(image);
    *out = handleOf(&image->props);
    return kOfxStatOK;
}
OfxStatus clipReleaseImage(OfxPropertySetHandle)
{
    // Images live until the render ends.
    return kOfxStatOK;
}
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle handle, OfxTime, OfxRectD* bounds)
{
    if (!handle || !bounds) return kOfxStatErrBadHandle;
    Clip* clip = clipOf(handle);
    const QSize size = !clip->input.isNull() ? clip->input.size()
        : QSize(int(clip->effect->props.number(kOfxImageEffectPropProjectSize, 0, 0)),
                int(clip->effect->props.number(kOfxImageEffectPropProjectSize, 1, 0)));
    *bounds = {0.0, 0.0, double(size.width()), double(size.height())};
    return kOfxStatOK;
}
int abortEffect(OfxImageEffectHandle) { return 0; }

struct Memory { QByteArray bytes; int locks = 0; };
OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t bytes, OfxImageMemoryHandle* out)
{
    if (!out) return kOfxStatErrBadHandle;
    auto* memory = new Memory;
    memory->bytes = QByteArray(qsizetype(bytes), '\0');
    *out = reinterpret_cast<OfxImageMemoryHandle>(memory);
    return kOfxStatOK;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle handle)
{
    delete reinterpret_cast<Memory*>(handle);
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle handle, void** out)
{
    if (!handle || !out) return kOfxStatErrBadHandle;
    auto* memory = reinterpret_cast<Memory*>(handle);
    ++memory->locks;
    *out = memory->bytes.data();
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle handle)
{
    if (!handle) return kOfxStatErrBadHandle;
    --reinterpret_cast<Memory*>(handle)->locks;
    return kOfxStatOK;
}

const OfxImageEffectSuiteV1 kImageEffectSuite = {
    getPropertySet, getParamSet, clipDefine, clipGetHandle, clipGetPropertySet,
    clipGetImage, clipReleaseImage, clipGetRegionOfDefinition, abortEffect,
    imageMemoryAlloc, imageMemoryFree, imageMemoryLock, imageMemoryUnlock,
};

// ------------------------------------------------------------ parameter suite

OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name, OfxPropertySetHandle* out)
{
    if (!set || !type || !name) return kOfxStatErrBadHandle;
    ParamSet* params = paramSetOf(set);
    if (params->find(name)) return kOfxStatErrExists;
    auto param = std::make_unique<Param>();
    param->name = name;
    param->type = type;
    defineParamDefaults(*param);
    if (out) *out = handleOf(&param->props);
    params->params.push_back(std::move(param));
    return kOfxStatOK;
}
OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name, OfxParamHandle* param, OfxPropertySetHandle* out)
{
    if (!set || !name) return kOfxStatErrBadHandle;
    Param* p = paramSetOf(set)->find(name);
    if (!p) return kOfxStatErrUnknown;
    if (param) *param = reinterpret_cast<OfxParamHandle>(p);
    if (out) *out = handleOf(&p->props);
    return kOfxStatOK;
}
OfxStatus paramSetGetPropertySet(OfxParamSetHandle set, OfxPropertySetHandle* out)
{
    if (!set || !out) return kOfxStatErrBadHandle;
    *out = handleOf(&paramSetOf(set)->props);
    return kOfxStatOK;
}
OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* out)
{
    if (!param || !out) return kOfxStatErrBadHandle;
    *out = handleOf(&paramOf(param)->props);
    return kOfxStatOK;
}

// The value of a parameter into the pointers that follow in `args`.
OfxStatus getValues(Param* p, va_list args)
{
    if (!p) return kOfxStatErrBadHandle;
    if (p->textual()) {
        *va_arg(args, const char**) = p->text.constData();
        return kOfxStatOK;
    }
    const int dimension = p->dimension();
    if (dimension == 0) return kOfxStatErrUnsupported;
    for (int i = 0; i < dimension; ++i) {
        const double v = i < int(p->numbers.size()) ? p->numbers[size_t(i)] : 0.0;
        if (p->integral()) *va_arg(args, int*) = int(std::lround(v));
        else *va_arg(args, double*) = v;
    }
    return kOfxStatOK;
}
OfxStatus setValues(Param* p, va_list args)
{
    if (!p) return kOfxStatErrBadHandle;
    if (p->textual()) {
        const char* text = va_arg(args, const char*);
        p->text = text ? text : "";
        return kOfxStatOK;
    }
    const int dimension = p->dimension();
    if (dimension == 0) return kOfxStatOK;    // a push button
    p->numbers.resize(size_t(dimension));
    for (int i = 0; i < dimension; ++i)
        p->numbers[size_t(i)] = p->integral() ? double(va_arg(args, int)) : va_arg(args, double);
    return kOfxStatOK;
}

OfxStatus paramGetValue(OfxParamHandle h, ...)
{ va_list a; va_start(a, h); const OfxStatus s = getValues(paramOf(h), a); va_end(a); return s; }
OfxStatus paramGetValueAtTime(OfxParamHandle h, OfxTime t, ...)
{ va_list a; va_start(a, t); const OfxStatus s = getValues(paramOf(h), a); va_end(a); return s; }
OfxStatus paramGetDerivative(OfxParamHandle h, OfxTime t, ...)
{
    Param* p = paramOf(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list a; va_start(a, t);
    for (int i = 0; i < p->dimension(); ++i) *va_arg(a, double*) = 0.0;
    va_end(a);
    return kOfxStatOK;
}
OfxStatus paramGetIntegral(OfxParamHandle h, OfxTime t1, OfxTime t2, ...)
{
    Param* p = paramOf(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list a; va_start(a, t2);
    for (int i = 0; i < p->dimension(); ++i)
        *va_arg(a, double*) = (i < int(p->numbers.size()) ? p->numbers[size_t(i)] : 0.0) * (t2 - t1);
    va_end(a);
    return kOfxStatOK;
}
OfxStatus paramSetValue(OfxParamHandle h, ...)
{ va_list a; va_start(a, h); const OfxStatus s = setValues(paramOf(h), a); va_end(a); return s; }
OfxStatus paramSetValueAtTime(OfxParamHandle h, OfxTime t, ...)
{ va_list a; va_start(a, t); const OfxStatus s = setValues(paramOf(h), a); va_end(a); return s; }
OfxStatus paramGetNumKeys(OfxParamHandle h, unsigned int* count)
{ if (!h || !count) return kOfxStatErrBadHandle; *count = 0; return kOfxStatOK; }
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime*) { return kOfxStatErrBadIndex; }
OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int*) { return kOfxStatFailed; }
OfxStatus paramDeleteKey(OfxParamHandle, OfxTime) { return kOfxStatErrBadIndex; }
OfxStatus paramDeleteAllKeys(OfxParamHandle) { return kOfxStatOK; }
OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime, const OfxRangeD*)
{
    if (!to || !from) return kOfxStatErrBadHandle;
    paramOf(to)->numbers = paramOf(from)->numbers;
    paramOf(to)->text = paramOf(from)->text;
    return kOfxStatOK;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char*) { return kOfxStatOK; }
OfxStatus paramEditEnd(OfxParamSetHandle) { return kOfxStatOK; }

const OfxParameterSuiteV1 kParameterSuite = {
    paramDefine, paramGetHandle, paramSetGetPropertySet, paramGetPropertySet,
    paramGetValue, paramGetValueAtTime, paramGetDerivative, paramGetIntegral,
    paramSetValue, paramSetValueAtTime, paramGetNumKeys, paramGetKeyTime, paramGetKeyIndex,
    paramDeleteKey, paramDeleteAllKeys, paramCopy, paramEditBegin, paramEditEnd,
};

// --------------------------------------------------------- the other suites

OfxStatus memoryAlloc(void*, size_t bytes, void** out)
{
    if (!out) return kOfxStatErrBadHandle;
    *out = std::malloc(qMax<size_t>(1, bytes));
    return *out ? kOfxStatOK : kOfxStatErrMemory;
}
OfxStatus memoryFree(void* data) { std::free(data); return kOfxStatOK; }
const OfxMemorySuiteV1 kMemorySuite = {memoryAlloc, memoryFree};

thread_local unsigned int t_threadIndex = 0;
thread_local bool t_spawned = false;

OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int count, void* arg)
{
    if (!func) return kOfxStatErrBadHandle;
    if (count == 0) count = qMax(1u, std::thread::hardware_concurrency());
    if (count == 1) { func(0, 1, arg); return kOfxStatOK; }
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < count; ++i) {
        threads.emplace_back([func, i, count, arg] {
            t_threadIndex = i;
            t_spawned = true;
            func(i, count, arg);
        });
    }
    for (std::thread& t : threads) t.join();
    return kOfxStatOK;
}
OfxStatus multiThreadNumCPUs(unsigned int* n) { if (!n) return kOfxStatErrBadHandle; *n = qMax(1u, std::thread::hardware_concurrency()); return kOfxStatOK; }
OfxStatus multiThreadIndex(unsigned int* i) { if (!i) return kOfxStatErrBadHandle; *i = t_threadIndex; return kOfxStatOK; }
int multiThreadIsSpawnedThread() { return t_spawned ? 1 : 0; }
OfxStatus mutexCreate(OfxMutexHandle* m, int lockCount)
{
    if (!m) return kOfxStatErrBadHandle;
    auto* mutex = new QRecursiveMutex;
    for (int i = 0; i < lockCount; ++i) mutex->lock();
    *m = reinterpret_cast<OfxMutexHandle>(mutex);
    return kOfxStatOK;
}
OfxStatus mutexDestroy(const OfxMutexHandle m) { delete reinterpret_cast<QRecursiveMutex*>(m); return kOfxStatOK; }
OfxStatus mutexLock(const OfxMutexHandle m) { if (!m) return kOfxStatErrBadHandle; reinterpret_cast<QRecursiveMutex*>(m)->lock(); return kOfxStatOK; }
OfxStatus mutexUnLock(const OfxMutexHandle m) { if (!m) return kOfxStatErrBadHandle; reinterpret_cast<QRecursiveMutex*>(m)->unlock(); return kOfxStatOK; }
OfxStatus mutexTryLock(const OfxMutexHandle m)
{ if (!m) return kOfxStatErrBadHandle; return reinterpret_cast<QRecursiveMutex*>(m)->tryLock() ? kOfxStatOK : kOfxStatFailed; }
const OfxMultiThreadSuiteV1 kMultiThreadSuite = {
    multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
    mutexCreate, mutexDestroy, mutexLock, mutexUnLock, mutexTryLock,
};

QMutex g_messageMutex;
QStringList g_messages;

OfxStatus postMessage(const char* type, const char* format, va_list args)
{
    char buffer[2048];
    std::vsnprintf(buffer, sizeof buffer, format ? format : "", args);
    const QString text = QStringLiteral("%1: %2").arg(QString::fromLatin1(type ? type : ""), QString::fromUtf8(buffer));
    {
        QMutexLocker lock(&g_messageMutex);
        g_messages.append(text);
        while (g_messages.size() > 200) g_messages.removeFirst();
    }
    OV_LOG_INFO(QStringLiteral("OpenFX: ") + text);
    return type && std::strcmp(type, kOfxMessageQuestion) == 0 ? kOfxStatReplyDefault : kOfxStatOK;
}
OfxStatus message(void*, const char* type, const char*, const char* format, ...)
{ va_list a; va_start(a, format); const OfxStatus s = postMessage(type, format, a); va_end(a); return s; }
OfxStatus setPersistentMessage(void*, const char* type, const char*, const char* format, ...)
{ va_list a; va_start(a, format); const OfxStatus s = postMessage(type, format, a); va_end(a); return s; }
OfxStatus clearPersistentMessage(void*) { return kOfxStatOK; }
const OfxMessageSuiteV1 kMessageSuiteV1 = {message};
const OfxMessageSuiteV2 kMessageSuiteV2 = {message, setPersistentMessage, clearPersistentMessage};

OfxStatus progressStart1(void*, const char*) { return kOfxStatOK; }
OfxStatus progressStart2(void*, const char*, const char*) { return kOfxStatOK; }
OfxStatus progressUpdate(void*, double) { return kOfxStatOK; }
OfxStatus progressEnd(void*) { return kOfxStatOK; }
const OfxProgressSuiteV1 kProgressSuiteV1 = {progressStart1, progressUpdate, progressEnd};
const OfxProgressSuiteV2 kProgressSuiteV2 = {progressStart2, progressUpdate, progressEnd};

OfxStatus timeLineGetTime(void* instance, double* time)
{
    if (!instance || !time) return kOfxStatErrBadHandle;
    *time = reinterpret_cast<Effect*>(instance)->time;
    return kOfxStatOK;
}
OfxStatus timeLineGotoTime(void*, double) { return kOfxStatOK; }
OfxStatus timeLineGetTimeBounds(void* instance, double* first, double* last)
{
    if (!instance || !first || !last) return kOfxStatErrBadHandle;
    *first = 0.0;
    *last = reinterpret_cast<Effect*>(instance)->duration;
    return kOfxStatOK;
}
const OfxTimeLineSuiteV1 kTimeLineSuite = {timeLineGetTime, timeLineGotoTime, timeLineGetTimeBounds};

OfxStatus interactSwapBuffers(OfxInteractHandle) { return kOfxStatErrUnsupported; }
OfxStatus interactRedraw(OfxInteractHandle) { return kOfxStatErrUnsupported; }
OfxStatus interactGetPropertySet(OfxInteractHandle, OfxPropertySetHandle*) { return kOfxStatErrUnsupported; }
const OfxInteractSuiteV1 kInteractSuite = {interactSwapBuffers, interactRedraw, interactGetPropertySet};

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version)
{
    if (!name) return nullptr;
    const auto is = [name](const char* suite) { return std::strcmp(name, suite) == 0; };
    if (is(kOfxPropertySuite) && version == 1) return &kPropertySuite;
    if (is(kOfxImageEffectSuite) && version == 1) return &kImageEffectSuite;
    if (is(kOfxParameterSuite) && version == 1) return &kParameterSuite;
    if (is(kOfxMemorySuite) && version == 1) return &kMemorySuite;
    if (is(kOfxMultiThreadSuite) && version == 1) return &kMultiThreadSuite;
    if (is(kOfxMessageSuite) && version == 1) return &kMessageSuiteV1;
    if (is(kOfxMessageSuite) && version == 2) return &kMessageSuiteV2;
    if (is(kOfxProgressSuite) && version == 1) return &kProgressSuiteV1;
    if (is(kOfxProgressSuite) && version == 2) return &kProgressSuiteV2;
    if (is(kOfxTimeLineSuite) && version == 1) return &kTimeLineSuite;
    if (is(kOfxInteractSuite) && version == 1) return &kInteractSuite;
    return nullptr;
}

// The host as plug-ins see it. kOfxPropName is the reference's own
// ("com.FXHOME.HITFILM", Tannen 0x1806a8dc8): plug-ins made for HitFilm and
// VEGAS Effects recognise their host by it.
PropertySet& hostProperties()
{
    static PropertySet host = [] {
        PropertySet s;
        s.setString(kOfxPropType, kOfxTypeImageEffectHost);
        s.setString(kOfxPropName, "com.FXHOME.HITFILM");
        s.setString(kOfxPropLabel, "OpenVegas Effects");
        s.setInts(kOfxPropAPIVersion, {1, 4});
        s.setInts(kOfxPropVersion, {1, 0, 0});
        s.setString(kOfxPropVersionLabel, "1.0");
        s.setInt(kOfxImageEffectHostPropIsBackground, 0);
        s.setInt(kOfxImageEffectPropSupportsOverlays, 0);
        s.setInt(kOfxImageEffectPropSupportsMultiResolution, 0);
        s.setInt(kOfxImageEffectPropSupportsTiles, 0);
        s.setInt(kOfxImageEffectPropTemporalClipAccess, 0);
        s.setStrings(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA});
        s.setStrings(kOfxImageEffectPropSupportedContexts,
                     {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral,
                      kOfxImageEffectContextTransition, kOfxImageEffectContextGenerator});
        s.setStrings(kOfxImageEffectPropSupportedPixelDepths,
                     {kOfxBitDepthByte, kOfxBitDepthShort, kOfxBitDepthFloat});
        s.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
        s.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0);
        s.setInt(kOfxImageEffectPropSetableFrameRate, 0);
        s.setInt(kOfxImageEffectPropSetableFielding, 0);
        s.setInt(kOfxParamHostPropSupportsCustomInteract, 0);
        s.setInt(kOfxParamHostPropSupportsStringAnimation, 0);
        s.setInt(kOfxParamHostPropSupportsChoiceAnimation, 0);
        s.setInt(kOfxParamHostPropSupportsBooleanAnimation, 0);
        s.setInt(kOfxParamHostPropSupportsCustomAnimation, 0);
        s.setInt(kOfxParamHostPropSupportsParametricAnimation, 0);
        s.setInt(kOfxParamHostPropMaxParameters, -1);
        s.setInt(kOfxParamHostPropMaxPages, 0);
        s.setInts(kOfxParamHostPropPageRowColumnCount, {0, 0});
        s.setInt(kOfxImageEffectInstancePropSequentialRender, 0);
        s.setString(kOfxImageEffectPropOpenGLRenderSupported, "false");
        s.setInt(kOfxImageEffectPropRenderQualityDraft, 0);
        s.setString(kOfxImageEffectHostPropNativeOrigin, kOfxHostNativeOriginBottomLeft);
        return s;
    }();
    return host;
}

OfxHost* ofxHost()
{
    static OfxHost host{handleOf(&hostProperties()), fetchSuite};
    return &host;
}

// One recursive lock around everything plug-ins do.
QRecursiveMutex& hostMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

// A plug-in's entry point, with a structured exception handler around it on
// Windows: a fault in a plug-in fails the call instead of the application.
#ifdef _MSC_VER
int guardedEntry(OfxPluginEntryPoint* entry, const char* action, const void* handle,
                 OfxPropertySetHandle in, OfxPropertySetHandle out, OfxStatus* status)
{
    __try {
        *status = entry(action, handle, in, out);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return int(GetExceptionCode());
    }
}
#else
int guardedEntry(OfxPluginEntryPoint* entry, const char* action, const void* handle,
                 OfxPropertySetHandle in, OfxPropertySetHandle out, OfxStatus* status)
{
    *status = entry(action, handle, in, out);
    return 0;
}
#endif

// A loaded image effect: its binary, its OfxPlugin, its descriptors and the
// instances made of it.
struct PluginRuntime
{
    QString key;
    OfxPluginRecord record;
    QLibrary* library = nullptr;
    OfxPlugin* plugin = nullptr;
    bool loaded = false;
    QString error;
    std::unique_ptr<Effect> descriptor;
    std::map<QByteArray, std::unique_ptr<Effect>> contextDescriptors;
    std::map<QString, std::unique_ptr<Effect>> instances;

    OfxStatus call(const char* action, const void* handle, PropertySet* in = nullptr, PropertySet* out = nullptr)
    {
        if (!plugin || !plugin->mainEntry) return kOfxStatErrBadHandle;
        OfxStatus status = kOfxStatFailed;
        const int fault = guardedEntry(plugin->mainEntry, action, handle, in ? handleOf(in) : nullptr,
                                       out ? handleOf(out) : nullptr, &status);
        if (fault != 0) {
            error = QStringLiteral("%1 faulted (0x%2)").arg(QString::fromLatin1(action))
                        .arg(quint32(fault), 8, 16, QLatin1Char('0'));
            OV_LOG_WARN(QStringLiteral("OpenFX %1: %2").arg(record.identifier, error));
            return kOfxStatErrFatal;
        }
        return status;
    }
};

std::map<QString, std::unique_ptr<PluginRuntime>>& runtimes()
{
    static std::map<QString, std::unique_ptr<PluginRuntime>> map;
    return map;
}

struct Registration
{
    QString runtimeKey;
    OfxPluginRecord record;
    bool transition = false;
};

QHash<QString, Registration>& registrations()
{
    static QHash<QString, Registration> map;
    return map;
}

// The registrations have a lock of their own: the renderer asks
// isOfxPlugin of every effect while another thread may hold the host's lock
// through a whole render.
QMutex& registrationMutex()
{
    static QMutex mutex;
    return mutex;
}

std::optional<Registration> registration(const core::Identifier& id)
{
    QMutexLocker lock(&registrationMutex());
    const auto found = registrations().constFind(id.value());
    if (found == registrations().cend()) return std::nullopt;
    return *found;
}

// The binaries loaded so far; a binary is never unloaded (plug-ins keep
// static state, and some register atexit handlers).
QHash<QString, QLibrary*>& libraries()
{
    static QHash<QString, QLibrary*> map;
    return map;
}

typedef int (*GetNumberOfPluginsFn)();
typedef OfxPlugin* (*GetPluginFn)(int);
typedef OfxStatus (*SetHostFn)(const OfxHost*);

QLibrary* loadBinary(const QString& path, QString* error)
{
    const QString key = QFileInfo(path).absoluteFilePath();
    if (QLibrary* existing = libraries().value(key)) return existing;
    auto* library = new QLibrary(key);
    library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!library->load()) {
        if (error) *error = library->errorString();
        delete library;
        return nullptr;
    }
    if (!library->resolve("OfxGetNumberOfPlugins") || !library->resolve("OfxGetPlugin")) {
        if (error) *error = QStringLiteral("not an OpenFX binary (no OfxGetNumberOfPlugins/OfxGetPlugin)");
        library->unload();
        delete library;
        return nullptr;
    }
    // OpenFX 1.5: the host is given to the whole binary first.
    if (auto setHost = reinterpret_cast<SetHostFn>(library->resolve("OfxSetHost"))) setHost(ofxHost());
    libraries().insert(key, library);
    return library;
}

// The image effect descriptor's defaults (describe action).
std::unique_ptr<Effect> newDescriptor(PluginRuntime* runtime, const QByteArray& context = {})
{
    auto effect = std::make_unique<Effect>();
    effect->runtime = runtime;
    effect->context = context;
    PropertySet& s = effect->props;
    s.setString(kOfxPropType, kOfxTypeImageEffect);
    s.setString(kOfxPropLabel, runtime->plugin->pluginIdentifier);
    s.setString(kOfxPropShortLabel, "");
    s.setString(kOfxPropLongLabel, "");
    s.setString(kOfxImageEffectPluginPropGrouping, "");
    s.setString(kOfxPropPluginDescription, "");
    s.setStrings(kOfxImageEffectPropSupportedContexts, {});
    s.setStrings(kOfxImageEffectPropSupportedPixelDepths, {});
    s.setInt(kOfxImageEffectPluginPropSingleInstance, 0);
    s.setString(kOfxImageEffectPluginRenderThreadSafety, kOfxImageEffectRenderInstanceSafe);
    s.setInt(kOfxImageEffectPluginPropHostFrameThreading, 0);
    s.setInt(kOfxImageEffectPropSupportsMultiResolution, 1);
    s.setInt(kOfxImageEffectPropSupportsTiles, 1);
    s.setInt(kOfxImageEffectPropTemporalClipAccess, 0);
    s.setInt(kOfxImageEffectPluginPropFieldRenderTwiceAlways, 1);
    s.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
    s.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0);
    s.setPointer(kOfxImageEffectPluginPropOverlayInteractV1, nullptr);
    s.setString(kOfxImageEffectPropClipPreferencesSlaveParam, "");
    s.setString(kOfxImageEffectPropOpenGLRenderSupported, "false");
    if (!context.isEmpty()) s.setString(kOfxImageEffectPropContext, context);
    return effect;
}

// Loads a plug-in and runs load and describe (once).
bool prepareRuntime(PluginRuntime* runtime)
{
    if (runtime->loaded) return runtime->error.isEmpty();
    runtime->loaded = true;
    QString error;
    QLibrary* library = loadBinary(runtime->record.binaryPath, &error);
    if (!library) {
        runtime->error = error;
        return false;
    }
    auto get = reinterpret_cast<GetPluginFn>(library->resolve("OfxGetPlugin"));
    OfxPlugin* plugin = get ? get(runtime->record.index) : nullptr;
    if (!plugin || !plugin->pluginApi || std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0) {
        runtime->error = QStringLiteral("not an OpenFX image effect");
        return false;
    }
    runtime->plugin = plugin;
    if (plugin->setHost) plugin->setHost(ofxHost());
    const OfxStatus loaded = runtime->call(kOfxActionLoad, nullptr);
    if (loaded != kOfxStatOK && loaded != kOfxStatReplyDefault) {
        if (runtime->error.isEmpty()) runtime->error = QStringLiteral("the load action failed (%1)").arg(loaded);
        return false;
    }
    runtime->descriptor = newDescriptor(runtime);
    const OfxStatus described = runtime->call(kOfxActionDescribe, runtime->descriptor.get());
    if (described != kOfxStatOK && described != kOfxStatReplyDefault) {
        if (runtime->error.isEmpty()) runtime->error = QStringLiteral("the describe action failed (%1)").arg(described);
        return false;
    }
    return true;
}

// The descriptor of a context, made by describeInContext on a copy of the
// plug-in descriptor.
Effect* contextDescriptor(PluginRuntime* runtime, const QByteArray& context)
{
    auto found = runtime->contextDescriptors.find(context);
    if (found != runtime->contextDescriptors.end()) return found->second.get();
    auto effect = newDescriptor(runtime, context);
    effect->props = runtime->descriptor->props;
    effect->props.setString(kOfxImageEffectPropContext, context);
    PropertySet in;
    in.setString(kOfxImageEffectPropContext, context);
    const OfxStatus status = runtime->call(kOfxImageEffectActionDescribeInContext, effect.get(), &in);
    if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
        runtime->error = QStringLiteral("describeInContext(%1) failed (%2)")
                             .arg(QString::fromLatin1(context)).arg(status);
        return nullptr;
    }
    Effect* raw = effect.get();
    runtime->contextDescriptors[context] = std::move(effect);
    return raw;
}

QString colourText(const std::vector<double>& rgba)
{
    QColor c = QColor::fromRgbF(float(qBound(0.0, rgba.size() > 0 ? rgba[0] : 0.0, 1.0)),
                                float(qBound(0.0, rgba.size() > 1 ? rgba[1] : 0.0, 1.0)),
                                float(qBound(0.0, rgba.size() > 2 ? rgba[2] : 0.0, 1.0)),
                                float(qBound(0.0, rgba.size() > 3 ? rgba[3] : 1.0, 1.0)));
    return c.alpha() == 255 ? c.name() : c.name(QColor::HexArgb);
}

// The controls of a context descriptor, as this port's parameter specs.
void collectParameters(Effect* descriptor, QVector<EffectParameterSpec>* parameters, QStringList* types,
                       QStringList* normalised, bool transition)
{
    QHash<QByteArray, QString> groupLabels;
    for (auto& param : descriptor->params.params)
        if (param->type == kOfxParamTypeGroup)
            groupLabels.insert(param->name, QString::fromUtf8(param->props.string(kOfxPropLabel, 0, param->name)));
    for (auto& param : descriptor->params.params) {
        Param& p = *param;
        if (p.type == kOfxParamTypeGroup || p.type == kOfxParamTypePage || p.type == kOfxParamTypeParametric)
            continue;
        if (transition && p.name == kOfxImageEffectTransitionParamName) continue;
        EffectParameterSpec spec;
        spec.name = QString::fromUtf8(p.name);
        spec.displayName = QString::fromUtf8(p.props.string(kOfxPropLabel, 0, p.name));
        spec.group = groupLabels.value(p.props.string(kOfxParamPropParent));
        spec.hidden = p.props.number(kOfxParamPropSecret) != 0.0;
        const auto def = [&p](int i) { return p.props.number(kOfxParamPropDefault, i, 0.0); };
        const auto bound = [&p](const char* display, const char* hard, int i, double fallback) {
            double v = p.props.number(display, i, fallback);
            if (std::abs(v) >= 1e30 || v == double(INT_MIN) || v == double(INT_MAX)) v = p.props.number(hard, i, fallback);
            if (std::abs(v) >= 1e30 || v == double(INT_MIN) || v == double(INT_MAX)) v = fallback;
            return v;
        };
        const QByteArray doubleType = p.props.string(kOfxParamPropDoubleType);
        if (p.type == kOfxParamTypeInteger) {
            spec.type = QStringLiteral("int");
            spec.defaultValue = QString::number(int(def(0)));
            spec.minimum = bound(kOfxParamPropDisplayMin, kOfxParamPropMin, 0, -100000.0);
            spec.maximum = bound(kOfxParamPropDisplayMax, kOfxParamPropMax, 0, 100000.0);
            spec.decimals = 0;
        } else if (p.type == kOfxParamTypeDouble) {
            spec.type = doubleType == kOfxParamDoubleTypeAngle ? QStringLiteral("angle") : QStringLiteral("double");
            spec.defaultValue = QString::number(def(0), 'g', 10);
            spec.minimum = bound(kOfxParamPropDisplayMin, kOfxParamPropMin, 0, -100000.0);
            spec.maximum = bound(kOfxParamPropDisplayMax, kOfxParamPropMax, 0, 100000.0);
            spec.decimals = int(p.props.number(kOfxParamPropDigits, 0, 2));
            spec.step = p.props.number(kOfxParamPropIncrement, 0, 1.0);
        } else if (p.type == kOfxParamTypeBoolean) {
            spec.type = QStringLiteral("bool");
            spec.defaultValue = def(0) != 0.0 ? QStringLiteral("true") : QStringLiteral("false");
        } else if (p.type == kOfxParamTypeChoice) {
            spec.type = QStringLiteral("int");
            for (const QByteArray& option : p.props.stringList(kOfxParamPropChoiceOption))
                spec.choices << QString::fromUtf8(option);
            for (int i = 0; i < spec.choices.size(); ++i) spec.choiceValues << QString::number(i);
            spec.defaultValue = QString::number(int(def(0)));
        } else if (p.type == kOfxParamTypeRGBA || p.type == kOfxParamTypeRGB) {
            spec.type = QStringLiteral("color");
            std::vector<double> rgba{def(0), def(1), def(2), p.type == kOfxParamTypeRGBA ? def(3) : 1.0};
            spec.defaultValue = colourText(rgba);
        } else if (p.type == kOfxParamTypeDouble2D || p.type == kOfxParamTypeInteger2D) {
            spec.type = QStringLiteral("point2d");
            spec.defaultValue = QStringLiteral("%1,%2").arg(def(0)).arg(def(1));
        } else if (p.type == kOfxParamTypeDouble3D || p.type == kOfxParamTypeInteger3D) {
            spec.type = QStringLiteral("point3d");
            spec.defaultValue = QStringLiteral("%1,%2,%3").arg(def(0)).arg(def(1)).arg(def(2));
        } else if (p.type == kOfxParamTypeString) {
            const QByteArray mode = p.props.string(kOfxParamPropStringMode);
            spec.type = mode == kOfxParamStringIsFilePath ? QStringLiteral("open-file")
                      : mode == kOfxParamStringIsDirectoryPath ? QStringLiteral("directory")
                      : mode == kOfxParamStringIsMultiLine ? QStringLiteral("multiline")
                      : mode == kOfxParamStringIsLabel ? QStringLiteral("label")
                      : QStringLiteral("string");
            spec.defaultValue = QString::fromUtf8(p.props.string(kOfxParamPropDefault));
        } else if (p.type == kOfxParamTypePushButton) {
            spec.type = QStringLiteral("button");
            spec.defaultValue = QStringLiteral("false");
        } else if (p.type == kOfxParamTypeCustom) {
            spec.type = QStringLiteral("string");
            spec.hidden = true;
            spec.defaultValue = QString::fromUtf8(p.props.string(kOfxParamPropDefault));
        } else {
            continue;
        }
        parameters->append(spec);
        types->append(QString::fromLatin1(p.type));
        normalised->append(p.props.string(kOfxParamPropDefaultCoordinateSystem) == kOfxParamCoordinatesNormalised
                           ? QStringLiteral("1") : QString());
    }
}

QString runtimeKey(const QString& binaryPath, int index)
{
    return QFileInfo(binaryPath).absoluteFilePath() + QLatin1Char('#') + QString::number(index);
}

PluginRuntime* runtimeFor(const OfxPluginRecord& record)
{
    const QString key = runtimeKey(record.binaryPath, record.index);
    auto it = runtimes().find(key);
    if (it == runtimes().end()) {
        auto runtime = std::make_unique<PluginRuntime>();
        runtime->key = key;
        runtime->record = record;
        it = runtimes().emplace(key, std::move(runtime)).first;
    }
    return it->second.get();
}

// The context an effect is instantiated in: Filter, then General, then
// Generator (a generator replaces the layer's picture).
QByteArray effectContext(const QStringList& contexts)
{
    for (const char* context : {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral,
                                kOfxImageEffectContextGenerator}) {
        if (contexts.contains(QString::fromLatin1(context))) return context;
    }
    return {};
}

QString sandboxExecutable;

} // namespace

// ----------------------------------------------------------------- public

QString ofxPluginGuid(const QString& identifier, int versionMajor, bool transition)
{
    const QByteArray text = identifier.toUtf8() + ' ' + QByteArray::number(versionMajor) + ' '
                            + (transition ? "0" : "1");
    const QByteArray sha = QCryptographicHash::hash(text, QCryptographicHash::Sha1);
    const auto b = [&sha](int i) { return uint(uchar(sha.at(i))); };
    // Tannen FUN_1802ff1a0 writes the digest words into an FXID (Data1..4)
    // with the version in Data3's low byte and Data4[0] made of the same
    // byte's low nibble; kept exactly so that project files name the same
    // plug-in by the same ID.
    const uint data1 = (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);
    const uint data2 = (b(4) << 8) | b(5);
    const uint data3 = (((b(6) << 8) | b(7)) & 0xff0fu) | 0x0050u;
    const uint data4[8] = {(b(7) & 0x0fu) | 0xd0u, b(9), b(10), b(11), b(12), b(13), b(14), b(15)};
    return QStringLiteral("%1-%2-%3-%4%5-%6%7%8%9%10%11")
        .arg(data1, 8, 16, QLatin1Char('0')).arg(data2, 4, 16, QLatin1Char('0'))
        .arg(data3, 4, 16, QLatin1Char('0')).arg(data4[0], 2, 16, QLatin1Char('0'))
        .arg(data4[1], 2, 16, QLatin1Char('0')).arg(data4[2], 2, 16, QLatin1Char('0'))
        .arg(data4[3], 2, 16, QLatin1Char('0')).arg(data4[4], 2, 16, QLatin1Char('0'))
        .arg(data4[5], 2, 16, QLatin1Char('0')).arg(data4[6], 2, 16, QLatin1Char('0'))
        .arg(data4[7], 2, 16, QLatin1Char('0'));
}

QStringList ofxPluginSearchPaths()
{
    QStringList paths;
    for (const QString& path : qEnvironmentVariable("OFX_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
        paths << QDir::fromNativeSeparators(path);
#if defined(Q_OS_WIN)
    QString common = qEnvironmentVariable("CommonProgramFiles");
    if (common.isEmpty()) common = QStringLiteral("C:/Program Files/Common Files");
    paths << QDir::fromNativeSeparators(common) + QStringLiteral("/OFX/Plugins");
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/Library/OFX/Plugins");
#else
    paths << QStringLiteral("/usr/OFX/Plugins");
#endif
    paths.removeDuplicates();
    return paths;
}

QStringList findOfxBinaries(const QStringList& roots)
{
#if defined(Q_OS_WIN)
    const QString arch = QStringLiteral("Win64");
#elif defined(Q_OS_MACOS)
    const QString arch = QStringLiteral("MacOS");
#else
    const QString arch = QStringLiteral("Linux-x86-64");
#endif
    QStringList binaries;
    for (const QString& root : roots) {
        QDirIterator it(root, {QStringLiteral("*.ofx.bundle")}, QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString bundle = it.next();
            const QDir contents(bundle + QStringLiteral("/Contents/") + arch);
            for (const QString& file : contents.entryList({QStringLiteral("*.ofx")}, QDir::Files))
                binaries << contents.filePath(file);
        }
    }
    binaries.removeDuplicates();
    binaries.sort(Qt::CaseInsensitive);
    return binaries;
}

QVector<OfxPluginRecord> describeOfxBinary(const QString& binaryPath, QString* error)
{
    QMutexLocker lock(&hostMutex());
    QVector<OfxPluginRecord> records;
    QString loadError;
    QLibrary* library = loadBinary(binaryPath, &loadError);
    if (!library) {
        if (error) *error = loadError;
        return records;
    }
    auto count = reinterpret_cast<GetNumberOfPluginsFn>(library->resolve("OfxGetNumberOfPlugins"));
    auto get = reinterpret_cast<GetPluginFn>(library->resolve("OfxGetPlugin"));
    const int n = count ? count() : 0;
    for (int i = 0; i < n; ++i) {
        OfxPlugin* plugin = get(i);
        if (!plugin || !plugin->pluginApi || std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0
            || !plugin->pluginIdentifier)
            continue;
        OfxPluginRecord record;
        record.binaryPath = QFileInfo(binaryPath).absoluteFilePath();
        record.index = i;
        record.identifier = QString::fromLatin1(plugin->pluginIdentifier);
        record.versionMajor = int(plugin->pluginVersionMajor);
        record.versionMinor = int(plugin->pluginVersionMinor);
        PluginRuntime* runtime = runtimeFor(record);
        if (!prepareRuntime(runtime)) {
            record.error = runtime->error;
            records.append(record);
            continue;
        }
        PropertySet& d = runtime->descriptor->props;
        record.label = QString::fromUtf8(d.string(kOfxPropLabel, 0, plugin->pluginIdentifier));
        record.grouping = QString::fromUtf8(d.string(kOfxImageEffectPluginPropGrouping));
        record.description = QString::fromUtf8(d.string(kOfxPropPluginDescription));
        for (const QByteArray& context : d.stringList(kOfxImageEffectPropSupportedContexts))
            record.contexts << QString::fromLatin1(context);
        record.context = QString::fromLatin1(effectContext(record.contexts));
        record.transition = record.contexts.contains(QString::fromLatin1(kOfxImageEffectContextTransition));
        const QByteArray context = record.context.isEmpty() ? QByteArray(kOfxImageEffectContextTransition)
                                                            : record.context.toLatin1();
        if (record.context.isEmpty() && !record.transition) {
            record.error = QStringLiteral("no context this host supports");
        } else if (Effect* descriptor = contextDescriptor(runtime, context)) {
            collectParameters(descriptor, &record.parameters, &record.ofxTypes, &record.normalisedDefaults,
                              record.context.isEmpty());
        } else {
            record.error = runtime->error;
        }
        records.append(record);
    }
    return records;
}

void setOfxSandboxExecutable(const QString& executable)
{
    sandboxExecutable = executable;
}

QVector<OfxPluginRecord> describeOfxBinarySafely(const QString& binaryPath, QString* error)
{
    if (sandboxExecutable.isEmpty()) return describeOfxBinary(binaryPath, error);
    QTemporaryDir dir;
    const QString output = dir.filePath(QStringLiteral("describe.json"));
    QProcess process;
    process.start(sandboxExecutable, {QStringLiteral("--ofx-describe"), binaryPath, output});
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error) *error = QStringLiteral("the OpenFX sandbox did not finish describing it");
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        if (error) *error = QStringLiteral("the plug-in crashed the OpenFX sandbox");
        return {};
    }
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("the OpenFX sandbox wrote nothing (exit code %1)").arg(process.exitCode());
        return {};
    }
    return ofxRecordsFromJson(file.readAll());
}

int runOfxDescribeSandbox(const QString& binaryPath, const QString& outputPath)
{
    QString error;
    QVector<OfxPluginRecord> records = describeOfxBinary(binaryPath, &error);
    if (records.isEmpty() && !error.isEmpty()) {
        OfxPluginRecord failed;
        failed.binaryPath = binaryPath;
        failed.error = error;
        records.append(failed);
    }
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) return 2;
    file.write(ofxRecordsToJson(records));
    return 0;
}

QString ofxPluginCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/ofxPluginCache.json");
}

bool deleteOfxPluginCache()
{
    const QString path = ofxPluginCachePath();
    return path.isEmpty() || !QFile::exists(path) || QFile::remove(path);
}

QVector<OfxPluginRecord> describeOfxBinariesCached(const QStringList& binaryPaths)
{
    // Bumped when the records describe more than an older build wrote.
    constexpr int kCacheVersion = 1;
    const QString path = ofxPluginCachePath();
    QJsonObject cached;
    if (QFile file(path); !path.isEmpty() && file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        if (root.value(QStringLiteral("version")).toInt() == kCacheVersion)
            cached = root.value(QStringLiteral("binaries")).toObject();
    }
    QJsonObject fresh;
    bool changed = cached.size() != binaryPaths.size();
    QVector<OfxPluginRecord> records;
    for (const QString& binary : binaryPaths) {
        const QFileInfo info(binary);
        const QString key = info.absoluteFilePath();
        const qint64 size = info.size();
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        QJsonObject entry = cached.value(key).toObject();
        if (entry.isEmpty() || entry.value(QStringLiteral("size")).toInteger() != size
            || entry.value(QStringLiteral("modified")).toInteger() != modified) {
            QString error;
            QVector<OfxPluginRecord> described = describeOfxBinarySafely(key, &error);
            if (described.isEmpty() && !error.isEmpty()) {
                OfxPluginRecord failed;
                failed.binaryPath = key;
                failed.error = error;
                described.append(failed);
            }
            entry = QJsonObject();
            entry.insert(QStringLiteral("size"), size);
            entry.insert(QStringLiteral("modified"), modified);
            entry.insert(QStringLiteral("records"),
                         QJsonDocument::fromJson(ofxRecordsToJson(described)).array());
            changed = true;
        }
        fresh.insert(key, entry);
        const QByteArray json = QJsonDocument(entry.value(QStringLiteral("records")).toArray()).toJson();
        for (const OfxPluginRecord& record : ofxRecordsFromJson(json)) {
            if (!record.error.isEmpty())
                OV_LOG_WARN(QStringLiteral("OpenFX %1: %2").arg(record.identifier.isEmpty() ? key : record.identifier,
                                                                 record.error));
            if (!record.identifier.isEmpty()) records.append(record);
        }
    }
    if (changed && !path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonObject root;
        root.insert(QStringLiteral("version"), kCacheVersion);
        root.insert(QStringLiteral("binaries"), fresh);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    return records;
}

QByteArray ofxRecordsToJson(const QVector<OfxPluginRecord>& records)
{
    QJsonArray array;
    for (const OfxPluginRecord& r : records) {
        QJsonObject o;
        o.insert(QStringLiteral("binary"), r.binaryPath);
        o.insert(QStringLiteral("index"), r.index);
        o.insert(QStringLiteral("identifier"), r.identifier);
        o.insert(QStringLiteral("major"), r.versionMajor);
        o.insert(QStringLiteral("minor"), r.versionMinor);
        o.insert(QStringLiteral("label"), r.label);
        o.insert(QStringLiteral("grouping"), r.grouping);
        o.insert(QStringLiteral("description"), r.description);
        o.insert(QStringLiteral("contexts"), QJsonArray::fromStringList(r.contexts));
        o.insert(QStringLiteral("context"), r.context);
        o.insert(QStringLiteral("transition"), r.transition);
        o.insert(QStringLiteral("error"), r.error);
        QJsonArray params;
        for (int i = 0; i < r.parameters.size(); ++i) {
            const EffectParameterSpec& p = r.parameters.at(i);
            QJsonObject po;
            po.insert(QStringLiteral("name"), p.name);
            po.insert(QStringLiteral("label"), p.displayName);
            po.insert(QStringLiteral("type"), p.type);
            po.insert(QStringLiteral("ofxType"), r.ofxTypes.value(i));
            po.insert(QStringLiteral("normalised"), r.normalisedDefaults.value(i));
            po.insert(QStringLiteral("default"), p.defaultValue);
            po.insert(QStringLiteral("min"), p.minimum);
            po.insert(QStringLiteral("max"), p.maximum);
            po.insert(QStringLiteral("decimals"), p.decimals);
            po.insert(QStringLiteral("step"), p.step);
            po.insert(QStringLiteral("choices"), QJsonArray::fromStringList(p.choices));
            po.insert(QStringLiteral("choiceValues"), QJsonArray::fromStringList(p.choiceValues));
            po.insert(QStringLiteral("group"), p.group);
            po.insert(QStringLiteral("hidden"), p.hidden);
            params.append(po);
        }
        o.insert(QStringLiteral("parameters"), params);
        array.append(o);
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QVector<OfxPluginRecord> ofxRecordsFromJson(const QByteArray& json)
{
    QVector<OfxPluginRecord> records;
    const auto strings = [](const QJsonValue& v) {
        QStringList out;
        for (const QJsonValue& item : v.toArray()) out << item.toString();
        return out;
    };
    for (const QJsonValue& value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = value.toObject();
        OfxPluginRecord r;
        r.binaryPath = o.value(QStringLiteral("binary")).toString();
        r.index = o.value(QStringLiteral("index")).toInt();
        r.identifier = o.value(QStringLiteral("identifier")).toString();
        r.versionMajor = o.value(QStringLiteral("major")).toInt(1);
        r.versionMinor = o.value(QStringLiteral("minor")).toInt();
        r.label = o.value(QStringLiteral("label")).toString();
        r.grouping = o.value(QStringLiteral("grouping")).toString();
        r.description = o.value(QStringLiteral("description")).toString();
        r.contexts = strings(o.value(QStringLiteral("contexts")));
        r.context = o.value(QStringLiteral("context")).toString();
        r.transition = o.value(QStringLiteral("transition")).toBool();
        r.error = o.value(QStringLiteral("error")).toString();
        for (const QJsonValue& pv : o.value(QStringLiteral("parameters")).toArray()) {
            const QJsonObject po = pv.toObject();
            EffectParameterSpec p;
            p.name = po.value(QStringLiteral("name")).toString();
            p.displayName = po.value(QStringLiteral("label")).toString();
            p.type = po.value(QStringLiteral("type")).toString();
            p.defaultValue = po.value(QStringLiteral("default")).toString();
            p.minimum = po.value(QStringLiteral("min")).toDouble(-100000.0);
            p.maximum = po.value(QStringLiteral("max")).toDouble(100000.0);
            p.decimals = po.value(QStringLiteral("decimals")).toInt(2);
            p.step = po.value(QStringLiteral("step")).toDouble(1.0);
            p.choices = strings(po.value(QStringLiteral("choices")));
            p.choiceValues = strings(po.value(QStringLiteral("choiceValues")));
            p.group = po.value(QStringLiteral("group")).toString();
            p.hidden = po.value(QStringLiteral("hidden")).toBool();
            r.parameters.append(p);
            r.ofxTypes.append(po.value(QStringLiteral("ofxType")).toString());
            r.normalisedDefaults.append(po.value(QStringLiteral("normalised")).toString());
        }
        records.append(r);
    }
    return records;
}

core::Identifier ofxEffectId(const QString& identifier, bool transition)
{
    return core::Identifier((transition ? QStringLiteral("ofx-transition:") : QStringLiteral("ofx:")) + identifier);
}

EffectSpec ofxEffectSpec(const OfxPluginRecord& record, bool transition)
{
    EffectSpec spec;
    spec.id = ofxEffectId(record.identifier, transition);
    spec.name = record.identifier;
    spec.displayName = record.label.isEmpty() ? record.identifier : record.label;
    // The grouping is a path ("Blur/Gaussian"): its first part is the category.
    const QStringList groups = record.grouping.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    spec.category = groups.isEmpty() ? QStringLiteral("OpenFX") : groups.first();
    if (groups.size() > 1) spec.subCategory = groups.mid(1).join(QLatin1Char('/'));
    spec.description = record.description;
    spec.version = core::Version(record.versionMajor, record.versionMinor, 0);
    spec.kind = transition ? PluginKind::VideoTransition : PluginKind::Effect2D;
    spec.parameters = record.parameters;
    spec.guid = ofxPluginGuid(record.identifier, record.versionMajor, transition);
    spec.renderable = record.isValid();
    spec.unavailableReason = record.error;
    spec.keywords << QStringLiteral("OpenFX") << QStringLiteral("OFX");
    return spec;
}

void registerOfxPlugin(const core::Identifier& id, const OfxPluginRecord& record, bool transition)
{
    QMutexLocker lock(&registrationMutex());
    registrations().insert(id.value(), {runtimeKey(record.binaryPath, record.index), record, transition});
}

void clearOfxPlugins()
{
    QMutexLocker lock(&registrationMutex());
    registrations().clear();
}

bool isOfxPlugin(const core::Identifier& id)
{
    QMutexLocker lock(&registrationMutex());
    return registrations().contains(id.value());
}

QStringList ofxMessages()
{
    QMutexLocker lock(&g_messageMutex);
    return g_messages;
}

namespace {

// Makes (once) the instance of a plug-in an effect renders with.
Effect* instanceFor(PluginRuntime* runtime, const QByteArray& context, const QString& key, const QSize& size,
                    const NativeFrameTime& time)
{
    const QString instanceKey = QString::fromLatin1(context) + QLatin1Char('|') + key;
    auto found = runtime->instances.find(instanceKey);
    if (found != runtime->instances.end()) return found->second.get();
    Effect* descriptor = contextDescriptor(runtime, context);
    if (!descriptor) return nullptr;
    auto effect = std::make_unique<Effect>();
    effect->runtime = runtime;
    effect->instance = true;
    effect->context = context;
    effect->props = descriptor->props;
    PropertySet& s = effect->props;
    s.setString(kOfxPropType, kOfxTypeImageEffectInstance);
    s.setString(kOfxImageEffectPropContext, context);
    s.setPointer(kOfxPropInstanceData, nullptr);
    s.setInt(kOfxPropIsInteractive, 0);
    s.setDoubles(kOfxImageEffectPropProjectSize, {double(size.width()), double(size.height())});
    s.setDoubles(kOfxImageEffectPropProjectOffset, {0.0, 0.0});
    s.setDoubles(kOfxImageEffectPropProjectExtent, {double(size.width()), double(size.height())});
    s.setDouble(kOfxImageEffectPropProjectPixelAspectRatio, 1.0);
    s.setDouble(kOfxImageEffectInstancePropEffectDuration, double(qMax(1, time.layerFrames)));
    s.setInt(kOfxImageEffectInstancePropSequentialRender, 0);
    s.setDouble(kOfxImageEffectPropFrameRate, time.frameRate > 0.0 ? time.frameRate : 30.0);
    s.setInt(kOfxImageEffectPropInAnalysis, 0);
    for (auto& param : descriptor->params.params) {
        auto copy = std::make_unique<Param>();
        copy->name = param->name;
        copy->type = param->type;
        copy->props = param->props;
        copy->resetToDefault();
        effect->params.params.push_back(std::move(copy));
    }
    for (auto& clip : descriptor->clips) {
        auto copy = std::make_unique<Clip>();
        copy->name = clip->name;
        copy->props = clip->props;
        copy->effect = effect.get();
        PropertySet& c = copy->props;
        c.setString(kOfxImageEffectPropPixelDepth, kOfxBitDepthByte);
        c.setString(kOfxImageEffectPropComponents, kOfxImageComponentRGBA);
        c.setString(kOfxImageClipPropUnmappedPixelDepth, kOfxBitDepthByte);
        c.setString(kOfxImageClipPropUnmappedComponents, kOfxImageComponentRGBA);
        c.setString(kOfxImageEffectPropPreMultiplication, kOfxImagePreMultiplied);
        c.setDouble(kOfxImagePropPixelAspectRatio, 1.0);
        c.setDouble(kOfxImageEffectPropFrameRate, time.frameRate > 0.0 ? time.frameRate : 30.0);
        c.setDoubles(kOfxImageEffectPropFrameRange, {0.0, double(qMax(1, time.layerFrames))});
        c.setDouble(kOfxImageEffectPropUnmappedFrameRate, time.frameRate > 0.0 ? time.frameRate : 30.0);
        c.setDoubles(kOfxImageEffectPropUnmappedFrameRange, {0.0, double(qMax(1, time.layerFrames))});
        c.setString(kOfxImageClipPropFieldOrder, kOfxImageFieldNone);
        c.setInt(kOfxImageClipPropConnected, 0);
        c.setInt(kOfxImageClipPropContinuousSamples, 0);
        effect->clips.push_back(std::move(copy));
    }
    Effect* raw = effect.get();
    runtime->instances[instanceKey] = std::move(effect);
    const OfxStatus created = runtime->call(kOfxActionCreateInstance, raw);
    if (created != kOfxStatOK && created != kOfxStatReplyDefault) {
        runtime->instances.erase(instanceKey);
        if (runtime->error.isEmpty()) runtime->error = QStringLiteral("createInstance failed (%1)").arg(created);
        return nullptr;
    }
    return raw;
}

// Puts an effect's values (this port's strings) into the instance.
void applyValues(Effect* effect, const OfxPluginRecord& record, const QStringList& values, bool transition,
                 const QSize& size)
{
    const QVector<EffectParameterSpec>& specs = record.parameters;
    for (int i = 0; i < specs.size(); ++i) {
        Param* p = effect->params.find(specs.at(i).name.toUtf8().constData());
        if (!p) continue;
        QString value = i < values.size() && !values.at(i).isEmpty() ? values.at(i) : specs.at(i).defaultValue;
        if (p->textual()) {
            p->text = value.toUtf8();
            continue;
        }
        const int dimension = p->dimension();
        if (dimension == 0) continue;
        p->numbers.assign(size_t(dimension), 0.0);
        if (p->type == kOfxParamTypeBoolean) {
            p->numbers[0] = value == QLatin1String("true") || value == QLatin1String("1") ? 1.0 : 0.0;
        } else if (p->type == kOfxParamTypeChoice) {
            // The option's index, or its label as an older value names it.
            bool number = false;
            const int index = value.toInt(&number);
            p->numbers[0] = number ? index : qMax(0, specs.at(i).choices.indexOf(value));
        } else if (p->type == kOfxParamTypeRGBA || p->type == kOfxParamTypeRGB) {
            const QColor c(value.trimmed());
            const QColor colour = c.isValid() ? c : QColor(Qt::black);
            p->numbers[0] = colour.redF(); p->numbers[1] = colour.greenF(); p->numbers[2] = colour.blueF();
            if (dimension > 3) p->numbers[3] = colour.alphaF();
        } else {
            const QStringList parts = value.split(QLatin1Char(','));
            for (int d = 0; d < dimension; ++d) p->numbers[size_t(d)] = parts.value(d).trimmed().toDouble();
            // A default given in normalised coordinates, kept until edited.
            if (record.normalisedDefaults.value(i) == QLatin1String("1") && value == specs.at(i).defaultValue) {
                for (int d = 0; d < dimension; ++d)
                    p->numbers[size_t(d)] *= d == 0 ? size.width() : d == 1 ? size.height() : 1.0;
            }
        }
    }
    Q_UNUSED(transition);
}

// Picks the deepest depth both sides support, preferring 8 bits.
QByteArray pickDepth(PropertySet& props)
{
    const QList<QByteArray> depths = props.stringList(kOfxImageEffectPropSupportedPixelDepths);
    if (depths.isEmpty() || depths.contains(kOfxBitDepthByte)) return kOfxBitDepthByte;
    if (depths.contains(kOfxBitDepthFloat)) return kOfxBitDepthFloat;
    if (depths.contains(kOfxBitDepthShort)) return kOfxBitDepthShort;
    return kOfxBitDepthByte;
}

// One render of an instance: inputs by clip name, the output's size.
bool render(const core::Identifier& id, const QHash<QByteArray, QImage>& inputs, const QSize& size,
            const QStringList& values, const NativeFrameTime& time, double transitionProgress, QImage* result)
{
    const std::optional<Registration> reg = registration(id);
    if (!reg) return false;
    QMutexLocker lock(&hostMutex());
    PluginRuntime* runtime = runtimeFor(reg->record);
    if (!prepareRuntime(runtime)) return false;
    const QByteArray context = reg->transition ? QByteArray(kOfxImageEffectContextTransition)
                                               : effectContext(reg->record.contexts);
    if (context.isEmpty()) return false;
    const QString key = time.instanceKey.isEmpty() ? QStringLiteral("default") : time.instanceKey;
    Effect* effect = instanceFor(runtime, context, key, size, time);
    if (!effect) return false;
    effect->time = time.layerFrame;
    effect->duration = qMax(1, time.layerFrames);
    effect->depth = pickDepth(effect->props);
    effect->props.setDoubles(kOfxImageEffectPropProjectSize, {double(size.width()), double(size.height())});
    effect->props.setDoubles(kOfxImageEffectPropProjectExtent, {double(size.width()), double(size.height())});
    applyValues(effect, reg->record, values, reg->transition, size);
    if (reg->transition) {
        if (Param* t = effect->params.find(kOfxImageEffectTransitionParamName)) t->numbers = {transitionProgress};
    }
    // The clips: inputs connected where a picture is given, the output a
    // fresh transparent buffer.
    auto output = std::make_shared<Image>();
    output->pixels = QByteArray(qsizetype(size.width()) * size.height() * 4 * bytesPerComponent(effect->depth), '\0');
    describeImage(*output, size.width(), size.height(), effect->depth, QByteArrayLiteral("Output"));
    for (auto& clip : effect->clips) {
        clip->input = QImage();
        clip->output.reset();
        clip->props.setString(kOfxImageEffectPropPixelDepth, effect->depth);
        clip->props.setString(kOfxImageClipPropUnmappedPixelDepth, effect->depth);
        if (clip->name == kOfxImageEffectOutputClipName) {
            clip->output = output;
            clip->props.setInt(kOfxImageClipPropConnected, 1);
        } else {
            clip->input = inputs.value(clip->name);
            clip->props.setInt(kOfxImageClipPropConnected, clip->input.isNull() ? 0 : 1);
        }
    }
    const double frame = time.layerFrame;
    PropertySet begin;
    begin.setDoubles(kOfxImageEffectPropFrameRange, {frame, frame});
    begin.setDouble(kOfxImageEffectPropFrameStep, 1.0);
    begin.setInt(kOfxPropIsInteractive, 0);
    begin.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
    begin.setInt(kOfxImageEffectPropSequentialRenderStatus, 0);
    begin.setInt(kOfxImageEffectPropInteractiveRenderStatus, 0);
    runtime->call(kOfxImageEffectActionBeginSequenceRender, effect, &begin);
    PropertySet args;
    args.setDouble(kOfxPropTime, frame);
    args.setString(kOfxImageEffectPropFieldToRender, kOfxImageFieldNone);
    args.setInts(kOfxImageEffectPropRenderWindow, {0, 0, size.width(), size.height()});
    args.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
    args.setInt(kOfxImageEffectPropSequentialRenderStatus, 0);
    args.setInt(kOfxImageEffectPropInteractiveRenderStatus, 0);
    args.setInt(kOfxImageEffectPropRenderQualityDraft, 0);
    const OfxStatus status = runtime->call(kOfxImageEffectActionRender, effect, &args);
    runtime->call(kOfxImageEffectActionEndSequenceRender, effect, &begin);
    effect->images.clear();
    for (auto& clip : effect->clips) { clip->input = QImage(); clip->output.reset(); }
    if (status != kOfxStatOK) {
        OV_LOG_WARN(QStringLiteral("OpenFX %1: render failed (%2)").arg(reg->record.identifier).arg(status));
        return false;
    }
    *result = fromOfxPixels(output->pixels, size.width(), size.height(), effect->depth);
    return !result->isNull();
}

} // namespace

bool applyOfxEffectToImage(QImage& image, const core::Identifier& id, const QStringList& values,
                           const NativeFrameTime& time)
{
    if (image.isNull()) return false;
    const QImage source = image.format() == QImage::Format_RGBA8888 ? image
                                                                    : image.convertToFormat(QImage::Format_RGBA8888);
    QHash<QByteArray, QImage> inputs;
    inputs.insert(kOfxImageEffectSimpleSourceClipName, source);
    QImage result;
    if (!render(id, inputs, source.size(), values, time, 0.0, &result)) return false;
    image = result;
    return true;
}

bool applyOfxTransition(QImage& output, const QImage& from, const QImage& to, double progress,
                        const core::Identifier& id, const QStringList& values, const NativeFrameTime& time)
{
    const QSize size = !from.isNull() ? from.size() : to.size();
    if (size.isEmpty()) return false;
    QHash<QByteArray, QImage> inputs;
    inputs.insert(kOfxImageEffectTransitionSourceFromClipName, from.convertToFormat(QImage::Format_RGBA8888));
    inputs.insert(kOfxImageEffectTransitionSourceToClipName, to.convertToFormat(QImage::Format_RGBA8888));
    QImage result;
    if (!render(id, inputs, size, values, time, qBound(0.0, progress, 1.0), &result)) return false;
    output = result;
    return true;
}

} // namespace plugin
} // namespace openvegas
