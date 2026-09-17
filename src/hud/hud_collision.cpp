#include "hud/hud_collision.h"

#include "core/logger.h"
#include "core/settings.h"
#include "libwwhd/libwwhd.h"
#include "render/scene_depth.h"
#include "tools/flycam.h"

#include "imgui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Hud {
namespace Collision {
static const int   kMaxShapes   = WWHD_CCS_ALL_MAX;
static const int   kMaxMass     = 640;
static const int   kMaxMeshTris = 8000;
static const int   kMeshBuckets = 24;
static const int   kRingSegs    = 16;
static const int   kCircleSegs  = 24;
static const float kNearClip    = 8.0f;
static const float kDepthBias   = 2.0f;
static const float kMeshCutoffMin = 200.0f;
static const int   kMassSegs    = 8;
static const float kLineWidth   = 1.5f;
static const float kEdgeWidth   = 1.0f;
static const float kDegToRad    = 3.14159265f / 180.0f;
static const float kTwoPi       = 6.28318531f;

enum Kind { KIND_AABB = 0, KIND_CYL, KIND_SPH, KIND_CPS };
enum Role { ROLE_AT = 1, ROLE_TG = 2, ROLE_CO = 4 };
enum Cls  { CLS_GROUND = 0, CLS_WALL, CLS_ROOF };

struct Shape {
    u8    kind;
    u8    roles;
    u8    hits;
    u8    own;
    float v[8];
};

struct Mass {
    cXyz  pos;
    float radius, height;
    u8    hits;
};

struct View {
    cXyz  eye, center, up;
    float fovy;
    float nearZ, farZ;
    float m22, m23, m32, m33;
    bool  glClip;
    bool  valid;
    bool  projValid;
};

struct Basis {
    cXyz  eye, side, up, fwd;
    float tanHalf, aspect, halfW, halfH;
};

struct Pt {
    float x, y, d;
};

struct MeshTri {
    Pt    p[4];
    float zv;
    u8    n;
    u8    cut;
    u8    cls;
    u8    through;
    int   next;
};

static Shape       s_shapes[kMaxShapes];
static int         s_count;
static wwhd_gptr_t s_pending[WWHD_CCS_ALL_MAX];
static int         s_pendingCount;
static const u8*   s_camProc;
static bool        s_camProcSeen;
static const char* s_hiddenWhy;
static View        s_view;
static u32         s_vtblCyl, s_vtblSph, s_vtblCps;
static MeshTri     s_mesh[kMaxMeshTris];
static int         s_meshCount;
static float       s_meshCutoff;
static Mass        s_mass[2][kMaxMass];
static int         s_massWrite;
static int         s_massCount;
static int         s_massBuf;
static int         s_order[kMaxShapes];
static float       s_orderKey[kMaxShapes];
static bool        s_depthMode;
static ImVec2      s_uvWhite;
static char        s_depthInfo[112] = "depth: no camera seen yet";

int ShapeCount() { return s_count; }
int MassCount() { return s_massCount; }
const char* DepthInfo() { return s_depthInfo; }
const char* HiddenReason() { return s_hiddenWhy; }

static cXyz sub(const cXyz& a, const cXyz& b) { cXyz r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
static cXyz add(const cXyz& a, const cXyz& b) { cXyz r = { a.x + b.x, a.y + b.y, a.z + b.z }; return r; }
static cXyz mul(const cXyz& a, float s)       { cXyz r = { a.x * s, a.y * s, a.z * s }; return r; }
static float dot(const cXyz& a, const cXyz& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static cXyz cross(const cXyz& a, const cXyz& b)
{
    cXyz r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}
static cXyz normalize(const cXyz& a)
{
    const float len = sqrtf(dot(a, a));
    return len > 0.0001f ? mul(a, 1.0f / len) : a;
}

static void calibrate()
{
    const u8* link = (const u8*)daPy_lk_c_getPlayer();
    if (!link)
        return;
    s_vtblCyl = *(const u32*)(link + WWHD_DAPY_OFF_CC_CYL + 0x114);
    s_vtblCps = *(const u32*)(link + WWHD_DAPY_OFF_CC_AT_CPS + 0x114);
    s_vtblSph = *(const u32*)(link + WWHD_DAPY_OFF_CC_FAN_SPH + 0x114);
}

// The stts record names the owning actor at +0x0C.
static bool ownedByLink(const cCcD_Obj* o)
{
    const u8* link = (const u8*)daPy_lk_c_getPlayer();
    if (!link || !o->mpStts)
        return false;
    const wwhd_gptr_t owner = *(const wwhd_gptr_t*)(WWHD_AT(const u8, o->mpStts) + 0x0C);
    return owner != 0 && WWHD_AT(const u8, owner) == link;
}

static void capture(const cCcD_Obj* o)
{
    u8 roles = 0, hits = 0;
    if (o->mAtSPrm & WWHD_CCD_PRM_SET) { roles |= ROLE_AT; if (o->mAtRPrm & WWHD_CCD_PRM_HIT) hits |= ROLE_AT; }
    if (o->mTgSPrm & WWHD_CCD_PRM_SET) { roles |= ROLE_TG; if (o->mTgRPrm & WWHD_CCD_PRM_HIT) hits |= ROLE_TG; }
    if (o->mCoSPrm & WWHD_CCD_PRM_SET) { roles |= ROLE_CO; if (o->mCoRPrm & WWHD_CCD_PRM_HIT) hits |= ROLE_CO; }
    if (!roles || s_count >= kMaxShapes)
        return;

    Shape& s = s_shapes[s_count];
    s.roles = roles;
    s.hits = hits;
    s.own = ownedByLink(o) ? 1 : 0;
    const u32 vt = o->mAttrVtbl;
    if (vt && vt == s_vtblCyl) {
        s.kind = KIND_CYL;
        s.v[0] = o->mShape.cyl.mCenter.x; s.v[1] = o->mShape.cyl.mCenter.y; s.v[2] = o->mShape.cyl.mCenter.z;
        s.v[3] = o->mShape.cyl.mRadius;   s.v[4] = o->mShape.cyl.mHeight;
    } else if (vt && vt == s_vtblSph) {
        s.kind = KIND_SPH;
        s.v[0] = o->mShape.sph.mCenter.x; s.v[1] = o->mShape.sph.mCenter.y; s.v[2] = o->mShape.sph.mCenter.z;
        s.v[3] = o->mShape.sph.mRadius;
    } else if (vt && vt == s_vtblCps) {
        s.kind = KIND_CPS;
        s.v[0] = o->mShape.cps.mStart.x; s.v[1] = o->mShape.cps.mStart.y; s.v[2] = o->mShape.cps.mStart.z;
        s.v[3] = o->mShape.cps.mEnd.x;   s.v[4] = o->mShape.cps.mEnd.y;   s.v[5] = o->mShape.cps.mEnd.z;
        s.v[6] = o->mShape.cps.mRadius;
    } else {
        s.kind = KIND_AABB;
        s.v[0] = o->mAabMin.x; s.v[1] = o->mAabMin.y; s.v[2] = o->mAabMin.z;
        s.v[3] = o->mAabMax.x; s.v[4] = o->mAabMax.y; s.v[5] = o->mAabMax.z;
    }
    s_count++;
}

static bool s_frozen;

void OnBeforeMove(void* ccsp)
{
    s_pendingCount = 0;
    s_frozen = !s_camProcSeen;
    if (!Config::g_settings.collisionView || !ccsp || s_frozen)
        return;
    const cCcS* ccs = (const cCcS*)ccsp;
    int n = ccs->mObjAllCount;
    if (n < 0) n = 0;
    if (n > WWHD_CCS_ALL_MAX) n = WWHD_CCS_ALL_MAX;
    memcpy(s_pending, ccs->mpObjAll, (size_t)n * sizeof(wwhd_gptr_t));
    s_pendingCount = n;
}

void OnMassCheck(void* mng, const void* pos, unsigned result)
{
    const Config::Settings& cfg = Config::g_settings;
    if (!cfg.collisionView || !cfg.collisionMass || !mng || !pos)
        return;
    if (s_massWrite >= kMaxMass)
        return;
    Mass& m = s_mass[s_massBuf][s_massWrite++];
    m.pos = *(const cXyz*)pos;
    m.radius = dCcMassS_getRadius(mng);
    m.height = dCcMassS_getHeight(mng);
    m.hits = (u8)(result & 0xFFu);
}

void OnAfterMove(void*)
{
    if (s_frozen)
        return;
    s_massCount = s_massWrite;
    s_massBuf = 1 - s_massBuf;
    s_massWrite = 0;

    s_count = 0;
    if (!Config::g_settings.collisionView)
        return;
    calibrate();
    for (int i = 0; i < s_pendingCount; ++i) {
        const cCcD_Obj* o = WWHD_AT(cCcD_Obj, s_pending[i]);
        if (o)
            capture(o);
    }
    s_pendingCount = 0;
}

void OnCameraRun(void* camera)
{
    dCamera_c* cam = (dCamera_c*)camera;
    if (!cam)
        return;
    if (cam->mPlayerIdx == 0 || !s_camProcSeen) {
        s_camProc = dCam_getProcess(cam);
        s_camProcSeen = true;
    }
}

static float ndcAt(const View& v, float zv)
{
    const float ze = -zv;
    const float w = v.m32 * ze + v.m33;
    return fabsf(w) > 1e-9f ? (v.m22 * ze + v.m23) / w : 1.0f;
}

// The title sea runs the play scene camera; only the stage name tells it apart.
static bool onTitleStage()
{
    return strcmp(dComIfGp_getCurStageName(), "sea_T") == 0;
}

static void setHidden(const char* why)
{
    const bool same = (why == nullptr) == (s_hiddenWhy == nullptr) &&
                      (why == nullptr || strcmp(why, s_hiddenWhy) == 0);
    if (same)
        return;
    s_hiddenWhy = why;
    if (Config::g_settings.collisionView)
        Logger::Log("collision viewer %s%s", why ? "hidden: " : "shown", why ? why : "");
}

static const int kFreezeFrames = 15;
static int       s_idleFrames;

// No Run this frame means a frozen world: keep the last view, hide after kFreezeFrames.
void OnFrameEnd()
{
    const bool ran = s_camProcSeen;
    s_camProcSeen = false;
    if (ran)
        s_idleFrames = 0;
    else if (s_idleFrames < kFreezeFrames)
        ++s_idleFrames;
    setHidden(s_idleFrames >= kFreezeFrames ? "camera idle"
              : onTitleStage() ? "title screen" : nullptr);
    if (!ran)
        return;
    const dCam_view_t* view = dCam_getView(s_camProc);
    const f32* m = dCam_getDeviceProjMtx(s_camProc);
    if (!view || !m)
        return;

    s_view.eye = view->mEye;
    s_view.center = view->mCenter;
    s_view.up = view->mUp;
    s_view.fovy = view->mFovy;
    s_view.nearZ = view->mNear;
    s_view.farZ = view->mFar;
    s_view.m22 = m[10];
    s_view.m23 = m[11];
    s_view.m32 = m[14];
    s_view.m33 = m[15];
    s_view.valid = s_view.fovy > 1.0f && s_view.fovy < 179.0f;
    s_view.projValid = s_view.valid && s_view.nearZ > 0.0f && s_view.farZ > s_view.nearZ &&
                       (s_view.m32 != 0.0f || s_view.m33 != 0.0f) && s_view.m22 == s_view.m22;
    if (!s_view.projValid) {
        snprintf(s_depthInfo, sizeof(s_depthInfo), "depth: projection not ready (near %.1f far %.0f)",
                 s_view.nearZ, s_view.farZ);
        return;
    }
    const float nearNdc = ndcAt(s_view, s_view.nearZ);
    const float farNdc = ndcAt(s_view, s_view.farZ);
    s_view.glClip = nearNdc < -0.5f;
    snprintf(s_depthInfo, sizeof(s_depthInfo), "depth: near %.1f far %.0f, ndc %.2f..%.2f (%s clip)",
             s_view.nearZ, s_view.farZ, nearNdc, farNdc, s_view.glClip ? "GL" : "DX");
}

static float windowDepth(float zv)
{
    float d = ndcAt(s_view, zv - kDepthBias);
    if (s_view.glClip)
        d = d * 0.5f + 0.5f;
    if (Config::g_settings.collisionDepthInvert)
        d = 1.0f - d;
    return d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
}

static float viewDepth(const Basis& b, const cXyz& p)
{
    return dot(sub(p, b.eye), b.fwd);
}

static void projectView(const Basis& b, float vx, float vy, float vz, Pt* out)
{
    out->x = b.halfW + vx / (vz * b.tanHalf * b.aspect) * b.halfW;
    out->y = b.halfH - vy / (vz * b.tanHalf) * b.halfH;
    out->d = s_depthMode ? windowDepth(vz) : 0.0f;
}

static bool project(const Basis& b, const cXyz& p, Pt* out)
{
    const cXyz d = sub(p, b.eye);
    const float z = dot(d, b.fwd);
    if (z < kNearClip)
        return false;
    projectView(b, dot(d, b.side), dot(d, b.up), z, out);
    return true;
}

static float projectedRadius(const Basis& b, const cXyz& p, float r)
{
    const float z = viewDepth(b, p);
    return z > kNearClip ? r / (z * b.tanHalf) * b.halfH : 0.0f;
}

struct VPt {
    float vx, vy, vz;
    Pt    s;
};

static VPt toView(const Basis& b, const cXyz& p)
{
    const cXyz d = sub(p, b.eye);
    VPt v;
    v.vx = dot(d, b.side);
    v.vy = dot(d, b.up);
    v.vz = dot(d, b.fwd);
    if (v.vz >= kNearClip)
        projectView(b, v.vx, v.vy, v.vz, &v.s);
    else
        v.s.x = v.s.y = v.s.d = 0.0f;
    return v;
}

static bool inFront(const VPt& v) { return v.vz >= kNearClip; }

static void cutPoint(const Basis& b, const VPt& a, const VPt& c, Pt* out)
{
    const float t = (kNearClip - a.vz) / (c.vz - a.vz);
    projectView(b, a.vx + (c.vx - a.vx) * t, a.vy + (c.vy - a.vy) * t, kNearClip, out);
}

// Sutherland-Hodgman against the near plane; chord is the edge left lying on it.
static int clipPoly(const Basis& b, const VPt* in, int n, Pt* out, int* chord)
{
    int count = 0;
    *chord = -1;
    for (int i = 0; i < n; ++i) {
        const VPt& a = in[i];
        const VPt& c = in[(i + 1) % n];
        if (inFront(a))
            out[count++] = a.s;
        if (inFront(a) != inFront(c)) {
            if (inFront(a))
                *chord = count;
            cutPoint(b, a, c, &out[count++]);
        }
    }
    return count;
}

static bool clipSeg(const Basis& b, const VPt& a, const VPt& c, Pt* pa, Pt* pc)
{
    if (!inFront(a) && !inFront(c))
        return false;
    *pa = a.s;
    *pc = c.s;
    if (inFront(a) != inFront(c))
        cutPoint(b, a, c, inFront(a) ? pc : pa);
    return true;
}

static ImVec2 uvFor(float d)
{
    return s_depthMode ? ImVec2(d, 0.0f) : s_uvWhite;
}

static void fillPoly(ImDrawList* dl, const Pt* p, int n, ImU32 col)
{
    if (n < 3 || !(col & IM_COL32_A_MASK))
        return;
    dl->PrimReserve((n - 2) * 3, n);
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int i = 2; i < n; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx((ImDrawIdx)(base + i - 1));
        dl->PrimWriteIdx((ImDrawIdx)(base + i));
    }
    for (int i = 0; i < n; ++i)
        dl->PrimWriteVtx(ImVec2(p[i].x, p[i].y), uvFor(p[i].d), col);
}

static void segment(ImDrawList* dl, const Pt& a, const Pt& b, ImU32 col, float width)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    const float s = width * 0.5f / len;
    const float nx = -dy * s, ny = dx * s;
    const Pt q[4] = { { a.x + nx, a.y + ny, a.d }, { b.x + nx, b.y + ny, b.d },
                      { b.x - nx, b.y - ny, b.d }, { a.x - nx, a.y - ny, a.d } };
    fillPoly(dl, q, 4, col);
}

static void polyline(ImDrawList* dl, const Pt* p, int n, bool closed, ImU32 col, float width)
{
    for (int i = 0; i + 1 < n; ++i)
        segment(dl, p[i], p[i + 1], col, width);
    if (closed && n > 2)
        segment(dl, p[n - 1], p[0], col, width);
}

static void circle(ImDrawList* dl, const Pt& c, float r, ImU32 fill, ImU32 line)
{
    if (r < 0.5f)
        return;
    Pt pts[kCircleSegs];
    for (int i = 0; i < kCircleSegs; ++i) {
        const float t = kTwoPi * (float)i / (float)kCircleSegs;
        pts[i].x = c.x + r * cosf(t);
        pts[i].y = c.y + r * sinf(t);
        pts[i].d = c.d;
    }
    fillPoly(dl, pts, kCircleSegs, fill);
    polyline(dl, pts, kCircleSegs, true, line, kLineWidth);
}

static ImU32 roleColor(u8 role, bool hit, bool fill)
{
    const int a = fill ? (hit ? 0xB4 : 0x50) : (hit ? 0xFF : 0xC0);
    switch (role) {
    case ROLE_AT: return IM_COL32(255, 40, 40, a);
    case ROLE_TG: return IM_COL32(40, 255, 40, a);
    default:      return IM_COL32(255, 255, 255, a);
    }
}

static void polyOut(ImDrawList* dl, const Basis& b, const VPt* v, int n, ImU32 fill,
                    ImU32 line, float width)
{
    Pt out[kRingSegs + 1];
    bool allIn = true;
    for (int i = 0; i < n && allIn; ++i)
        allIn = inFront(v[i]);
    int m, chord = -1;
    if (allIn) {
        for (int i = 0; i < n; ++i)
            out[i] = v[i].s;
        m = n;
    } else {
        m = clipPoly(b, v, n, out, &chord);
    }
    if (m < 3)
        return;
    fillPoly(dl, out, m, fill);
    if (!(line & IM_COL32_A_MASK))
        return;
    if (chord < 0) {
        polyline(dl, out, m, true, line, width);
        return;
    }
    for (int i = 0; i < m; ++i)
        if (i != chord)
            segment(dl, out[i], out[(i + 1) % m], line, width);
}

static void ringView(const Basis& b, const cXyz& c, const cXyz& e1, const cXyz& e2, float r,
                     VPt* out)
{
    for (int i = 0; i < kRingSegs; ++i) {
        const float t = kTwoPi * (float)i / (float)kRingSegs;
        out[i] = toView(b, add(c, add(mul(e1, r * cosf(t)), mul(e2, r * sinf(t)))));
    }
}

static void drawRing(ImDrawList* dl, const Basis& b, const VPt* pts, ImU32 fill, ImU32 line)
{
    polyOut(dl, b, pts, kRingSegs, fill, line, kLineWidth);
}

static void drawSides(ImDrawList* dl, const Basis& b, const VPt* a, const VPt* c, ImU32 fill,
                      ImU32 line)
{
    for (int i = 0; i < kRingSegs; ++i) {
        const int j = (i + 1) % kRingSegs;
        const VPt quad[4] = { a[i], a[j], c[j], c[i] };
        polyOut(dl, b, quad, 4, fill, 0, 0.0f);
        Pt pa, pc;
        if ((i & 3) == 0 && clipSeg(b, a[i], c[i], &pa, &pc))
            segment(dl, pa, pc, line, kEdgeWidth);
    }
}

static void drawCylinder(ImDrawList* dl, const Basis& b, const Shape& s, ImU32 fill, ImU32 line)
{
    const cXyz bottom = { s.v[0], s.v[1], s.v[2] };
    const cXyz top = { s.v[0], s.v[1] + s.v[4], s.v[2] };
    const cXyz ex = { 1.0f, 0.0f, 0.0f }, ez = { 0.0f, 0.0f, 1.0f };
    VPt lo[kRingSegs], hi[kRingSegs];
    ringView(b, bottom, ex, ez, s.v[3], lo);
    ringView(b, top, ex, ez, s.v[3], hi);
    drawSides(dl, b, lo, hi, fill, line);
    drawRing(dl, b, lo, fill, line);
    drawRing(dl, b, hi, fill, line);
}

static void drawSphere(ImDrawList* dl, const Basis& b, const Shape& s, ImU32 fill, ImU32 line)
{
    const cXyz c = { s.v[0], s.v[1], s.v[2] };
    Pt centre;
    if (project(b, c, &centre))
        circle(dl, centre, projectedRadius(b, c, s.v[3]), fill, line);
    const cXyz ex = { 1.0f, 0.0f, 0.0f }, ey = { 0.0f, 1.0f, 0.0f }, ez = { 0.0f, 0.0f, 1.0f };
    VPt pts[kRingSegs];
    ringView(b, c, ex, ez, s.v[3], pts);
    drawRing(dl, b, pts, 0, line);
    ringView(b, c, ex, ey, s.v[3], pts);
    drawRing(dl, b, pts, 0, line);
}

static void drawCapsule(ImDrawList* dl, const Basis& b, const Shape& s, ImU32 fill, ImU32 line)
{
    const cXyz a = { s.v[0], s.v[1], s.v[2] };
    const cXyz c = { s.v[3], s.v[4], s.v[5] };
    const float r = s.v[6];
    cXyz axis = sub(c, a);
    const float len = sqrtf(dot(axis, axis));
    if (len < 0.001f) {
        Shape sph = s;
        sph.v[3] = r;
        drawSphere(dl, b, sph, fill, line);
        return;
    }
    axis = mul(axis, 1.0f / len);
    const cXyz helperY = { 0.0f, 1.0f, 0.0f }, helperX = { 1.0f, 0.0f, 0.0f };
    const cXyz e1 = normalize(cross(axis, fabsf(axis.y) < 0.9f ? helperY : helperX));
    const cXyz e2 = cross(axis, e1);
    VPt lo[kRingSegs], hi[kRingSegs];
    ringView(b, a, e1, e2, r, lo);
    ringView(b, c, e1, e2, r, hi);
    drawSides(dl, b, lo, hi, fill, line);
    Pt pa, pc;
    if (project(b, a, &pa))
        circle(dl, pa, projectedRadius(b, a, r), fill, line);
    if (project(b, c, &pc))
        circle(dl, pc, projectedRadius(b, c, r), fill, line);
}

static void drawBox(ImDrawList* dl, const Basis& b, const Shape& s, ImU32 fill, ImU32 line)
{
    VPt p[8];
    for (int i = 0; i < 8; ++i) {
        const cXyz c = { (i & 1) ? s.v[3] : s.v[0], (i & 2) ? s.v[4] : s.v[1], (i & 4) ? s.v[5] : s.v[2] };
        p[i] = toView(b, c);
    }
    static const int faces[6][4] = { {0,1,3,2}, {4,5,7,6}, {0,1,5,4}, {2,3,7,6}, {0,2,6,4}, {1,3,7,5} };
    for (int f = 0; f < 6; ++f) {
        const VPt quad[4] = { p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], p[faces[f][3]] };
        polyOut(dl, b, quad, 4, fill, 0, 0.0f);
    }
    static const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
    for (int e = 0; e < 12; ++e) {
        Pt pa, pc;
        if (clipSeg(b, p[edges[e][0]], p[edges[e][1]], &pa, &pc))
            segment(dl, pa, pc, line, kEdgeWidth);
    }
}

