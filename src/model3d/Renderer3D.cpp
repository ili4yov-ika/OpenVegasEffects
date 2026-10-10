#include "model3d/Renderer3D.h"

#include <QPainter>
#include <QVector4D>
#include <QtMath>

#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <thread>
#include <vector>

namespace openvegas {
namespace model3d {

namespace {

// A triangle corner after transform: clip-space position plus what shading
// needs interpolated across the face.
struct Vertex
{
    QVector4D clip;
    QVector3D world;
    QVector3D normal;
    QVector2D uv;
};

// Screen-space corner: pixel x/y, 1/w for perspective-correct interpolation,
// and depth in 0..1.
struct ScreenVertex
{
    double x = 0.0;
    double y = 0.0;
    double invW = 0.0;
    double depth = 0.0;
    QVector3D world;
    QVector3D normal;
    QVector2D uv;
};

double edgeFunction(double ax, double ay, double bx, double by, double px, double py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// The texel at `uv` - v up, as model files give it - bilinear, the texture
// repeating beyond 0..1.
QColor sampleTexture(const QImage& texture, const QVector2D& uv)
{
    const int width = texture.width();
    const int height = texture.height();
    const double x = double(uv.x()) * width - 0.5;
    const double y = (1.0 - double(uv.y())) * height - 0.5;
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double tx = x - fx;
    const double ty = y - fy;
    const auto wrap = [](int i, int n) {
        i %= n;
        return i < 0 ? i + n : i;
    };
    const int x0 = wrap(int(fx), width), x1 = wrap(int(fx) + 1, width);
    const int y0 = wrap(int(fy), height), y1 = wrap(int(fy) + 1, height);
    const uchar* top = texture.constScanLine(y0);
    const uchar* bottom = texture.constScanLine(y1);
    double c[4];
    for (int k = 0; k < 4; ++k) {
        c[k] = ((top[x0 * 4 + k] * (1.0 - tx) + top[x1 * 4 + k] * tx) * (1.0 - ty)
                + (bottom[x0 * 4 + k] * (1.0 - tx) + bottom[x1 * 4 + k] * tx) * ty) / 255.0;
    }
    return QColor::fromRgbF(float(c[0]), float(c[1]), float(c[2]), float(c[3]));
}

// A pixel of a face before light: the material's colours with its maps.
struct Surface
{
    QColor diffuse;
    QColor specular;
    QColor emissive;
    QColor ambient;
    double occlusion = 1.0;
    double metallic = 0.0;
    double roughness = 1.0;
    QVector3D normal;
};

QVector4D rgba(const QColor& colour, double alpha)
{
    return QVector4D(float(colour.redF()), float(colour.greenF()), float(colour.blueF()), float(alpha));
}

const Material& materialFor(const Mesh& mesh, int index, const Material& fallback)
{
    if (index >= 0 && index < mesh.materials.size()) {
        return mesh.materials.at(index);
    }
    return fallback;
}

} // namespace

void Light::setCone(double angleDegrees, double featherPercent)
{
    // Flux.dll 0x180483820 / 0x1804a5100: half the angle in radians; the
    // whole part of the cone is (1 - feather) of it.
    const double half = angleDegrees * 0.017453292 * 0.5;
    spotOuterCosine = std::cos(half);
    spotInnerCosine = std::cos((1.0 - featherPercent / 100.0) * half);
    const double between = spotInnerCosine - spotOuterCosine;
    spotScale = between > 0.0 ? 1.0 / between : 0.0;
}

namespace {

// Flux's microfacet helpers (its lighting shader's GLSL, transcribed).
double visibilityOcclusion(double nDotL, double nDotV, double alphaRoughness)
{
    const double a2 = alphaRoughness * alphaRoughness;
    const double ggxV = nDotL * std::sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const double ggxL = nDotV * std::sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    const double ggx = ggxV + ggxL;
    return ggx > 0.0 ? 0.5 / ggx : 0.0;
}

double microfacetDistribution(double alphaRoughness, double nDotH)
{
    // A floor on the roughness keeps a mirror-smooth surface from dividing
    // zero by zero where the GPU would make it NaN.
    const double a2 = qMax(alphaRoughness * alphaRoughness, 1e-8);
    const double f = (nDotH * a2 - nDotH) * nDotH + 1.0;
    return a2 / (M_PI * f * f);
}

QVector3D schlick(const QVector3D& f0, double cosine)
{
    const float weight = float(std::pow(qBound(0.0, 1.0 - cosine, 1.0), 5.0));
    return f0 + (QVector3D(1.0f, 1.0f, 1.0f) - f0) * weight;
}

// GetPointShade: N.L * ((1 - F) * diffuse / pi + F * vis * D).
QVector3D pointShade(double nDotL, double nDotH, double nDotV, double vDotH, const QVector3D& diffuse,
                     const QVector3D& specular, double roughness)
{
    if (nDotL <= 0.0 && nDotV <= 0.0) return QVector3D();
    const double alphaRoughness = roughness * roughness;
    const QVector3D f = schlick(specular, vDotH);
    const double vis = visibilityOcclusion(nDotL, nDotV, alphaRoughness);
    const double d = microfacetDistribution(alphaRoughness, nDotH);
    const QVector3D diffuseContribution = (QVector3D(1.0f, 1.0f, 1.0f) - f) * diffuse / float(M_PI);
    const QVector3D specularContribution = f * float(vis * d);
    return (diffuseContribution + specularContribution) * float(nDotL);
}

// Cook-Torrance's Beckmann distribution and geometric attenuation.
double beckmann(double nDotH, double roughness)
{
    const double m2 = roughness * roughness;
    const double denominator1 = 4.0 * m2 * std::pow(nDotH, 4.0);
    const double r1 = denominator1 != 0.0 ? 1.0 / denominator1 : 0.0;
    const double denominator2 = m2 * nDotH * nDotH;
    const double r2 = denominator2 != 0.0 ? (nDotH * nDotH - 1.0) / denominator2 : 0.0;
    return r1 * std::exp(r2);
}

double geometricAttenuation(double nDotH, double nDotV, double nDotL, double vDotH)
{
    const double g1 = 2.0 * nDotH * nDotV / vDotH;
    const double g2 = 2.0 * nDotH * nDotL / vDotH;
    return qMin(1.0, qMin(g1, g2));
}

} // namespace

QVector4D illuminate(const Lighting& lighting, const QVector4D& ambient, const QVector4D& diffuse,
                     const QVector4D& specular, const QVector3D& emissive, const QVector3D& position,
                     const QVector3D& normal, const QVector3D& eye, const Reflectance* reflectance)
{
    // lightType[0] == 0: the surface's own colour.
    if (lighting.lights.isEmpty()) return diffuse;
    const SurfaceLighting& surface = lighting.surface;
    const IlluminationModel model = reflectance ? reflectance->model : IlluminationModel::Phong;
    const double specularShare = reflectance ? reflectance->specular : surface.specular;
    const double shininess = reflectance ? reflectance->shininess : surface.shininess;
    const QVector3D n0 = normal.isNull() ? QVector3D(0.0f, 0.0f, 1.0f) : normal.normalized();
    QVector3D toView = eye - position;
    if (!toView.isNull()) toView.normalize();
    const double nDotV = qMax(std::abs(double(QVector3D::dotProduct(n0, toView))), 0.00001);
    // The colours each light is reflected with, and the one an ambient
    // light lights: the physically based models split the base colour.
    QVector3D diffuseColour = diffuse.toVector3D();
    QVector3D specularColour = specular.toVector3D();
    QVector3D ambientColour = ambient.toVector3D();
    double roughness = 0.0;
    if (model == IlluminationModel::MetallicRoughness) {
        const double metallic = qBound(0.0, reflectance->metallic, 1.0);
        roughness = qBound(0.0, reflectance->roughness, 1.0);
        const QVector3D f0(0.04f, 0.04f, 0.04f);
        const float kD = float((1.0 - 0.04) * (1.0 - metallic));
        specularColour = f0 + (diffuseColour - f0) * float(metallic);
        diffuseColour *= kD;
        ambientColour = diffuseColour;
    } else if (model == IlluminationModel::SpecularGlossiness) {
        roughness = qBound(0.0, reflectance->roughness, 1.0);
        const float kD = 1.0f - qMax(specularColour.x(), qMax(specularColour.y(), specularColour.z()));
        diffuseColour *= kD;
        ambientColour = diffuseColour;
    }
    QVector3D sum;            // colour
    double covered = 0.0;     // what the shadows of a surface that is not illuminated cover
    for (int i = 0; i < lighting.lights.size(); ++i) {
        const Light& light = lighting.lights.at(i);
        const QVector3D lightColour(float(light.color.redF()), float(light.color.greenF()),
                                    float(light.color.blueF()));
        if (light.type == Light::Type::Ambient) {
            if (surface.illuminated)
                sum += lightColour * float(light.intensity * surface.ambient) * ambientColour;
            continue;
        }
        const QVector3D toLight = light.position - position;
        const double distance = toLight.length();
        const QVector3D ldir = light.type == Light::Type::Directional
            ? -light.direction.normalized() : toLight.normalized();
        // Billboarded Lights: the surface faces the light and is seen from
        // it (n = normalize(-lightDir), viewVector = normalize(lightDir)).
        QVector3D n = n0;
        QVector3D view = toView;
        if (surface.billboardedLights && !toLight.isNull()) {
            n = -toLight.normalized();
            view = toLight.normalized();
        }
        // Two-sided: a face is lit from whichever side the light is on (an
        // open mesh, a sheet seen from behind).
        double nDotL = QVector3D::dotProduct(n, ldir);
        if (nDotL < 0.0) {
            n = -n;
            nDotL = -nDotL;
        }
        if (nDotL <= 0.0001) continue;
        double attenuation = 1.0;
        const double reach = light.reach;
        if (light.falloff == Light::Falloff::Curve && reach > 0.0)
            attenuation = qMin((distance * distance - 2.0 * reach * distance + reach * reach) / (reach * reach), 1.0);
        else if (light.falloff == Light::Falloff::Linear && reach > 0.0)
            attenuation = qMin(1.0 - distance / reach, 1.0);
        if (light.falloff != Light::Falloff::None && distance > reach) attenuation = 0.0;
        if (light.type == Light::Type::Spot) {
            const double spotDot = QVector3D::dotProduct(-ldir, light.direction.normalized());
            if (spotDot < light.spotInnerCosine)
                attenuation *= qMax((spotDot - light.spotOuterCosine) * light.spotScale, 0.0);
        }
        QVector3D perLight;
        const QVector3D halfway = (ldir + view).normalized();
        const double nDotH = qMax(double(QVector3D::dotProduct(n, halfway)), 0.00001);
        const double vDotH = qMax(double(QVector3D::dotProduct(view, halfway)), 0.00001);
        if (model == IlluminationModel::MetallicRoughness || model == IlluminationModel::SpecularGlossiness) {
            perLight = lightColour * pointShade(qMax(nDotL, 0.00001), nDotH, nDotV, vDotH, diffuseColour,
                                                specularColour, roughness);
        } else if (model == IlluminationModel::CookTorrance) {
            const QVector3D fresnel = schlick(reflectance->cookTorranceFresnel, vDotH);
            const QVector3D term = fresnel
                * float(geometricAttenuation(nDotH, nDotV, nDotL, vDotH)
                        * beckmann(nDotH, reflectance->cookTorranceRoughness) / (nDotV * nDotL));
            perLight = lightColour * float(nDotL * surface.diffuse) * diffuseColour
                + term * specularColour * lightColour * float(specularShare * reflectance->cookTorranceSpecularReflectance);
        } else {
            // Phong.
            perLight = lightColour * float(nDotL * surface.diffuse) * diffuseColour;
            const QVector3D reflected = -ldir + n * float(2.0 * nDotL);
            const double spec = qMin(double(QVector3D::dotProduct(reflected, view)), 1.0);
            if (spec > 0.0)
                perLight += lightColour * specularColour * float(specularShare * std::pow(spec, shininess));
        }
        perLight *= float(light.intensity * attenuation);
        double shadow = 0.0;
        if (surface.receiveShadows && i < lighting.shadows.size() && lighting.shadows.at(i))
            shadow = qBound(0.0, lighting.shadows.at(i)->amountFor(position, surface.shadowBias), 1.0)
                     * light.shadowOpacity;
        const QVector3D shadowColour(float(light.shadowColor.redF()), float(light.shadowColor.greenF()),
                                     float(light.shadowColor.blueF()));
        if (surface.illuminated) {
            sum += shadowColour * float(shadow) + perLight * float(1.0 - shadow);
        } else {
            sum += shadowColour * float(shadow);
            covered += shadow;
        }
    }
    const auto clamp01 = [](const QVector3D& v) {
        return QVector3D(qBound(0.0f, v.x(), 1.0f), qBound(0.0f, v.y(), 1.0f), qBound(0.0f, v.z(), 1.0f));
    };
    if (surface.illuminated) {
        const QVector3D lit = clamp01(clamp01(sum) + emissive);
        return QVector4D(lit, diffuse.w());
    }
    // The shadows over the surface's own colour.
    const float over = float(qBound(0.0, covered, 1.0));
    return QVector4D(clamp01(sum) + diffuse.toVector3D() * (1.0f - over), diffuse.w());
}

namespace {

// One square depth map seen from the light: the distance along its axis of
// the nearest caster at each texel, infinity where there is none.
struct DepthFace
{
    QMatrix4x4 viewProjection;
    QVector3D origin;
    QVector3D axis;          // shadowDirection
    bool perspective = true;
    double texelAtUnit = 0.0;   // scene units a texel spans one unit away (perspective) or anywhere
    std::vector<float> depth;
};

class DepthShadowMap : public ShadowMap
{
public:
    DepthShadowMap(const Light& light, int size) : m_light(light), m_size(size) {}

