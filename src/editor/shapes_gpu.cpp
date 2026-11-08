#include "shapes_gpu.h"
#include "../log.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <thread>

namespace shapes {

namespace {

// ---- the shaders (HLSL, compiled at start for vs_3_0 / ps_3_0)
// Pixel shader constants (float4): see Params. p: the point in the shape's own pixels, from its centre (as shapes.cpp's
// texel centres); the soft edge is one screen pixel (from the derivatives, so turns and animations stay sharp).
const char kPS[] = R"HLSL(
float4 P0 : register(c0);   // quad width, height (shape pixels, margin included), margin, mode (0 fill, 1 stroke)
float4 P1 : register(c1);   // half width, half height, stroke band inner side, outer side (distance from the edge)
float4 P2 : register(c2);   // corner radii: top-left, top-right, bottom-right, bottom-left
float4 P3 : register(c3);   // stroke width, fill gradient mode, stroke gradient mode, cuts
float4 P4 : register(c4);   // fill: linear direction x, y, half length along it, depth (from the edge)
float4 P5 : register(c5);   // stroke: start offset (turns), fill stops, stroke stops, -
float4 FC[8] : register(c6);    // fill stops: colour
float4 FP[8] : register(c14);   //             position (x)
float4 SC[8] : register(c22);   // stroke stops: colour
float4 SP[8] : register(c30);   //               position (x)
float4 CA[16] : register(c38);  // cuts: centre x, y (shape's frame), cos, sin (into the cut's frame)
float4 CB[16] : register(c54);  //       half width, half height, gap, -
float4 CR[16] : register(c70);  //       corner radii

static const float PI = 3.14159265;

float SdRoundBox(float2 p, float2 b, float4 r) {
    float rr = p.x > 0 ? (p.y > 0 ? r.z : r.y) : (p.y > 0 ? r.w : r.x);
    rr = max(0, min(rr, min(b.x, b.y)));
    float2 q = abs(p) - b + rr;
    return min(max(q.x, q.y), 0) + length(max(q, 0)) - rr;
}

void Seg(float2 p, float2 a, float2 e, float len, float off, inout float best, inout float at) {
    float2 d = e - a;
    float l2 = dot(d, d);
    float u = l2 > 1e-6 ? saturate(dot(p - a, d) / l2) : 0;
    float2 q = a + u * d - p;
    float dd = dot(q, q);
    if (dd < best) { best = dd; at = off + u * len; }
}
void Arc(float2 p, float2 c, float rr, float a0, float len, float off, inout float best, inout float at) {
    if (rr > 0) {
        float a = atan2(p.y - c.y, p.x - c.x) - a0;
        a = a - 2 * PI * floor((a + PI) / (2 * PI));
        float u = saturate(a / (PI / 2));
        float qa = a0 + u * PI / 2;
        float2 q = c + float2(cos(qa), sin(qa)) * rr - p;
        float dd = dot(q, q);
        if (dd < best) { best = dd; at = off + u * len; }
    }
}
// where on the outline p is nearest, 0..1 clockwise from the top edge's left end
float OutlineT(float2 p, float2 b, float4 r4) {
    float4 r = max(0, min(r4, min(b.x, b.y)));
    float s0 = max(0, 2 * b.x - r.x - r.y), s1 = PI / 2 * r.y, s2 = max(0, 2 * b.y - r.y - r.z), s3 = PI / 2 * r.z;
    float s4 = max(0, 2 * b.x - r.z - r.w), s5 = PI / 2 * r.w, s6 = max(0, 2 * b.y - r.w - r.x), s7 = PI / 2 * r.x;
    float total = max(1e-3, s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7);
    float best = 1e30, at = 0, off = 0;
    Seg(p, float2(-b.x + r.x, -b.y), float2(b.x - r.y, -b.y), s0, off, best, at); off += s0;
    Arc(p, float2(b.x - r.y, -b.y + r.y), r.y, -PI / 2, s1, off, best, at); off += s1;
    Seg(p, float2(b.x, -b.y + r.y), float2(b.x, b.y - r.z), s2, off, best, at); off += s2;
    Arc(p, float2(b.x - r.z, b.y - r.z), r.z, 0, s3, off, best, at); off += s3;
    Seg(p, float2(b.x - r.z, b.y), float2(-b.x + r.w, b.y), s4, off, best, at); off += s4;
    Arc(p, float2(-b.x + r.w, b.y - r.w), r.w, PI / 2, s5, off, best, at); off += s5;
    Seg(p, float2(-b.x, b.y - r.w), float2(-b.x, -b.y + r.x), s6, off, best, at); off += s6;
    Arc(p, float2(-b.x + r.x, -b.y + r.x), r.x, PI, s7, off, best, at);
    return at / total;
}

// a gradient's colour at t: straight lines between stops, held past the ends
float4 SampFill(float t) {
    float n = P5.y;
    float4 c = FC[0];
    bool done = t <= FP[0].x;
    [loop] for (int j = 0; j < 7; j++) {
        if (j + 1 >= n || done) break;
        float a = FP[j].x, b = FP[j + 1].x;
        if (t >= a && t <= b) { float f = b - a > 1e-6 ? (t - a) / (b - a) : 0; c = lerp(FC[j], FC[j + 1], f); done = true; }
        else c = FC[j + 1];
    }
    return c;
}
float4 SampStroke(float t) {
    float n = P5.z;
    float4 c = SC[0];
    bool done = t <= SP[0].x;
    [loop] for (int j = 0; j < 7; j++) {
        if (j + 1 >= n || done) break;
        float a = SP[j].x, b = SP[j + 1].x;
        if (t >= a && t <= b) { float f = b - a > 1e-6 ? (t - a) / (b - a) : 0; c = lerp(SC[j], SC[j + 1], f); done = true; }
        else c = SC[j + 1];
    }
    return c;
}

float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {
    float2 p = uv * P0.xy - P0.zz - P1.xy;
    float fw = max(max(length(ddx(p)), length(ddy(p))), 1e-4);   // shape pixels per screen pixel
    float k = 1 / fw, aa = 0.5 * fw;
    float2 b = P1.xy;
    float lo = P1.z, hi = P1.w, sw = P3.x;
    float d = SdRoundBox(p, b, P2);
    float cutEdge = 0;   // the nearest edge here is a cut's (no stroke of the shape's there)
    float nc = P3.w, dcut = -1e9;
    [loop] for (int i = 0; i < 16; i++) {   // A minus B: max(dA, -dB), B grown by its gap
        if (i >= nc) break;
        float2 l = p - CA[i].xy;
        float2 rp = float2(l.x * CA[i].z - l.y * CA[i].w, l.x * CA[i].w + l.y * CA[i].z);
        float dc = -(SdRoundBox(rp, CB[i].xy, CR[i]) - CB[i].z);
        dcut = max(dcut, dc);
        if (dc > d) { d = dc; cutEdge = 1; }
    }
    float4 o = 0;
    if (P0.w < 0.5) {   // the fill: to the path, under the stroke
        float cov = saturate((aa - d) * k);
        float4 c = float4(1, 1, 1, 1);
        if (P3.y > 0.5) {   // (one sample, its t by the gradient's mode: linear, radial, from the edge)
            // (from the edge: measured from the outline with its corners rounded to 1.5x the depth at least, so the
            // glow's lines bend round the corners instead of meeting in creases from them)
            float dg = max(SdRoundBox(p, b, max(P2, 1.5 * P4.w)), dcut);
            float t = P3.y < 1.5 ? (dot(p, P4.xy) / P4.z + 1) / 2 : P3.y < 2.5 ? length(p / max(b, 1e-3)) : (P4.w > 0 ? -dg / P4.w : 1);
            c = SampFill(saturate(t));
        }
        o = float4(c.rgb, c.a * cov);
    } else {            // the stroke band
        float outer = saturate((aa - (d - hi)) * k), inner = saturate((aa - (d - lo)) * k);
        float band = (sw > 0 && cutEdge < 0.5) ? max(0, outer - inner) : 0;
        float4 c = float4(1, 1, 1, 1);
        if (P3.z > 0.5) {   // across the stroke, or along it (around the outline from its start offset)
            float t = (hi - d) / max(1e-3, sw);
            if (P3.z > 1.5) { t = OutlineT(p, b, P2) + P5.x; t = t - floor(t); }
            c = SampStroke(saturate(t));
        }
        o = float4(c.rgb, c.a * band);
    }
    return o * col;
}
)HLSL";

// ImGui's DX9 vertices (position, colour, uv) through its own projection (passed in: world / view are identity)
const char kVS[] = R"HLSL(
row_major float4x4 WVP : register(c0);
struct VO { float4 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
VO main(float4 pos : POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) {
    VO o;
    o.pos = mul(float4(pos.xyz, 1), WVP);
    o.col = col;
    o.uv = uv;
    return o;
}
)HLSL";

constexpr int kRegs = 86;
struct Params { float c[kRegs][4]; };
std::deque<Params> g_params;   // this frame's shapes' constants (the draw list keeps pointers until it's rendered)

IDirect3DDevice9* g_gdev = nullptr;
IDirect3DPixelShader9* g_ps = nullptr;
IDirect3DVertexShader9* g_vs = nullptr;
// 0 not tried, 1 compiling (background), 2 compiled (shaders not made yet), 3 ready, -1 failed
std::atomic<int> g_state{0};
std::string g_psCode, g_vsCode;

typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                   ID3DBlob**, ID3DBlob**);