static cXyz shapeCentre(const Shape& s)
{
    cXyz c = { s.v[0], s.v[1], s.v[2] };
    if (s.kind == KIND_CYL) c.y += s.v[4] * 0.5f;
    else if (s.kind == KIND_CPS || s.kind == KIND_AABB) {
        c.x = (s.v[0] + s.v[3]) * 0.5f; c.y = (s.v[1] + s.v[4]) * 0.5f; c.z = (s.v[2] + s.v[5]) * 0.5f;
    }
    return c;
}

static float boundingRadius(const Shape& s)
{
    switch (s.kind) {
    case KIND_CYL: {
        const float half = s.v[4] * 0.5f;
        return sqrtf(s.v[3] * s.v[3] + half * half);
    }
    case KIND_SPH:
        return s.v[3];
    case KIND_CPS: {
        const cXyz a = { s.v[0], s.v[1], s.v[2] };
        const cXyz c = { s.v[3], s.v[4], s.v[5] };
        const cXyz d = sub(c, a);
        return sqrtf(dot(d, d)) * 0.5f + s.v[6];
    }
    default: {
        const cXyz half = { (s.v[3] - s.v[0]) * 0.5f, (s.v[4] - s.v[1]) * 0.5f,
                            (s.v[5] - s.v[2]) * 0.5f };
        return sqrtf(dot(half, half));
    }
    }
}