    QVector<DepthFace> faces;

    double amountAt(const QVector3D& position) const override { return amountFor(position, 1.0); }
    double amountFor(const QVector3D& position, double bias) const override
    {
        // A point light's cube: the face the direction mostly goes along,
        // which is the one that shows the point (Flux takes the most of all
        // six, the others seeing nothing there).
        if (faces.size() == 6) {
            const QVector3D d = position - m_light.position;
            const float ax = std::abs(d.x()), ay = std::abs(d.y()), az = std::abs(d.z());
            int face = ax >= ay && ax >= az ? (d.x() >= 0 ? 0 : 1)
                     : ay >= az ? (d.y() >= 0 ? 2 : 3) : (d.z() >= 0 ? 4 : 5);
            return amountIn(faces.at(face), position, bias);
        }
        double most = 0.0;
        for (const DepthFace& face : faces) most = qMax(most, amountIn(face, position, bias));
        return most;
    }

private:
    double amountIn(const DepthFace& face, const QVector3D& position, double bias) const
    {
        // In front of the light only (dot(shadowDirection, p - light) > 0).
        const float ahead = QVector3D::dotProduct(face.axis, position - face.origin);
        if (ahead <= 0.0f) return 0.0;
        const QVector4D clip = face.viewProjection * QVector4D(position, 1.0f);
        if (clip.w() <= 0.0f) return 0.0;
        const double u = (double(clip.x()) / clip.w()) * 0.5 + 0.5;
        const double v = 0.5 - (double(clip.y()) / clip.w()) * 0.5;
        if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) return 0.0;
        // How far the surface may lie behind the nearest caster and still be
        // that caster: a texel's width there and a little more.
        const double texel = face.perspective ? face.texelAtUnit * ahead : face.texelAtUnit;
        const double receiver = double(ahead) - (qMax(0.0, bias) * (1.0 + 0.002 * ahead) + 2.0 * texel);
        const auto shadowedAt = [&](double su, double sv) {
            const int x = qBound(0, int(su * m_size), m_size - 1);
            const int y = qBound(0, int(sv * m_size), m_size - 1);
            const float stored = face.depth[size_t(y) * size_t(m_size) + size_t(x)];
            return std::isfinite(stored) && receiver > stored;
        };
        double sum = shadowedAt(u, v) ? 10.0 : 0.0;
        double counter = 10.0;
        if (u >= 0.01 && v >= 0.01 && u <= 0.99 && v <= 0.99) {
            const double radius = qMax(20.0 * m_light.shadowDiffusion, 1.0);
            const int range = int(radius * 0.5 + 0.5);
            // Flux samples every 1/2048 of the map across the square; a wide
            // one is sampled every few of those here, to keep it affordable.
            const int stride = qMax(1, (2 * range + 1 + 8) / 9);
            constexpr double kScale = 1.0 / 2048.0;
            for (int i = -range; i <= range; i += stride) {
                for (int j = -range; j <= range; j += stride) {
                    sum += shadowedAt(u + i * kScale, v + j * kScale) ? 1.0 : 0.0;
                    counter += 1.0;
                }
            }
        }
        return sum / counter;
    }