bool CompileOne(CompileFn fn, const char* src, size_t len, const char* target, std::string& out) {
    ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
    HRESULT hr = fn(src, len, "reui_shape", nullptr, nullptr, "main", target, 1u << 15 /* optimization level 3 */, 0, &code, &err);
    if (err) {
        std::string msg((const char*)err->GetBufferPointer(), err->GetBufferSize());
        reui::Log("shapes: %s %s: %s", target, FAILED(hr) ? "didn't compile" : "warnings", msg.c_str());
        err->Release();
    }
    if (FAILED(hr) || !code) { if (code) code->Release(); return false; }
    out.assign((const char*)code->GetBufferPointer(), code->GetBufferSize());
    code->Release();
    return true;
}
void CompileThread() {
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    if (!m) m = LoadLibraryA("d3dcompiler_43.dll");
    CompileFn fn = m ? (CompileFn)GetProcAddress(m, "D3DCompile") : nullptr;
    if (!fn) { reui::Log("shapes: no d3dcompiler_47.dll / _43.dll - shapes made on the CPU"); g_state = -1; return; }
    bool ok = CompileOne(fn, kPS, sizeof kPS - 1, "ps_3_0", g_psCode) && CompileOne(fn, kVS, sizeof kVS - 1, "vs_3_0", g_vsCode);
    g_state = ok ? 2 : -1;
    if (!ok) reui::Log("shapes: shaders failed - shapes made on the CPU");
}