static void drawMassOne(ImDrawList* dl, const Basis& b, const Mass& m)
{
    const cXyz base = m.pos;
    const cXyz top = { m.pos.x, m.pos.y + m.height, m.pos.z };
    const cXyz ex = { 1.0f, 0.0f, 0.0f }, ez = { 0.0f, 0.0f, 1.0f };
    VPt lo[kMassSegs], hi[kMassSegs];
    for (int i = 0; i < kMassSegs; ++i) {
        const float t = kTwoPi * (float)i / (float)kMassSegs;
        const cXyz o = add(mul(ex, m.radius * cosf(t)), mul(ez, m.radius * sinf(t)));
        lo[i] = toView(b, add(base, o));
        hi[i] = toView(b, add(top, o));
    }
    const bool hit = (m.hits & (WWHD_CCMASS_HIT_AT | WWHD_CCMASS_HIT_CO)) != 0;
    const ImU32 line = hit ? IM_COL32(255, 240, 80, 255) : IM_COL32(80, 230, 190, 190);
    polyOut(dl, b, lo, kMassSegs, 0, line, kEdgeWidth);
    polyOut(dl, b, hi, kMassSegs, 0, line, kEdgeWidth);
    for (int i = 0; i < kMassSegs; i += 2) {
        Pt pa, pc;
        if (clipSeg(b, lo[i], hi[i], &pa, &pc))
            segment(dl, pa, pc, line, kEdgeWidth);
    }
}