    Light m_light;
    int m_size = 0;
};

// Draws the casters' nearest distances along `face.axis` into its map.
void drawCasters(DepthFace& face, const QVector<ShadowCaster>& casters, int size)
{
    face.depth.assign(size_t(size) * size_t(size), std::numeric_limits<float>::infinity());
    const float nearest = face.perspective ? 1.0f : -std::numeric_limits<float>::max();
    for (const ShadowCaster& caster : casters) {
        const bool pictured = !caster.picture.isNull() && caster.uvs.size() == caster.corners.size();
        const QImage& picture = caster.picture;
        for (int t = 0; t + 2 < caster.corners.size(); t += 3) {
            double sx[3], sy[3], inverseW[3], distance[3];
            bool behind = false;
            for (int c = 0; c < 3; ++c) {
                const QVector3D& corner = caster.corners.at(t + c);
                const QVector4D clip = face.viewProjection * QVector4D(corner, 1.0f);
                const float along = QVector3D::dotProduct(face.axis, corner - face.origin);
                if (face.perspective && (clip.w() <= 0.0f || along < nearest)) behind = true;
                const double w = face.perspective ? double(clip.w()) : 1.0;
                inverseW[c] = 1.0 / w;
                sx[c] = (double(clip.x()) / w * 0.5 + 0.5) * size;
                sy[c] = (0.5 - double(clip.y()) / w * 0.5) * size;
                distance[c] = along;
            }
            if (behind) continue;
            const double area = edgeFunction(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
            if (qFuzzyIsNull(area)) continue;
            const int minX = qMax(0, int(std::floor(qMin(sx[0], qMin(sx[1], sx[2])))));
            const int maxX = qMin(size - 1, int(std::ceil(qMax(sx[0], qMax(sx[1], sx[2])))));
            const int minY = qMax(0, int(std::floor(qMin(sy[0], qMin(sy[1], sy[2])))));
            const int maxY = qMin(size - 1, int(std::ceil(qMax(sy[0], qMax(sy[1], sy[2])))));
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const double px = x + 0.5, py = y + 0.5;
                    double b[3] = {edgeFunction(sx[1], sy[1], sx[2], sy[2], px, py) / area,
                                   edgeFunction(sx[2], sy[2], sx[0], sy[0], px, py) / area,
                                   edgeFunction(sx[0], sy[0], sx[1], sy[1], px, py) / area};
                    if (b[0] < 0.0 || b[1] < 0.0 || b[2] < 0.0) continue;
                    // Perspective-correct: 1/w is what is linear on the map.
                    const double weight = b[0] * inverseW[0] + b[1] * inverseW[1] + b[2] * inverseW[2];
                    if (weight <= 0.0) continue;
                    double p[3];
                    for (int c = 0; c < 3; ++c) p[c] = b[c] * inverseW[c] / weight;
                    const float d = float(p[0] * distance[0] + p[1] * distance[1] + p[2] * distance[2]);
                    float& stored = face.depth[size_t(y) * size_t(size) + size_t(x)];
                    if (d >= stored) continue;
                    if (pictured) {
                        const QPointF& a = caster.uvs.at(t), &bb = caster.uvs.at(t + 1), &cc = caster.uvs.at(t + 2);
                        const int ix = qBound(0, int((p[0] * a.x() + p[1] * bb.x() + p[2] * cc.x()) * picture.width()),
                                              picture.width() - 1);
                        const int iy = qBound(0, int((p[0] * a.y() + p[1] * bb.y() + p[2] * cc.y()) * picture.height()),
                                              picture.height() - 1);
                        if (qAlpha(picture.pixel(ix, iy)) <= 127) continue;
                    }
                    stored = d;
                }
            }
        }
    }
}

QVector3D anyPerpendicular(const QVector3D& axis)
{
    const QVector3D up = std::abs(axis.y()) < 0.99f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(0.0f, 0.0f, 1.0f);
    return QVector3D::crossProduct(QVector3D::crossProduct(axis, up), axis).normalized();
}

} // namespace

std::shared_ptr<const ShadowMap> makeShadowMap(const Light& light, const QVector<ShadowCaster>& casters, int size)
{
    if (!light.castShadows || light.type == Light::Type::Ambient || casters.isEmpty() || size < 2) return nullptr;
    auto map = std::make_shared<DepthShadowMap>(light, size);
    const QVector3D direction = light.direction.isNull() ? QVector3D(0.0f, 0.0f, -1.0f) : light.direction.normalized();
    const auto perspectiveFace = [&](const QVector3D& axis, double fieldOfView) {
        DepthFace face;
        face.origin = light.position;
        face.axis = axis;
        QMatrix4x4 projection;
        projection.perspective(float(fieldOfView), 1.0f, 1.0f, 1.0e7f);
        QMatrix4x4 view;
        view.lookAt(light.position, light.position + axis, anyPerpendicular(axis));
        face.viewProjection = projection * view;
        face.texelAtUnit = 2.0 * std::tan(qDegreesToRadians(fieldOfView) / 2.0) / size;
        drawCasters(face, casters, size);
        map->faces.append(face);
    };
    if (light.type == Light::Type::Directional) {
        // Parallel, across the casters: their extent seen along the light.
        DepthFace face;
        face.perspective = false;
        face.axis = direction;
        const QVector3D up = anyPerpendicular(direction);
        const QVector3D right = QVector3D::crossProduct(direction, up).normalized();
        float lowR = std::numeric_limits<float>::max(), highR = -lowR, lowU = lowR, highU = -lowR, lowF = lowR;
        for (const ShadowCaster& caster : casters)
            for (const QVector3D& corner : caster.corners) {
                const float r = QVector3D::dotProduct(corner, right), u = QVector3D::dotProduct(corner, up);
                lowR = qMin(lowR, r); highR = qMax(highR, r); lowU = qMin(lowU, u); highU = qMax(highU, u);
                lowF = qMin(lowF, QVector3D::dotProduct(corner, direction));
            }
        const float half = qMax(1.0f, qMax(highR - lowR, highU - lowU) * 0.5f * 1.02f);
        const QVector3D middle = right * ((lowR + highR) * 0.5f) + up * ((lowU + highU) * 0.5f);
        // The light's own position decides what is in front of it, as in Flux.
        face.origin = light.position;
        const QVector3D eye = middle + direction * (lowF - 1.0f);
        QMatrix4x4 projection;
        projection.ortho(-half, half, -half, half, 0.0f, 1.0e7f);
        QMatrix4x4 view;
        view.lookAt(eye, eye + direction, up);
        face.viewProjection = projection * view;
        face.texelAtUnit = 2.0 * half / size;
        drawCasters(face, casters, size);
        // Distances from the light's plane, as amountIn measures them.
        map->faces.append(face);
    } else if (light.type == Light::Type::Spot && light.spotOuterCosine > 0.867) {
        perspectiveFace(direction, qRadiansToDegrees(2.0 * std::acos(light.spotOuterCosine)) * 1.05);
    } else {
        for (const QVector3D& axis : {QVector3D(1, 0, 0), QVector3D(-1, 0, 0), QVector3D(0, 1, 0),
                                      QVector3D(0, -1, 0), QVector3D(0, 0, 1), QVector3D(0, 0, -1)})
            perspectiveFace(axis, 90.0);
    }
    return map;
}

QMatrix4x4 Camera::viewProjection(const QSize& canvas) const
{
    const double aspect = canvas.height() > 0
        ? double(canvas.width()) / double(canvas.height())
        : 1.0;
    QMatrix4x4 projection;
    if (orthographic && orthographicHeight > 0.0) {
        const float halfHeight = float(orthographicHeight * 0.5);
        const float halfWidth = float(halfHeight * aspect);
        projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, float(nearPlane), float(farPlane));
    } else {
        projection.perspective(float(fieldOfViewDegrees), float(aspect), float(nearPlane),
                               float(farPlane));
    }
    QMatrix4x4 view;
    view.lookAt(position, target, up);
    return projection * view;
}