void __cdecl On(const ImDrawList*, const ImDrawCmd* cmd) {
    const Params* p = (const Params*)cmd->UserCallbackData;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!p || !dd || !g_gdev) return;
    float L = dd->DisplayPos.x + 0.5f, R = L + dd->DisplaySize.x, T = dd->DisplayPos.y + 0.5f, B = T + dd->DisplaySize.y;
    float m[16] = {2.f / (R - L), 0, 0, 0,   0, 2.f / (T - B), 0, 0,   0, 0, 0.5f, 0,   (L + R) / (L - R), (T + B) / (B - T), 0.5f, 1};
    g_gdev->SetVertexShader(g_vs);
    g_gdev->SetPixelShader(g_ps);
    g_gdev->SetVertexShaderConstantF(0, m, 4);
    g_gdev->SetPixelShaderConstantF(0, &p->c[0][0], kRegs);
}
void __cdecl Off(const ImDrawList*, const ImDrawCmd*) {
    if (!g_gdev) return;
    g_gdev->SetVertexShader(nullptr);
    g_gdev->SetPixelShader(nullptr);
}

void Set(Params& p, int r, float a, float b, float c, float d) { p.c[r][0] = a; p.c[r][1] = b; p.c[r][2] = c; p.c[r][3] = d; }

} // namespace

bool GpuOn(IDirect3DDevice9* dev) {
    if (!dev) return false;
    if (g_state == 3 && dev == g_gdev) return true;
    if (g_state == 3 && dev != g_gdev) { GpuRelease(); g_state = 2; }   // (a new device: the shaders made again from the code)
    if (g_state == 0) { g_state = 1; std::thread(CompileThread).detach(); return false; }   // (CPU meanwhile: no hitch)
    if (g_state != 2) return false;
    g_gdev = dev;
    if (FAILED(dev->CreatePixelShader((const DWORD*)g_psCode.data(), &g_ps)) || FAILED(dev->CreateVertexShader((const DWORD*)g_vsCode.data(), &g_vs))) {
        reui::Log("shapes: the device didn't take the shaders (shader model 3 needed) - shapes made on the CPU");
        GpuRelease(); g_state = -1; return false;
    }
    g_state = 3;
    reui::Log("shapes: drawn on the graphics card");
    return true;
}
void GpuFrameStart() { g_params.clear(); }
void GpuRelease() {
    if (g_ps) { g_ps->Release(); g_ps = nullptr; }
    if (g_vs) { g_vs->Release(); g_vs = nullptr; }
    if (g_state == 3) g_state = 2;   // (made again on the next ask)
    g_gdev = nullptr;
    g_params.clear();
}