static void drawMass(ImDrawList* dl, const Basis& b)
{
    const float range = Config::g_settings.collisionRange;
    for (int i = 0; i < s_massCount; ++i) {
        const Mass& m = s_mass[1 - s_massBuf][i];
        const cXyz centre = { m.pos.x, m.pos.y + m.height * 0.5f, m.pos.z };
        const cXyz d = sub(centre, b.eye);
        const float half = m.height * 0.5f;
        const float bound = sqrtf(m.radius * m.radius + half * half);
        if (range > 0.0f && sqrtf(dot(d, d)) - bound > range)
            continue;
        drawMassOne(dl, b, m);
    }
}

static bool containsEye(const Shape& s, const cXyz& eye)
{
    const float slack = 40.0f;
    switch (s.kind) {
    case KIND_CYL: {
        const float dx = eye.x - s.v[0], dz = eye.z - s.v[2];
        const float r = s.v[3] + slack;
        return dx * dx + dz * dz <= r * r && eye.y >= s.v[1] - slack &&
               eye.y <= s.v[1] + s.v[4] + slack;
    }
    case KIND_SPH: {
        const cXyz c = { s.v[0], s.v[1], s.v[2] };
        const cXyz d = sub(eye, c);
        const float r = s.v[3] + slack;
        return dot(d, d) <= r * r;
    }
    case KIND_CPS: {
        const cXyz a = { s.v[0], s.v[1], s.v[2] };
        const cXyz c = { s.v[3], s.v[4], s.v[5] };
        const cXyz ab = sub(c, a);
        const float len2 = dot(ab, ab);
        float t = len2 > 0.0001f ? dot(sub(eye, a), ab) / len2 : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        const cXyz d = sub(eye, add(a, mul(ab, t)));
        const float r = s.v[6] + slack;
        return dot(d, d) <= r * r;
    }
    default:
        return eye.x >= s.v[0] - slack && eye.x <= s.v[3] + slack &&
               eye.y >= s.v[1] - slack && eye.y <= s.v[4] + slack &&
               eye.z >= s.v[2] - slack && eye.z <= s.v[5] + slack;
    }
}