Camera defaultCameraFor(const QVector3D& centre, double radius, const QSize& canvas)
{
    Camera camera;
    camera.target = centre;
    // Back off far enough that a sphere of this radius fits the vertical field
    // of view, with a little air around it.
    const double halfFov = camera.fieldOfViewDegrees * 0.5 * M_PI / 180.0;
    const double distance = radius > 0.0 ? (radius * 1.6) / std::sin(halfFov) : 1000.0;
    camera.position = centre + QVector3D(0.0f, 0.0f, float(distance));
    Q_UNUSED(canvas);
    return camera;
}

double DepthOfField::blurTerm(double eyeDepth) const
{
    if (!enabled || maxCoC <= 0.0 || pixelsPerMM <= 0.0 || focalLength <= 0.0 || focusDistance <= 0.0) return 0.0;
    constexpr double kLensDistance = 50.25;
    const double zScale = 1.0 / pixelsPerMM;
    const double inverse = 1.0 / focalLength - 1.0 / focusDistance;
    if (inverse == 0.0) return 0.0;
    const double zFocus = 1.0 / inverse + focusDistance;
    const double d = std::abs(eyeDepth) * zScale + kLensDistance;
    const double denominator = zFocus * (d - focalLength);
    const double coc = denominator != 0.0 ? std::abs(lensDiameter * focalLength * (zFocus - d) / denominator) : 0.0;
    return qBound(0.0, qBound(0.0, coc * pixelsPerMM * blurAmount, maxCoC) / maxCoC, 1.0);
}

