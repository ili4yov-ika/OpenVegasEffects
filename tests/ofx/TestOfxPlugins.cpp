// OpenFX image effects for the OpenFX host's tests, written against the bare
// OpenFX C API (no support library), as a plug-in vendor would:
//   org.openvegas.test.Gain   a filter: Gain (double), Mode (choice), Tint
//                             (RGBA), Keep Alpha (boolean), Offset (2D, in
//                             normalised coordinates);
//   org.openvegas.test.Wipe   a transition: SourceTo left of the Transition
//                             position, SourceFrom right of it.
// Built with OPENVEGAS_OFX_CRASH it is instead one plug-in,
// org.openvegas.test.Crash, that faults in its describe action.

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include <cmath>
#include <cstring>

namespace {

const OfxHost* g_host = nullptr;
const OfxPropertySuiteV1* g_props = nullptr;
const OfxImageEffectSuiteV1* g_effects = nullptr;
const OfxParameterSuiteV1* g_params = nullptr;

void setHost(OfxHost* host)
{
    g_host = host;
}

OfxStatus loadSuites()
{
    if (!g_host) return kOfxStatErrMissingHostFeature;
    g_props = static_cast<const OfxPropertySuiteV1*>(g_host->fetchSuite(g_host->host, kOfxPropertySuite, 1));
    g_effects = static_cast<const OfxImageEffectSuiteV1*>(g_host->fetchSuite(g_host->host, kOfxImageEffectSuite, 1));
    g_params = static_cast<const OfxParameterSuiteV1*>(g_host->fetchSuite(g_host->host, kOfxParameterSuite, 1));
    return g_props && g_effects && g_params ? kOfxStatOK : kOfxStatErrMissingHostFeature;
}

// The 8-bit or float component at (x, y) of an image, through its bounds and
// row bytes.
struct View
{
    OfxPropertySetHandle props = nullptr;
    char* data = nullptr;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, rowBytes = 0;
    bool isFloat = false;

    bool fetch(OfxImageClipHandle clip, OfxTime time)
    {
        if (g_effects->clipGetImage(clip, time, nullptr, &props) != kOfxStatOK) return false;
        void* p = nullptr;
        g_props->propGetPointer(props, kOfxImagePropData, 0, &p);
        data = static_cast<char*>(p);
        int bounds[4] = {};
        g_props->propGetIntN(props, kOfxImagePropBounds, 4, bounds);
        x1 = bounds[0]; y1 = bounds[1]; x2 = bounds[2]; y2 = bounds[3];
        g_props->propGetInt(props, kOfxImagePropRowBytes, 0, &rowBytes);
        char* depth = nullptr;
        g_props->propGetString(props, kOfxImageEffectPropPixelDepth, 0, &depth);
        isFloat = depth && std::strcmp(depth, kOfxBitDepthFloat) == 0;
        return data != nullptr;
    }
    void release()
    {
        if (props) g_effects->clipReleaseImage(props);
        props = nullptr;
    }
    float get(int x, int y, int c) const
    {
        const char* row = data + (y - y1) * rowBytes;
        return isFloat ? reinterpret_cast<const float*>(row)[(x - x1) * 4 + c]
                       : reinterpret_cast<const unsigned char*>(row)[(x - x1) * 4 + c] / 255.0f;
    }
    void set(int x, int y, int c, float v)
    {
        char* row = data + (y - y1) * rowBytes;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (isFloat) reinterpret_cast<float*>(row)[(x - x1) * 4 + c] = v;
        else reinterpret_cast<unsigned char*>(row)[(x - x1) * 4 + c] = static_cast<unsigned char>(std::lround(v * 255.0f));
    }
};

OfxStatus describeCommon(OfxImageEffectHandle effect, const char* label, const char* grouping,
                         const char* const* contexts, int contextCount)
{
    OfxPropertySetHandle props = nullptr;
    g_effects->getPropertySet(effect, &props);
    g_props->propSetString(props, kOfxPropLabel, 0, label);
    g_props->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, grouping);
    g_props->propSetString(props, kOfxPropPluginDescription, 0, "A test plug-in of OpenVegas Effects.");
    g_props->propSetStringN(props, kOfxImageEffectPropSupportedContexts, contextCount, contexts);
    const char* depths[] = {kOfxBitDepthByte, kOfxBitDepthFloat};
    g_props->propSetStringN(props, kOfxImageEffectPropSupportedPixelDepths, 2, depths);
    g_props->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    return kOfxStatOK;
}

void defineClip(OfxImageEffectHandle effect, const char* name)
{
    OfxPropertySetHandle props = nullptr;
    g_effects->clipDefine(effect, name, &props);
    g_props->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
}

// ------------------------------------------------------------------ Gain