static void drawShapes(ImDrawList* dl, const Basis& b)
{
    const Config::Settings& cfg = Config::g_settings;
    const float range = cfg.collisionRange;

    bool hideOwn = false;
    for (int i = 0; i < s_count && !hideOwn; ++i)
        hideOwn = s_shapes[i].own && containsEye(s_shapes[i], b.eye);

    int n = 0;
    for (int i = 0; i < s_count; ++i) {
        if (hideOwn && s_shapes[i].own)
            continue;
        const cXyz d = sub(shapeCentre(s_shapes[i]), b.eye);
        if (range > 0.0f && sqrtf(dot(d, d)) - boundingRadius(s_shapes[i]) > range)
            continue;
        const float key = dot(d, b.fwd);
        int j = n++;
        while (j > 0 && s_orderKey[j - 1] < key) {
            s_order[j] = s_order[j - 1];
            s_orderKey[j] = s_orderKey[j - 1];
            --j;
        }
        s_order[j] = i;
        s_orderKey[j] = key;
    }

    for (int k = 0; k < n; ++k) {
        const Shape& s = s_shapes[s_order[k]];
        for (int r = 0; r < 3; ++r) {
            const u8 role = (u8)(1 << r);
            if (!(s.roles & role))
                continue;
            if ((role == ROLE_AT && !cfg.collisionAt) || (role == ROLE_TG && !cfg.collisionTg) ||
                (role == ROLE_CO && !cfg.collisionCo))
                continue;
            const bool hit = (s.hits & role) != 0;
            const ImU32 fill = roleColor(role, hit, true);
            const ImU32 line = roleColor(role, hit, false);
            switch (s.kind) {
            case KIND_CYL: drawCylinder(dl, b, s, fill, line); break;
            case KIND_SPH: drawSphere(dl, b, s, fill, line);   break;
            case KIND_CPS: drawCapsule(dl, b, s, fill, line);  break;
            default:       drawBox(dl, b, s, fill, line);      break;
            }
        }
    }
}