void applyDepthOfField(QImage& premultiplied, const QVector<float>& distances, const Camera& camera,
                       const DepthOfField& dof, double scale)
{
    const int width = premultiplied.width(), height = premultiplied.height();
    if (!dof.enabled || width <= 0 || height <= 0 || distances.size() != width * height) return;
    // The blur term of every pixel, by its depth along the camera's axis.
    const QMatrix4x4 viewProjection = camera.viewProjection(premultiplied.size());
    bool invertible = false;
    const QMatrix4x4 unproject = viewProjection.inverted(&invertible);
    if (!invertible) return;
    const QVector3D forward = (camera.target - camera.position).normalized();
    std::vector<float> blur(size_t(width) * size_t(height), 0.0f);
    bool any = false;
    for (int y = 0; y < height; ++y) {
        const double ndcY = 1.0 - 2.0 * (y + 0.5) / height;
        for (int x = 0; x < width; ++x) {
            const float distance = distances.at(y * width + x);
            if (!std::isfinite(distance)) continue;
            const double ndcX = 2.0 * (x + 0.5) / width - 1.0;
            const QVector3D ray = (unproject.map(QVector3D(float(ndcX), float(ndcY), 1.0f)) - camera.position).normalized();
            const double depth = double(distance) * double(QVector3D::dotProduct(ray, forward));
            const float term = float(dof.blurTerm(depth));
            blur[size_t(y) * size_t(width) + size_t(x)] = term;
            any = any || term > 0.0f;
        }
    }
    if (!any) return;
    // Each pixel's reach: the scene's own pixels their neighbourhood's mean
    // blur (as Flux's BoxBlurPass); the empty ones around them the largest
    // blur within reach, so a blurred surface's edge spreads into what is
    // not the scene and fades there.
    const double maxCoC = dof.maxCoC * scale;
    const auto index = [width](int x, int y) { return size_t(y) * size_t(width) + size_t(x); };
    std::vector<char> inScene(blur.size(), 0);
    for (size_t i = 0; i < blur.size(); ++i) inScene[i] = std::isfinite(distances.at(int(i))) ? 1 : 0;
    std::vector<float> reach(blur.size(), 0.0f);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            double sum = 0.0;
            int count = 0;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const int sx = x + i, sy = y + j;
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                    sum += blur[index(sx, sy)];
                    ++count;
                }
            reach[index(x, y)] = float(count ? sum / count : 0.0);
        }
    // The largest blur within the widest circle, across then down.
    const int widest = int(std::ceil(maxCoC * 0.5));
    std::vector<float> spread(blur), across(blur.size(), 0.0f);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float most = 0.0f;
            for (int i = -widest; i <= widest; ++i) {
                const int sx = x + i;
                if (sx < 0 || sx >= width) continue;
                const float b = blur[index(sx, y)];
                if (b * maxCoC * 0.5 >= std::abs(i)) most = qMax(most, b);
            }
            across[index(x, y)] = most;
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float most = 0.0f;
            for (int j = -widest; j <= widest; ++j) {
                const int sy = y + j;
                if (sy < 0 || sy >= height) continue;
                const float b = across[index(x, sy)];
                if (b * maxCoC * 0.5 >= std::abs(j)) most = qMax(most, b);
            }
            spread[index(x, y)] = most;
        }
    for (size_t i = 0; i < reach.size(); ++i)
        if (!inScene[i]) reach[i] = spread[i];
    // Across, then down: every tap weighted by its own blur - an empty one by
    // the blur of the pixel being made.
    std::vector<float> pixels(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        const uchar* row = premultiplied.constScanLine(y);
        for (int x = 0; x < width * 4; ++x) pixels[size_t(y) * size_t(width) * 4 + size_t(x)] = row[x];
    }
    std::vector<float> result(pixels.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const size_t at = index(x, y);
                const float own = reach[at];
                const int radius = int(std::floor(own * maxCoC * 0.5));
                float colour[4] = {0, 0, 0, 0};
                float total = 0.0f;
                if (radius >= 1) {
                    for (int i = -radius; i <= radius; ++i) {
                        const int sx = pass == 0 ? qBound(0, x + i, width - 1) : x;
                        const int sy = pass == 1 ? qBound(0, y + i, height - 1) : y;
                        const size_t tap = index(sx, sy);
                        const float weight = inScene[tap] ? blur[tap] : own;
                        if (weight <= 0.0f) continue;
                        for (int c = 0; c < 4; ++c) colour[c] += pixels[tap * 4 + size_t(c)] * weight;
                        total += weight;
                    }
                }
                for (int c = 0; c < 4; ++c)
                    result[at * 4 + size_t(c)] = total > 0.0f ? colour[c] / total : pixels[at * 4 + size_t(c)];
            }
        pixels.swap(result);
    }
    for (int y = 0; y < height; ++y) {
        uchar* row = premultiplied.scanLine(y);
        for (int x = 0; x < width * 4; ++x)
            row[x] = uchar(qBound(0, int(std::lround(pixels[size_t(y) * size_t(width) * 4 + size_t(x)])), 255));
    }
    // Premultiplied colour never above its alpha.
    for (int y = 0; y < height; ++y) {
        uchar* row = premultiplied.scanLine(y);
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) row[x * 4 + c] = qMin(row[x * 4 + c], row[x * 4 + 3]);
    }
}

Camera defaultCameraForCanvas(const QSize& canvas)
{
    // The reference's "New Camera": a 35 mm lens (54.4322 degrees across)
    // standing its zoom - the distance at which the frame fills it - back.
    Camera camera;
    const double halfHeight = qMax(1, canvas.height()) * 0.5;
    const double distance = qMax(1, canvas.width()) / (2.0 * std::tan(54.4322 * M_PI / 360.0));
    camera.fieldOfViewDegrees = 2.0 * std::atan(halfHeight / distance) * 180.0 / M_PI;
    camera.position = QVector3D(0.0f, 0.0f, float(distance));
    camera.target = QVector3D(0.0f, 0.0f, 0.0f);
    return camera;
}

double Fog::factor(double distance) const
{
    if (!enabled) return 1.0;
    double f = 1.0;
    switch (falloff) {
    case Falloff::Linear:
        f = farDistance != nearDistance ? (farDistance - distance) / (farDistance - nearDistance)
                                        : (distance < farDistance ? 1.0 : 0.0);
        break;
    case Falloff::Exponential:
        f = std::exp(-density * distance * 0.001);
        break;
    case Falloff::ExponentialSquared:
        // As written in Flux: the 0.001 is not squared with the distance.
        f = std::exp(-density * density * distance * distance * 0.001);
        break;
    }
    return qBound(0.0, f, 1.0);
}

void Fog::apply(uchar* rgba, double distance) const
{
    const double f = factor(distance);
    if (f >= 1.0) return;
    // Straight alpha: mix(fogColor * a, c * a, f) / a.
    const int fog[3] = {color.red(), color.green(), color.blue()};
    for (int c = 0; c < 3; ++c)
        rgba[c] = uchar(qBound(0, int(std::lround(fog[c] + (rgba[c] - fog[c]) * f)), 255));
}

QImage limitTextureSize(const QImage& image, int maxSize)
{
    if (image.isNull() || maxSize <= 0 || qMax(image.width(), image.height()) <= maxSize) return image;
    const QImage scaled = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return scaled.format() == image.format() ? scaled : scaled.convertToFormat(image.format());
}

int antialiasingSamples(int mode)
{
    // FluxOpenGLInfo::GPUSupportedAntialiasingModes (Flux.dll 1802939a0) and
    // Tannen's renderbuffer setup (180369770): 4x/8x/16x/32x MSAA take that
    // many samples; the CSAA modes keep 4 (8x, 16x) or 8 (8xQ, 16xQ, 32x)
    // colour samples, their extra coverage samples left to the NVIDIA driver.
    switch (mode) {
    case 2: return 8;
    case 3: return 16;
    case 4: return 32;
    case 6: case 8: case 9: return 8;
    default: return 4;
    }
}

QVector<QPointF> samplePositions(int samples)
{
    // Offsets from the pixel's centre in 1/16 pixel: Direct3D's standard
    // patterns, where GPUs put the samples of 4x, 8x and 16x MSAA.
    static const int k4[][2] = {{-2, -6}, {6, -2}, {-6, 2}, {2, 6}};
    static const int k8[][2] = {{1, -3}, {-1, 3}, {5, 1}, {-3, -5},
                                {-5, 5}, {-7, -1}, {3, 7}, {7, -7}};
    static const int k16[][2] = {{1, 1}, {-1, -3}, {-3, 2}, {4, -1}, {-5, -2}, {2, 5},
                                 {5, 3}, {3, -5}, {-2, 6}, {0, -7}, {-4, -6}, {-6, 4},
                                 {-8, 0}, {7, -4}, {6, 7}, {-7, -8}};
    QVector<QPointF> positions;
    const auto table = [&](const int (*offsets)[2], int count) {
        for (int i = 0; i < count; ++i)
            positions << QPointF(0.5 + offsets[i][0] / 16.0, 0.5 + offsets[i][1] / 16.0);
    };
    const int n = qBound(1, samples, kMaxSamples);
    if (n == 1) {
        positions << QPointF(0.5, 0.5);
    } else if (n == 4) {
        table(k4, 4);
    } else if (n == 8) {
        table(k8, 8);
    } else if (n == 16) {
        table(k16, 16);
    } else {
        // Any other count: one sample in each row and each column of an n x n
        // grid, the columns stepped by a number prime to n (n-rooks), which
        // spreads them over the pixel like a rotated grid.
        int step = qMax(1, int(std::lround(n * 0.382)));
        while (std::gcd(step, n) != 1) ++step;
        for (int i = 0; i < n; ++i)
            positions << QPointF(((i * step) % n + 0.5) / n, (i + 0.5) / n);
    }
    return positions;
}