float GpuMargin(float stroke, int align) {
    float sw = std::max(0.f, std::min(stroke, 1e4f));
    float lo = align == ALIGN_INSIDE ? -sw : align == ALIGN_CENTRE ? -sw / 2 : 0.f;
    return 1.5f + std::max(0.f, lo + sw);
}

bool GpuDraw(ImDrawList* dl, const Box& s, float stroke, int align, const Gradient& sg, const Gradient& fg,
             const std::vector<Box>& cuts, float margin, const ImVec2 q[4], ImU32 fillCol, ImU32 strokeCol) {
    if (g_state != 3 || !dl) return false;
    if (!std::isfinite(s.w) || !std::isfinite(s.h) || !std::isfinite(stroke) || s.w < 1 || s.h < 1 || s.w > 1e5f || s.h > 1e5f) return false;
    if ((int)cuts.size() > kGpuMaxCuts || (int)sg.stops.size() > kGpuMaxStops || (int)fg.stops.size() > kGpuMaxStops) return false;
    float sw = std::max(0.f, std::min(stroke, 1e4f));
    if (align < ALIGN_INSIDE || align > ALIGN_OUTSIDE) align = ALIGN_INSIDE;
    float lo = align == ALIGN_INSIDE ? -sw : align == ALIGN_CENTRE ? -sw / 2 : 0.f, hi = lo + sw;
    float bx = s.w / 2, by = s.h / 2;
    int fm = fg.mode && !fg.stops.empty() ? fg.mode : 0, sm = sg.mode && !sg.stops.empty() ? sg.mode : 0;
    Params p; memset(&p, 0, sizeof p);
    Set(p, 0, s.w + 2 * margin, s.h + 2 * margin, margin, 0);
    Set(p, 1, bx, by, lo, hi);
    Set(p, 2, s.r[0], s.r[1], s.r[2], s.r[3]);
    Set(p, 3, sw, (float)fm, (float)sm, (float)cuts.size());
    float la = fg.angle * 3.14159265f / 180, ldx = cosf(la), ldy = sinf(la);
    Set(p, 4, ldx, ldy, std::max(1e-3f, fabsf(ldx) * bx + fabsf(ldy) * by), fg.depth);
    Set(p, 5, sg.angle / 360.f, (float)fg.stops.size(), (float)sg.stops.size(), 0);
    for (size_t i = 0; i < fg.stops.size(); ++i) { auto& st = fg.stops[i]; Set(p, 6 + (int)i, st[1], st[2], st[3], st[4]); Set(p, 14 + (int)i, st[0], 0, 0, 0); }
    for (size_t i = 0; i < sg.stops.size(); ++i) { auto& st = sg.stops[i]; Set(p, 22 + (int)i, st[1], st[2], st[3], st[4]); Set(p, 30 + (int)i, st[0], 0, 0, 0); }
    for (size_t i = 0; i < cuts.size(); ++i) {
        const Box& c = cuts[i];
        Set(p, 38 + (int)i, c.cx, c.cy, cosf(-c.rot), sinf(-c.rot));
        Set(p, 54 + (int)i, c.w / 2, c.h / 2, c.gap, 0);
        Set(p, 70 + (int)i, c.r[0], c.r[1], c.r[2], c.r[3]);
    }
    ImTextureID tex = ImGui::GetIO().Fonts->TexID;   // (any texture: the shader doesn't read it)
    bool any = false;
    if ((fillCol >> IM_COL32_A_SHIFT) & 0xFF) {
        g_params.push_back(p);
        dl->AddCallback(On, &g_params.back());
        dl->AddImageQuad(tex, q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), fillCol);
        any = true;
    }
    if (sw > 0 && ((strokeCol >> IM_COL32_A_SHIFT) & 0xFF)) {
        p.c[0][3] = 1;
        g_params.push_back(p);
        dl->AddCallback(On, &g_params.back());
        dl->AddImageQuad(tex, q[0], q[1], q[2], q[3], ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), strokeCol);
        any = true;
    }
    if (any) dl->AddCallback(Off, nullptr);
    return true;
}

} // namespace shapes