// The radius adapts to the triangle budget so every registry entry gets its nearest triangles.
static void collectMesh(const Basis& b, const cXyz& ref, float limit)
{
    if (s_meshCutoff <= 0.0f || s_meshCutoff > limit)
        s_meshCutoff = limit;
    const float range = s_meshCutoff;
    s_meshCount = 0;
    const float range2 = range * range;
    for (int e = 0; e < WWHD_BGS_ELM_MAX && s_meshCount < kMaxMeshTris; ++e) {
        const cBgS_ChkElm* elm = dComIfGp_getBgSElm(e);
        if (!cBgS_elmUsed(elm) || !elm->m_bgw_base_ptr)
            continue;
        const void* bgw = WWHD_AT(void, elm->m_bgw_base_ptr);
        const cBgD_t* bgd = cBgW_getBgd(bgw);
        const cBgD_Vtx_t* vtx = cBgW_getVtxTbl(bgw);
        if (!cBgD_isInitialised(bgd) || !vtx || !bgd->m_t_tbl)
            continue;
        const cBgD_Tri_t* tris = WWHD_AT(cBgD_Tri_t, bgd->m_t_tbl);
        const cBgD_Ti_t* tis = bgd->m_ti_tbl ? WWHD_AT(cBgD_Ti_t, bgd->m_ti_tbl) : (const cBgD_Ti_t*)0;
        const int vnum = bgd->m_v_num;

        for (int t = 0; t < bgd->m_t_num && s_meshCount < kMaxMeshTris; ++t) {
            const cBgD_Tri_t& tri = tris[t];
            if (tri.vtx0 >= vnum || tri.vtx1 >= vnum || tri.vtx2 >= vnum)
                continue;
            const cXyz v0 = { vtx[tri.vtx0].x, vtx[tri.vtx0].y, vtx[tri.vtx0].z };
            const cXyz v1 = { vtx[tri.vtx1].x, vtx[tri.vtx1].y, vtx[tri.vtx1].z };
            const cXyz v2 = { vtx[tri.vtx2].x, vtx[tri.vtx2].y, vtx[tri.vtx2].z };
            const cXyz centroid = mul(add(add(v0, v1), v2), 1.0f / 3.0f);
            const cXyz dc = sub(centroid, ref), d0 = sub(v0, ref), d1 = sub(v1, ref),
                       d2 = sub(v2, ref);
            if (dot(dc, dc) > range2 && dot(d0, d0) > range2 && dot(d1, d1) > range2 &&
                dot(d2, d2) > range2)
                continue;

            MeshTri& m = s_mesh[s_meshCount];
            const VPt v[3] = { toView(b, v0), toView(b, v1), toView(b, v2) };
            int n, chord = -1;
            if (inFront(v[0]) && inFront(v[1]) && inFront(v[2])) {
                m.p[0] = v[0].s; m.p[1] = v[1].s; m.p[2] = v[2].s;
                n = 3;
            } else {
                n = clipPoly(b, v, 3, m.p, &chord);
            }
            if (n < 3)
                continue;
            bool left = true, right = true, above = true, below = true;
            for (int i = 0; i < n; ++i) {
                left = left && m.p[i].x < 0.0f;
                right = right && m.p[i].x > b.halfW * 2.0f;
                above = above && m.p[i].y < 0.0f;
                below = below && m.p[i].y > b.halfH * 2.0f;
            }
            if (left || right || above || below)
                continue;
            m.n = (u8)n;
            m.cut = (u8)(chord < 0 ? 0xFF : chord);
            const cXyz nrm = cross(sub(v1, v0), sub(v2, v0));
            const float nlen = sqrtf(dot(nrm, nrm));
            const float ny = nlen > 0.0001f ? nrm.y / nlen : 0.0f;
            m.cls = ny >= 0.5f ? CLS_GROUND : (ny < -0.8f ? CLS_ROOF : CLS_WALL);
            m.through = (tis && tri.id < bgd->m_ti_num) ? ((tis[tri.id].mPolyInf3 & 0x04u) != 0) : 0;
            m.zv = viewDepth(b, centroid);
            m.next = -1;
            s_meshCount++;
        }
    }

    if (s_meshCount >= kMaxMeshTris) {
        s_meshCutoff = range * 0.85f;
        if (s_meshCutoff < kMeshCutoffMin)
            s_meshCutoff = kMeshCutoffMin;
    } else if (s_meshCount * 4 < kMaxMeshTris * 3 && range < limit) {
        s_meshCutoff = range * 1.06f;
        if (s_meshCutoff > limit)
            s_meshCutoff = limit;
    }
}