namespace {

// A face ready to fill: its corners on screen and what shading needs.
struct Face
{
    ScreenVertex screen[3];
    QVector3D normal;
    double invArea = 0.0;
    const Material* material = nullptr;
    // Whether every corner has a UV, so the material's maps apply.
    bool mapped = false;
    // The UVs' directions on the face, in world space, for the normal and
    // bump maps.
    QVector3D tangent;
    QVector3D bitangent;
    double alpha = 0.0;
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
};

// The image is filled tile by tile, each with its own per-sample depth and
// colour, so 32 samples cost a few megabytes, not gigabytes, and tiles go
// to separate threads.
constexpr int kTile = 64;

quint32 pack(const uchar* rgba)
{
    return quint32(rgba[0]) | quint32(rgba[1]) << 8 | quint32(rgba[2]) << 16
           | quint32(rgba[3]) << 24;
}

// The material's maps at `uv`: colours multiplied, the normal turned.
void applyMaps(const Material& material, const Face& face, const QVector2D& uv, Surface* surface,
               double* alpha)
{
    const auto times = [](const QColor& a, const QColor& b) {
        return QColor::fromRgbF(a.redF() * b.redF(), a.greenF() * b.greenF(), a.blueF() * b.blueF());
    };
    if (!material.diffuseImage.isNull()) {
        const QColor texel = sampleTexture(material.diffuseImage, uv);
        surface->diffuse = times(surface->diffuse, texel);
        surface->ambient = times(surface->ambient, texel);
        if (material.alphaMode != AlphaMode::Opaque) *alpha *= texel.alphaF();
    }
    if (!material.specularImage.isNull())
        surface->specular = times(surface->specular, sampleTexture(material.specularImage, uv));
    if (!material.emissiveImage.isNull()) {
        // A black emissive colour with an emissive map: the map alone (a
        // layer lent to light a screen should glow by itself).
        const QColor base = surface->emissive.rgb() == qRgb(0, 0, 0) ? QColor(Qt::white) : surface->emissive;
        surface->emissive = times(base, sampleTexture(material.emissiveImage, uv));
    }
    if (!material.occlusionImage.isNull())
        surface->occlusion = sampleTexture(material.occlusionImage, uv).redF();
    if (!material.metallicImage.isNull() || !material.roughnessImage.isNull()) {
        // glTF's metallic-roughness texture packs roughness in G and metallic
        // in B. Separate maps use their red channel, which is also the common
        // convention for grayscale PBR textures.
        const QColor packed = !material.metallicImage.isNull()
            ? sampleTexture(material.metallicImage, uv) : QColor();
        const QColor rough = !material.roughnessImage.isNull()
            ? sampleTexture(material.roughnessImage, uv) : QColor();
        if (!material.metallicImage.isNull())
            surface->metallic *= material.metallicRoughnessPacked ? packed.blueF() : packed.redF();
        if (!material.roughnessImage.isNull())
            surface->roughness *= material.metallicRoughnessPacked ? rough.greenF() : rough.redF();
    }
    if (!material.specularGlossinessImage.isNull()) {
        // roughness = 1 - clamp(map alpha * glossiness factor, 0, 1).
        const QColor specGloss = sampleTexture(material.specularGlossinessImage, uv);
        surface->specular = times(surface->specular, specGloss);
        surface->roughness = 1.0 - qBound(0.0, specGloss.alphaF() * material.glossiness, 1.0);
    }
    if (material.normalImage.isNull() && material.bumpImage.isNull()) return;
    QVector3D n = surface->normal.normalized();
    if (n.isNull() || face.tangent.isNull() || face.bitangent.isNull()) return;
    // Gram-Schmidt: the tangent square to the normal, the bitangent from them
    // with the handedness the UVs have.
    QVector3D t = (face.tangent - n * QVector3D::dotProduct(n, face.tangent)).normalized();
    const float handed = QVector3D::dotProduct(QVector3D::crossProduct(n, t), face.bitangent) < 0.0f ? -1.0f : 1.0f;
    QVector3D b = QVector3D::crossProduct(n, t) * handed;
    if (!material.bumpImage.isNull()) {
        // The height's slope along u and v, a texel apart, tilts the normal.
        const QSizeF size = material.bumpImage.size();
        const float du = float(1.0 / size.width()), dv = float(1.0 / size.height());
        const double h = sampleTexture(material.bumpImage, uv).redF();
        const double hu = sampleTexture(material.bumpImage, uv + QVector2D(du, 0)).redF();
        const double hv = sampleTexture(material.bumpImage, uv + QVector2D(0, dv)).redF();
        const float strength = float(material.bumpScale);
        n = (n - t * float((hu - h) * strength) * float(size.width()) / 32.0f
               - b * float((hv - h) * strength) * float(size.height()) / 32.0f).normalized();
        t = (t - n * QVector3D::dotProduct(n, t)).normalized();
        b = QVector3D::crossProduct(n, t) * handed;
    }
    if (!material.normalImage.isNull()) {
        const QColor texel = sampleTexture(material.normalImage, uv);
        const QVector3D m(float(texel.redF() * 2.0 - 1.0), float(texel.greenF() * 2.0 - 1.0),
                          float(texel.blueF() * 2.0 - 1.0));
        const QVector3D turned = (t * m.x() + b * m.y() + n * m.z()).normalized();
        if (!turned.isNull()) n = turned;
    }
    surface->normal = n;
}

void fillTile(const QVector<Face>& faces, const QVector<int>& order, const QRect& tile,
              const QVector<QPointF>& offsets, const Camera& camera, const Lighting& lighting,
              const Fog& fog, uchar* bits, qsizetype bytesPerLine)
{
    const int count = offsets.size();
    const int tileWidth = tile.width();
    const size_t samples = size_t(tileWidth) * size_t(tile.height()) * size_t(count);
    std::vector<double> depthBuffer(samples, std::numeric_limits<double>::infinity());
    std::vector<quint32> colours(samples, 0u);

    for (int index : order) {
        const Face& face = faces.at(index);
        const ScreenVertex* screen = face.screen;
        const int minX = qMax(tile.left(), face.minX);
        const int maxX = qMin(tile.right(), face.maxX);
        const int minY = qMax(tile.top(), face.minY);
        const int maxY = qMin(tile.bottom(), face.maxY);
        const auto weights = [&](double px, double py, double* w) {
            w[0] = edgeFunction(screen[1].x, screen[1].y, screen[2].x, screen[2].y, px, py)
                   * face.invArea;
            w[1] = edgeFunction(screen[2].x, screen[2].y, screen[0].x, screen[0].y, px, py)
                   * face.invArea;
            w[2] = edgeFunction(screen[0].x, screen[0].y, screen[1].x, screen[1].y, px, py)
                   * face.invArea;
        };
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const size_t first = (size_t(y - tile.top()) * size_t(tileWidth)
                                      + size_t(x - tile.left())) * size_t(count);
                quint32 won = 0;
                double sumX = 0.0;
                double sumY = 0.0;
                int covered = 0;
                double before[kMaxSamples];
                for (int s = 0; s < count; ++s) {
                    const double px = x + offsets.at(s).x();
                    const double py = y + offsets.at(s).y();
                    double w[3];
                    weights(px, py, w);
                    if (w[0] < 0.0 || w[1] < 0.0 || w[2] < 0.0) continue;
                    const double depth =
                        w[0] * screen[0].depth + w[1] * screen[1].depth + w[2] * screen[2].depth;
                    double& stored = depthBuffer[first + size_t(s)];
                    if (depth >= stored) continue;
                    before[s] = stored;
                    stored = depth;
                    won |= 1u << s;
                    sumX += px;
                    sumY += py;
                    ++covered;
                }
                if (!covered) continue;

                // Shaded once for the pixel, at the middle of the samples
                // the face won (centroid sampling): inside the face, so an
                // edge pixel is not lit from beyond it. Everything
                // interpolated in world space is perspective-correct.
                double w[3];
                weights(sumX / covered, sumY / covered, w);
                const double invW = w[0] * screen[0].invW + w[1] * screen[1].invW
                                    + w[2] * screen[2].invW;
                QVector3D normal = face.normal;
                QVector3D world;
                if (invW > 0.0) {
                    const double p0 = w[0] * screen[0].invW / invW;
                    const double p1 = w[1] * screen[1].invW / invW;
                    const double p2 = w[2] * screen[2].invW / invW;
                    world = screen[0].world * float(p0) + screen[1].world * float(p1)
                        + screen[2].world * float(p2);
                    const QVector3D interpolated = screen[0].normal * float(p0)
                        + screen[1].normal * float(p1) + screen[2].normal * float(p2);
                    if (!interpolated.isNull()) normal = interpolated;
                }
                const Material& material = *face.material;
                // Specular-Glossiness: rough by what the glossiness leaves.
                const double roughness = material.illuminationModel == IlluminationModel::SpecularGlossiness
                    ? 1.0 - qBound(0.0, material.glossiness, 1.0) : material.roughness;
                Surface surface {material.diffuseColor, material.specularColor,
                                 material.emissiveColor, material.ambientColor, 1.0,
                                 material.metallic, roughness, normal};
                double alpha = face.alpha;
                if (face.mapped && invW > 0.0) {
                    const double p0 = w[0] * screen[0].invW / invW;
                    const double p1 = w[1] * screen[1].invW / invW;
                    const double p2 = w[2] * screen[2].invW / invW;
                    const QVector2D uv = screen[0].uv * float(p0) + screen[1].uv * float(p1)
                        + screen[2].uv * float(p2);
                    applyMaps(material, face, uv, &surface, &alpha);
                }
                // Mask: below Alpha Cut Off the surface is not there - its
                // samples give back the depth they took.
                if (material.alphaMode == AlphaMode::Mask) {
                    if (alpha < material.alphaCutOff) {
                        for (int s = 0; s < count; ++s)
                            if (won & (1u << s)) depthBuffer[first + size_t(s)] = before[s];
                        continue;
                    }
                    alpha = face.alpha;   // and wholly there above it
                }
                // Flux's model shader: the ambient colour times the diffuse
                // map too; the material's own illumination model, with its
                // Shininess as the exponent and its specular colour whole.
                Reflectance reflectance;
                reflectance.model = material.illuminationModel;
                reflectance.shininess = qMax(0.0, material.shininess);
                reflectance.roughness = surface.roughness;
                reflectance.metallic = surface.metallic;
                reflectance.cookTorranceRoughness = material.cookTorranceRoughness;
                reflectance.cookTorranceSpecularReflectance = material.cookTorranceSpecularReflectance;
                reflectance.cookTorranceFresnel = material.cookTorranceFresnel;
                const QColor& emissive = surface.emissive;
                QVector4D lit = illuminate(
                    lighting, rgba(surface.ambient, alpha), rgba(surface.diffuse, alpha),
                    rgba(surface.specular, alpha),
                    QVector3D(float(emissive.redF() + lighting.surface.emissive.redF()),
                              float(emissive.greenF() + lighting.surface.emissive.greenF()),
                              float(emissive.blueF() + lighting.surface.emissive.blueF())),
                    world, surface.normal, camera.position, &reflectance);
                // OcclusionTerm: the occlusion map darkens the result by
                // Occlusion Strength.
                if (surface.occlusion < 1.0) {
                    const float keep = float(1.0 - (1.0 - surface.occlusion) * qBound(0.0, material.occlusionStrength, 1.0));
                    lit = QVector4D(lit.toVector3D() * keep, lit.w());
                }
                const auto byte = [](float v) { return uchar(qBound(0, int(v * 255.0f + 0.5f), 255)); };
                uchar pixel[4] = {byte(lit.x()), byte(lit.y()), byte(lit.z()),
                                  uchar(qBound(0, int(alpha * 255.0 + 0.5), 255))};
                if (fog.enabled) fog.apply(pixel, double((world - camera.position).length()));
                const quint32 colour = pack(pixel);
                for (int s = 0; s < count; ++s) {
                    if (won & (1u << s)) colours[first + size_t(s)] = colour;
                }
            }
        }
    }

    // Resolve: each pixel is the average of its samples, alpha-weighted so
    // the clear ones around an edge do not darken it.
    for (int y = tile.top(); y <= tile.bottom(); ++y) {
        uchar* row = bits + qsizetype(y) * bytesPerLine;
        for (int x = tile.left(); x <= tile.right(); ++x) {
            const size_t first = (size_t(y - tile.top()) * size_t(tileWidth)
                                  + size_t(x - tile.left())) * size_t(count);
            uchar* pixel = row + size_t(x) * 4;
            if (count == 1) {
                const quint32 colour = colours[first];
                for (int c = 0; c < 4; ++c) pixel[c] = uchar(colour >> (8 * c));
                continue;
            }
            double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
            for (int s = 0; s < count; ++s) {
                const quint32 colour = colours[first + size_t(s)];
                const double alpha = double(colour >> 24);
                if (alpha <= 0.0) continue;
                r += double(colour & 0xff) * alpha;
                g += double((colour >> 8) & 0xff) * alpha;
                b += double((colour >> 16) & 0xff) * alpha;
                a += alpha;
            }
            if (a <= 0.0) continue;
            pixel[0] = uchar(qBound(0, int(std::lround(r / a)), 255));
            pixel[1] = uchar(qBound(0, int(std::lround(g / a)), 255));
            pixel[2] = uchar(qBound(0, int(std::lround(b / a)), 255));
            pixel[3] = uchar(qBound(0, int(std::lround(a / count)), 255));
        }
    }
}

} // namespace