OfxStatus gainDescribeInContext(OfxImageEffectHandle effect)
{
    defineClip(effect, kOfxImageEffectSimpleSourceClipName);
    defineClip(effect, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    g_effects->getParamSet(effect, &params);
    OfxPropertySetHandle p = nullptr;

    g_params->paramDefine(params, kOfxParamTypeGroup, "adjust", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Adjust");

    g_params->paramDefine(params, kOfxParamTypeDouble, "gain", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Gain");
    g_props->propSetString(p, kOfxParamPropParent, 0, "adjust");
    g_props->propSetDouble(p, kOfxParamPropDefault, 0, 1.0);
    g_props->propSetDouble(p, kOfxParamPropMin, 0, 0.0);
    g_props->propSetDouble(p, kOfxParamPropMax, 0, 4.0);
    g_props->propSetDouble(p, kOfxParamPropDisplayMin, 0, 0.0);
    g_props->propSetDouble(p, kOfxParamPropDisplayMax, 0, 2.0);

    g_params->paramDefine(params, kOfxParamTypeChoice, "mode", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Mode");
    g_props->propSetString(p, kOfxParamPropChoiceOption, 0, "Multiply");
    g_props->propSetString(p, kOfxParamPropChoiceOption, 1, "Invert");
    g_props->propSetInt(p, kOfxParamPropDefault, 0, 0);

    g_params->paramDefine(params, kOfxParamTypeRGBA, "tint", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Tint");
    const double white[] = {1.0, 1.0, 1.0, 1.0};
    g_props->propSetDoubleN(p, kOfxParamPropDefault, 4, white);

    g_params->paramDefine(params, kOfxParamTypeBoolean, "keepAlpha", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Keep Alpha");
    g_props->propSetInt(p, kOfxParamPropDefault, 0, 1);

    g_params->paramDefine(params, kOfxParamTypeDouble2D, "offset", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Offset");
    g_props->propSetString(p, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeXYAbsolute);
    g_props->propSetString(p, kOfxParamPropDefaultCoordinateSystem, 0, kOfxParamCoordinatesNormalised);
    const double centre[] = {0.5, 0.5};
    g_props->propSetDoubleN(p, kOfxParamPropDefault, 2, centre);
    return kOfxStatOK;
}

OfxStatus gainRender(OfxImageEffectHandle effect, OfxPropertySetHandle in)
{
    OfxTime time = 0;
    g_props->propGetDouble(in, kOfxPropTime, 0, &time);
    int window[4] = {};
    g_props->propGetIntN(in, kOfxImageEffectPropRenderWindow, 4, window);
    OfxImageClipHandle sourceClip = nullptr, outputClip = nullptr;
    g_effects->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &sourceClip, nullptr);
    g_effects->clipGetHandle(effect, kOfxImageEffectOutputClipName, &outputClip, nullptr);
    OfxParamSetHandle params = nullptr;
    g_effects->getParamSet(effect, &params);
    OfxParamHandle gainParam = nullptr, modeParam = nullptr, tintParam = nullptr, keepParam = nullptr, offsetParam = nullptr;
    g_params->paramGetHandle(params, "gain", &gainParam, nullptr);
    g_params->paramGetHandle(params, "mode", &modeParam, nullptr);
    g_params->paramGetHandle(params, "tint", &tintParam, nullptr);
    g_params->paramGetHandle(params, "keepAlpha", &keepParam, nullptr);
    g_params->paramGetHandle(params, "offset", &offsetParam, nullptr);
    double gain = 1.0, r = 1.0, g = 1.0, b = 1.0, a = 1.0, ox = 0.0, oy = 0.0;
    int mode = 0, keep = 1;
    g_params->paramGetValueAtTime(gainParam, time, &gain);
    g_params->paramGetValueAtTime(modeParam, time, &mode);
    g_params->paramGetValueAtTime(tintParam, time, &r, &g, &b, &a);
    g_params->paramGetValueAtTime(keepParam, time, &keep);
    g_params->paramGetValueAtTime(offsetParam, time, &ox, &oy);
    View src, dst;
    if (!src.fetch(sourceClip, time) || !dst.fetch(outputClip, time)) {
        src.release();
        dst.release();
        return kOfxStatFailed;
    }
    const float tint[3] = {float(r), float(g), float(b)};
    for (int y = window[1]; y < window[3]; ++y) {
        for (int x = window[0]; x < window[2]; ++x) {
            const float alpha = src.get(x, y, 3);
            for (int c = 0; c < 3; ++c) {
                const float v = src.get(x, y, c);
                // Premultiplied: inverting is alpha - value.
                dst.set(x, y, c, mode == 1 ? alpha - v : float(v * gain * tint[c]));
            }
            dst.set(x, y, 3, keep ? alpha : 1.0f);
        }
    }
    // The offset's pixel position (the host turns the normalised default
    // into pixels) marks itself with one white pixel.
    const int mx = int(ox), my = int(oy);
    if (mx >= window[0] && mx < window[2] && my >= window[1] && my < window[3])
        for (int c = 0; c < 4; ++c) dst.set(mx, my, c, 1.0f);
    src.release();
    dst.release();
    return kOfxStatOK;
}

OfxStatus gainMain(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle)
{
    OfxImageEffectHandle effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0) return loadSuites();
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        const char* contexts[] = {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral};
        return describeCommon(effect, "Test Gain", "OpenVegas Tests/Colour", contexts, 2);
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) return gainDescribeInContext(effect);
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0) return gainRender(effect, in);
    return kOfxStatReplyDefault;
}

// ------------------------------------------------------------------ Wipe

OfxStatus wipeDescribeInContext(OfxImageEffectHandle effect)
{
    defineClip(effect, kOfxImageEffectTransitionSourceFromClipName);
    defineClip(effect, kOfxImageEffectTransitionSourceToClipName);
    defineClip(effect, kOfxImageEffectOutputClipName);
    OfxParamSetHandle params = nullptr;
    g_effects->getParamSet(effect, &params);
    OfxPropertySetHandle p = nullptr;
    g_params->paramDefine(params, kOfxParamTypeDouble, kOfxImageEffectTransitionParamName, &p);
    g_params->paramDefine(params, kOfxParamTypeBoolean, "reverse", &p);
    g_props->propSetString(p, kOfxPropLabel, 0, "Reverse");
    g_props->propSetInt(p, kOfxParamPropDefault, 0, 0);
    return kOfxStatOK;
}

OfxStatus wipeRender(OfxImageEffectHandle effect, OfxPropertySetHandle in)
{
    OfxTime time = 0;
    g_props->propGetDouble(in, kOfxPropTime, 0, &time);
    int window[4] = {};
    g_props->propGetIntN(in, kOfxImageEffectPropRenderWindow, 4, window);
    OfxImageClipHandle fromClip = nullptr, toClip = nullptr, outputClip = nullptr;
    g_effects->clipGetHandle(effect, kOfxImageEffectTransitionSourceFromClipName, &fromClip, nullptr);
    g_effects->clipGetHandle(effect, kOfxImageEffectTransitionSourceToClipName, &toClip, nullptr);
    g_effects->clipGetHandle(effect, kOfxImageEffectOutputClipName, &outputClip, nullptr);
    OfxParamSetHandle params = nullptr;
    g_effects->getParamSet(effect, &params);
    OfxParamHandle transitionParam = nullptr, reverseParam = nullptr;
    g_params->paramGetHandle(params, kOfxImageEffectTransitionParamName, &transitionParam, nullptr);
    g_params->paramGetHandle(params, "reverse", &reverseParam, nullptr);
    double position = 0.0;
    int reverse = 0;
    g_params->paramGetValueAtTime(transitionParam, time, &position);
    g_params->paramGetValueAtTime(reverseParam, time, &reverse);
    View from, to, dst;
    if (!from.fetch(fromClip, time) || !to.fetch(toClip, time) || !dst.fetch(outputClip, time)) {
        from.release(); to.release(); dst.release();
        return kOfxStatFailed;
    }
    const int width = dst.x2 - dst.x1;
    const int edge = dst.x1 + int(std::lround(position * width));
    for (int y = window[1]; y < window[3]; ++y) {
        for (int x = window[0]; x < window[2]; ++x) {
            const bool showTo = reverse ? x >= dst.x2 - (edge - dst.x1) : x < edge;
            const View& source = showTo ? to : from;
            for (int c = 0; c < 4; ++c) dst.set(x, y, c, source.get(x, y, c));
        }
    }
    from.release(); to.release(); dst.release();
    return kOfxStatOK;
}

OfxStatus wipeMain(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle)
{
    OfxImageEffectHandle effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0) return loadSuites();
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        const char* contexts[] = {kOfxImageEffectContextTransition};
        return describeCommon(effect, "Test Wipe", "OpenVegas Tests", contexts, 1);
    }
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) return wipeDescribeInContext(effect);
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0) return wipeRender(effect, in);
    return kOfxStatReplyDefault;
}

// ----------------------------------------------------------------- Crash

OfxStatus crashMain(const char* action, const void*, OfxPropertySetHandle, OfxPropertySetHandle)
{
    if (std::strcmp(action, kOfxActionLoad) == 0) return loadSuites();
    if (std::strcmp(action, kOfxActionDescribe) == 0) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
    return kOfxStatReplyDefault;
}

#ifdef OPENVEGAS_OFX_CRASH
OfxPlugin g_plugins[] = {
    {kOfxImageEffectPluginApi, 1, "org.openvegas.test.Crash", 1, 0, setHost, crashMain},
};
#else
OfxPlugin g_plugins[] = {
    {kOfxImageEffectPluginApi, 1, "org.openvegas.test.Gain", 2, 1, setHost, gainMain},
    {kOfxImageEffectPluginApi, 1, "org.openvegas.test.Wipe", 1, 0, setHost, wipeMain},
};
#endif

} // namespace

OfxExport int OfxGetNumberOfPlugins(void)
{
    return int(sizeof g_plugins / sizeof g_plugins[0]);
}

OfxExport OfxPlugin* OfxGetPlugin(int nth)
{
    return nth >= 0 && nth < OfxGetNumberOfPlugins() ? &g_plugins[nth] : nullptr;
}