static ImU32 meshColor(u8 cls, u8 through, bool fill)
{
    const int a = fill ? (through ? 0x20 : 0x38) : (through ? 0xC0 : 0x80);
    if (through && !fill)
        return IM_COL32(255, 80, 200, a);
    switch (cls) {
    case CLS_GROUND: return IM_COL32(70, 150, 255, a);
    case CLS_ROOF:   return IM_COL32(190, 90, 255, a);
    default:         return IM_COL32(255, 150, 60, a);
    }
}

static void drawMesh(ImDrawList* dl, float range)
{
    if (s_meshCount == 0)
        return;
    const float step = range * 1.5f / (float)kMeshBuckets;
    int head[kMeshBuckets];
    for (int i = 0; i < kMeshBuckets; ++i)
        head[i] = -1;
    for (int i = 0; i < s_meshCount; ++i) {
        int bucket = (int)(s_mesh[i].zv / step);
        if (bucket < 0) bucket = 0;
        if (bucket >= kMeshBuckets) bucket = kMeshBuckets - 1;
        s_mesh[i].next = head[bucket];
        head[bucket] = i;
    }
    for (int bkt = kMeshBuckets - 1; bkt >= 0; --bkt) {
        for (int i = head[bkt]; i >= 0; i = s_mesh[i].next) {
            const MeshTri& m = s_mesh[i];
            const int n = m.n;
            fillPoly(dl, m.p, n, meshColor(m.cls, m.through, true));
            const ImU32 line = meshColor(m.cls, m.through, false);
            if (m.cut == 0xFF) {
                polyline(dl, m.p, n, true, line, kEdgeWidth);
                continue;
            }
            for (int e = 0; e < n; ++e)
                if (e != (int)m.cut)
                    segment(dl, m.p[e], m.p[(e + 1) % n], line, kEdgeWidth);
        }
    }
}