QImage renderMesh(const Mesh& mesh, const QMatrix4x4& modelMatrix, const Camera& camera,
                  const QSize& canvas, const Lighting& lighting, ShadingMode mode, double opacity,
                  const Fog& fog, int samples)
{
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    if (mesh.isEmpty() || canvas.isEmpty()) {
        return image;
    }

    const QMatrix4x4 viewProjection = camera.viewProjection(canvas);
    const QMatrix4x4 mvp = viewProjection * modelMatrix;
    // Normals transform by the inverse transpose, not by the model matrix:
    // a non-uniform scale would otherwise tilt them away from the surface.
    // mapVector applies the 3x3 part, leaving the translation out.
    bool invertible = false;
    const QMatrix4x4 inverted = modelMatrix.inverted(&invertible);
    const QMatrix4x4 normalMatrix = invertible ? inverted.transposed() : modelMatrix;

    const int width = canvas.width();
    const int height = canvas.height();

    const Material defaultMaterial;
    const double alpha = qBound(0.0, opacity, 1.0);

    QPainter wirePainter;
    if (mode == ShadingMode::Wireframe) {
        wirePainter.begin(&image);
        wirePainter.setRenderHint(QPainter::Antialiasing, true);
        wirePainter.setPen(QPen(QColor(220, 220, 230, int(alpha * 255.0)), 1.0));
    }

    QVector<Face> faces;
    faces.reserve(mesh.triangles.size());
    for (const Triangle& tri : mesh.triangles) {
        Vertex vertices[3];
        bool behindCamera = false;
        bool hasUvs = true;
        for (int c = 0; c < 3; ++c) {
            const QVector3D& local = mesh.positions.at(tri.position[c]);
            vertices[c].world = modelMatrix.map(local);
            vertices[c].clip = mvp * QVector4D(local, 1.0f);
            if (vertices[c].clip.w() <= 0.0f) {
                behindCamera = true;
            }
            const int ni = tri.normal[c];
            if (ni >= 0 && ni < mesh.normals.size()) {
                vertices[c].normal = normalMatrix.mapVector(mesh.normals.at(ni)).normalized();
            }
            const int ui = tri.uv[c];
            if (ui >= 0 && ui < mesh.uvs.size()) {
                vertices[c].uv = mesh.uvs.at(ui);
            } else {
                hasUvs = false;
            }
        }
        // Anything crossing the eye plane is dropped rather than clipped: a
        // near-plane clip would mean splitting triangles, and for a preview the
        // model is in front of the camera anyway.
        if (behindCamera) {
            continue;
        }

        Face face;
        ScreenVertex* screen = face.screen;
        for (int c = 0; c < 3; ++c) {
            const QVector4D& clip = vertices[c].clip;
            const double invW = 1.0 / double(clip.w());
            const double ndcX = double(clip.x()) * invW;
            const double ndcY = double(clip.y()) * invW;
            const double ndcZ = double(clip.z()) * invW;
            screen[c].x = (ndcX * 0.5 + 0.5) * width;
            screen[c].y = (1.0 - (ndcY * 0.5 + 0.5)) * height;
            screen[c].invW = invW;
            screen[c].depth = ndcZ;
            screen[c].world = vertices[c].world;
            screen[c].normal = vertices[c].normal;
            screen[c].uv = vertices[c].uv;
        }

        if (mode == ShadingMode::Wireframe) {
            for (int c = 0; c < 3; ++c) {
                const ScreenVertex& a = screen[c];
                const ScreenVertex& b = screen[(c + 1) % 3];
                wirePainter.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
            }
            continue;
        }

        // Face normal, used when the file carried none for this corner.
        face.normal = QVector3D::crossProduct(screen[1].world - screen[0].world,
                                              screen[2].world - screen[0].world);
        if (!face.normal.isNull()) {
            face.normal.normalize();
        }

        const double area = edgeFunction(screen[0].x, screen[0].y, screen[1].x, screen[1].y,
                                         screen[2].x, screen[2].y);
        if (qFuzzyIsNull(area)) {
            continue;
        }
        face.invArea = 1.0 / area;

        face.minX = qMax(0, int(std::floor(qMin(screen[0].x, qMin(screen[1].x, screen[2].x)))));
        face.maxX = qMin(width - 1, int(std::ceil(qMax(screen[0].x, qMax(screen[1].x, screen[2].x)))));
        face.minY = qMax(0, int(std::floor(qMin(screen[0].y, qMin(screen[1].y, screen[2].y)))));
        face.maxY = qMin(height - 1, int(std::ceil(qMax(screen[0].y, qMax(screen[1].y, screen[2].y)))));
        if (face.minX > face.maxX || face.minY > face.maxY) {
            continue;
        }

        face.material = &materialFor(mesh, tri.material, defaultMaterial);
        face.mapped = hasUvs;
        if (hasUvs) {
            // The directions u and v grow in on the face (Lengyel's tangent
            // basis), which a tangent-space normal map is written against.
            const QVector3D e1 = screen[1].world - screen[0].world;
            const QVector3D e2 = screen[2].world - screen[0].world;
            const QVector2D d1 = screen[1].uv - screen[0].uv;
            const QVector2D d2 = screen[2].uv - screen[0].uv;
            const float det = d1.x() * d2.y() - d2.x() * d1.y();
            if (!qFuzzyIsNull(det)) {
                face.tangent = (e1 * d2.y() - e2 * d1.y()) / det;
                face.bitangent = (e2 * d1.x() - e1 * d2.x()) / det;
            }
        }
        face.alpha = alpha * qBound(0.0, face.material->opacity, 1.0);
        if (face.alpha <= 0.0) {
            continue;
        }
        faces.append(face);
    }

    if (wirePainter.isActive()) {
        wirePainter.end();
    }
    if (faces.isEmpty()) {
        return image;
    }

    // Faces by the tiles they reach, in draw order.
    const int tilesX = (width + kTile - 1) / kTile;
    const int tilesY = (height + kTile - 1) / kTile;
    QVector<QVector<int>> bins(tilesX * tilesY);
    for (int i = 0; i < faces.size(); ++i) {
        const Face& face = faces.at(i);
        for (int ty = face.minY / kTile; ty <= face.maxY / kTile; ++ty)
            for (int tx = face.minX / kTile; tx <= face.maxX / kTile; ++tx)
                bins[ty * tilesX + tx].append(i);
    }
    QVector<int> busy;
    for (int t = 0; t < bins.size(); ++t) {
        if (!bins.at(t).isEmpty()) busy.append(t);
    }

    const QVector<QPointF> offsets = samplePositions(samples);
    uchar* bits = image.bits();
    const qsizetype bytesPerLine = image.bytesPerLine();
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int k = next.fetch_add(1); k < busy.size(); k = next.fetch_add(1)) {
            const int t = busy.at(k);
            const QRect tile = QRect((t % tilesX) * kTile, (t / tilesX) * kTile, kTile, kTile)
                                   .intersected(QRect(0, 0, width, height));
            fillTile(faces, bins.at(t), tile, offsets, camera, lighting, fog, bits, bytesPerLine);
        }
    };
    const int threads = qBound(1, int(std::thread::hardware_concurrency()),
                               qMin(int(busy.size()), 16));
    std::vector<std::thread> helpers;
    for (int i = 1; i < threads; ++i) helpers.emplace_back(work);
    work();
    for (std::thread& helper : helpers) helper.join();
    return image;
}

} // namespace model3d
} // namespace openvegas