static void bindDepthCb(const ImDrawList*, const ImDrawCmd*)
{
    SceneDepth::Bind(Config::g_settings.collisionDepthInvert ? -1.0f : 1.0f, 0.0f);
}

void Draw(float logicalWidth, float logicalHeight)
{
    const Config::Settings& cfg = Config::g_settings;
    if (!cfg.collisionView || !s_view.valid || s_hiddenWhy)
        return;

    s_depthMode = cfg.collisionDepth && s_view.projValid && SceneDepth::Available();
    s_uvWhite = ImGui::GetFontTexUvWhitePixel();

    Basis b;
    b.eye = s_view.eye;
    b.fwd = normalize(sub(s_view.center, s_view.eye));
    b.side = normalize(cross(b.fwd, s_view.up));
    b.up = cross(b.side, b.fwd);
    b.tanHalf = tanf(s_view.fovy * 0.5f * kDegToRad);
    if (b.tanHalf < 0.01f) b.tanHalf = 0.01f;
    b.aspect = logicalWidth / logicalHeight;
    b.halfW = logicalWidth * 0.5f;
    b.halfH = logicalHeight * 0.5f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (s_depthMode)
        dl->AddCallback(bindDepthCb, nullptr);

    if (cfg.collisionMesh) {
        const daPy_lk_c* link =
            Tools::FlyCam::IsActive() ? (const daPy_lk_c*)0 : daPy_lk_c_getPlayer();
        const cXyz ref = link ? link->base.current.pos : b.eye;
        collectMesh(b, ref, cfg.collisionMeshRange);
        drawMesh(dl, s_meshCutoff);
    }
    drawShapes(dl, b);
    if (cfg.collisionMass)
        drawMass(dl, b);

    if (s_depthMode)
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
}
}
