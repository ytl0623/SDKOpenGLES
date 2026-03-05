#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>
#include <string>

#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 1: BIN 解析 (Load_From_BIN) — 與純 C 版完全一致
 * ═══════════════════════════════════════════════════════════════════════ */

#define MAX_RULES  4096
#define NUM_LEVELS 7

struct DMC_State {
    int  DMC_EN, H_size, V_size, Bit_num;
    int  Width, Height;
    int  Level_B, Level_1, Level_2, Level_3, Level_4;
    int  Level_5, Level_6, Level_7, Level_W;
    int  Gray_Level[7];
    float COEF[8];
    float Offset_R[7], Offset_G[7], Offset_B[7];
    float Mag_R[7], Mag_G[7], Mag_B[7];
    float DMC_VACTIVE, DMC_PACKED_NUM, DMC_TRANS_NUM;
    int   Idx_table_W, Idx_table_H;
    float Max_rule_number;

    float Rule_R[MAX_RULES][7];
    float Rule_G[MAX_RULES][7];
    float Rule_B[MAX_RULES][7];

    float *Idx_R, *Idx_G, *Idx_B;
};

static int read_lv12(const uint8_t *d, int o) { return ((d[o]<<8)|d[o+1])>>4; }
static void unpack_nib(uint8_t v, float &h, float &l) { h=(v>>4)&0xF; l=v&0xF; }
static int get_line_num(int b) {
    switch(b-8){case 0:case 1:return 3;case 2:return 4;case 3:return 8;
    case 4:return 2;case 5:return 4;case 6:return 1;default:return 2;}
}

static bool load_from_bin(DMC_State &s, const uint8_t *bin, long sz)
{
    const int P = 9437184;
    if (sz < P + 256 + 2 + 86016 + 4094 + 2) return false;

    s.DMC_EN = bin[P+6];
    s.H_size = (int)pow(2,(bin[P+7]>>4)&0xF);
    s.V_size = (int)pow(2, bin[P+7]&0xF);
    s.Bit_num = ((bin[P+8]>>4)&0xF)+8;

    s.Level_B=read_lv12(bin,P+10); s.Level_1=read_lv12(bin,P+12);
    s.Level_2=read_lv12(bin,P+14); s.Level_3=read_lv12(bin,P+16);
    s.Level_4=read_lv12(bin,P+18); s.Level_5=read_lv12(bin,P+20);
    s.Level_6=read_lv12(bin,P+22); s.Level_7=read_lv12(bin,P+24);
    s.Level_W=read_lv12(bin,P+26);
    s.Gray_Level[0]=s.Level_1; s.Gray_Level[1]=s.Level_2;
    s.Gray_Level[2]=s.Level_3; s.Gray_Level[3]=s.Level_4;
    s.Gray_Level[4]=s.Level_5; s.Gray_Level[5]=s.Level_6;
    s.Gray_Level[6]=s.Level_7;

    for(int i=0;i<8;i++){int a=P+28+i*3;
        s.COEF[i]=(float)((bin[a]<<16)|(bin[a+1]<<8)|bin[a+2]);}
    for(int i=0;i<7;i++){
        s.Offset_R[i]=(bin[P+52+i*2]<<8)|bin[P+53+i*2];
        s.Offset_G[i]=(bin[P+66+i*2]<<8)|bin[P+67+i*2];
        s.Offset_B[i]=(bin[P+80+i*2]<<8)|bin[P+81+i*2];}

    unpack_nib(bin[P+94],s.Mag_R[0],s.Mag_R[1]);
    unpack_nib(bin[P+95],s.Mag_R[2],s.Mag_R[3]);
    unpack_nib(bin[P+96],s.Mag_R[4],s.Mag_R[5]);
    float tmp;
    unpack_nib(bin[P+97],s.Mag_R[6],s.Mag_G[0]);
    unpack_nib(bin[P+98],s.Mag_G[1],s.Mag_G[2]);
    unpack_nib(bin[P+99],s.Mag_G[3],s.Mag_G[4]);
    unpack_nib(bin[P+100],s.Mag_G[5],s.Mag_G[6]);
    unpack_nib(bin[P+101],s.Mag_B[0],s.Mag_B[1]);
    unpack_nib(bin[P+102],s.Mag_B[2],s.Mag_B[3]);
    unpack_nib(bin[P+103],s.Mag_B[4],s.Mag_B[5]);
    unpack_nib(bin[P+104],tmp,s.Mag_B[6]);

    s.DMC_VACTIVE   =(bin[P+105]<<8)|bin[P+106];
    s.DMC_PACKED_NUM=(bin[P+107]<<8)|bin[P+108];
    s.DMC_TRANS_NUM =(bin[P+109]<<8)|bin[P+110];

    if(s.H_size==1) s.Idx_table_W=s.Width; else s.Idx_table_W=s.Width/s.H_size+1;
    if(s.V_size==1) s.Idx_table_H=s.Height; else s.Idx_table_H=s.Height/s.V_size+1;
    s.Max_rule_number=powf(2,s.Bit_num);

    /* LUT2 */
    int ptr=P+256+2;
    int lut2ch=4096*7, rules=(int)s.Max_rule_number;
    memset(s.Rule_R,0,sizeof(s.Rule_R));
    memset(s.Rule_G,0,sizeof(s.Rule_G));
    memset(s.Rule_B,0,sizeof(s.Rule_B));
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_R[i/7][i%7]=bin[ptr];ptr++;}
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_G[i/7][i%7]=bin[ptr];ptr++;}
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_B[i/7][i%7]=bin[ptr];ptr++;}

    /* LUT1 */
    ptr+=4094; ptr+=2;
    int idxW=s.Idx_table_W, idxH=s.Idx_table_H;
    int tableSize=idxW*idxH, lineValues=idxW*3;

    int ltp=s.Width/s.H_size/4; if(s.H_size!=1)ltp++;
    int ps=s.Bit_num*12, ln=get_line_num(s.Bit_num);
    int sc=(int)ceil((double)(512*ln/ps));
    int dc=(512*ln)%ps;
    int pc=ltp/sc; if(ltp%sc)pc++;
    int cv=12*sc;
    int hlp=(12*sc*pc)-idxW*3;
    int hldb=hlp*s.Bit_num;

    s.Idx_R=(float*)calloc(tableSize,sizeof(float));
    s.Idx_G=(float*)calloc(tableSize,sizeof(float));
    s.Idx_B=(float*)calloc(tableSize,sizeof(float));
    int tv=tableSize*3;
    float *up=(float*)calloc(tv,sizeof(float));

    long bb=0; int bc=0,bp=ptr,mask=(1<<s.Bit_num)-1;
    for(int i=0;i<tv;i++){
        while(bc<s.Bit_num&&bp<sz){bb=(bb<<8)|bin[bp++];bc+=8;}
        if(bc>=s.Bit_num){int sh=bc-s.Bit_num;up[i]=(float)((int)((bb>>sh)&mask));bc-=s.Bit_num;}
        int hlpc=(i+1)%lineValues;
        if(hlpc%cv==0){int sk=dc;while(bc<sk&&bp<sz){bb=(bb<<8)|bin[bp++];bc+=8;}bc-=sk;}
        if(hlpc==0){int sk=hldb;while(bc<sk&&bp<sz){bb=(bb<<8)|bin[bp++];bc+=8;}bc-=sk;}
    }
    for(int i=0;i<tableSize;i++){
        s.Idx_R[i]=up[i*3]; s.Idx_G[i]=up[i*3+1]; s.Idx_B[i]=up[i*3+2];}
    free(up);
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 2: GPU Texture 準備
 *
 * 紋理規劃:
 *   Unit 0 — uIdxHigh : Idx_table_W × Idx_table_H, RGB
 *            R = Idx_R >> 8, G = Idx_G >> 8, B = Idx_B >> 8
 *   Unit 1 — uIdxLow  : 同上
 *            R = Idx_R & 0xFF, G = Idx_G & 0xFF, B = Idx_B & 0xFF
 *   Unit 2 — uRuleTable : 4096 × 7, RGB
 *            R = Rule_R[idx][lv], G = Rule_G, B = Rule_B
 * ═══════════════════════════════════════════════════════════════════════ */

GLuint texIdxHigh, texIdxLow, texRule;

static void prepare_textures(const DMC_State &s)
{
    int W = s.Idx_table_W, H = s.Idx_table_H;
    int sz = W * H;

    /* --- Index Table: 拆成 High / Low 兩張 --- */
    std::vector<uint8_t> idxH(sz * 3), idxL(sz * 3);
    for (int i = 0; i < sz; i++) {
        int r = (int)s.Idx_R[i], g = (int)s.Idx_G[i], b = (int)s.Idx_B[i];
        idxH[i*3+0] = (r >> 8) & 0xFF;  idxL[i*3+0] = r & 0xFF;
        idxH[i*3+1] = (g >> 8) & 0xFF;  idxL[i*3+1] = g & 0xFF;
        idxH[i*3+2] = (b >> 8) & 0xFF;  idxL[i*3+2] = b & 0xFF;
    }

    auto uploadTex = [](GLuint &tex, int w, int h, const uint8_t *data) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    uploadTex(texIdxHigh, W, H, idxH.data());
    uploadTex(texIdxLow,  W, H, idxL.data());

    /* --- Rule Table: 4096 × 7, RGB packed --- */
    std::vector<uint8_t> rule(4096 * 7 * 3, 0);
    int rules = (int)s.Max_rule_number;
    for (int lv = 0; lv < 7; lv++) {
        for (int idx = 0; idx < 4096; idx++) {
            int off = (lv * 4096 + idx) * 3;
            rule[off + 0] = (idx < rules) ? (uint8_t)s.Rule_R[idx][lv] : 0;
            rule[off + 1] = (idx < rules) ? (uint8_t)s.Rule_G[idx][lv] : 0;
            rule[off + 2] = (idx < rules) ? (uint8_t)s.Rule_B[idx][lv] : 0;
        }
    }
    uploadTex(texRule, 4096, 7, rule.data());

    printf("Textures uploaded: IdxHigh(%dx%d) IdxLow(%dx%d) Rule(4096x7)\n",
           W, H, W, H);
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 3: Fragment Shader — 完整 DMC HW_demo 邏輯
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *vertSrc = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char *fragSrc = R"(
precision highp float;
varying vec2 vTexCoord;

/* ══════════════════════════════════════════════════════════
 * GLES 2.0 Safe Fragment Shader
 *
 * GLES 2.0 限制:
 *   - 不能用 int 當函式回傳型別
 *   - 不能用 runtime variable 做 uniform array index
 *   - 不能傳陣列給函式
 *   - 不能宣告 local array
 * 全部改用 float + if-else chain 解決
 * ══════════════════════════════════════════════════════════ */

/* Textures */
uniform sampler2D uIdxHigh;
uniform sampler2D uIdxLow;
uniform sampler2D uRuleTable;

/* DMC Levels: 10 個固定節點 */
uniform float uLV0, uLV1, uLV2, uLV3, uLV4;
uniform float uLV5, uLV6, uLV7, uLV8, uLV9;

/* COEF: 8 個 */
uniform float uCO0, uCO1, uCO2, uCO3, uCO4, uCO5, uCO6, uCO7;

/* Offset (已在 CPU 解碼為有號浮點) */
uniform float uOR0,uOR1,uOR2,uOR3,uOR4,uOR5,uOR6;
uniform float uOG0,uOG1,uOG2,uOG3,uOG4,uOG5,uOG6;
uniform float uOB0,uOB1,uOB2,uOB3,uOB4,uOB5,uOB6;

/* Mag */
uniform float uMR0,uMR1,uMR2,uMR3,uMR4,uMR5,uMR6;
uniform float uMG0,uMG1,uMG2,uMG3,uMG4,uMG5,uMG6;
uniform float uMB0,uMB1,uMB2,uMB3,uMB4,uMB5,uMB6;

/* Config */
uniform float uIdxW, uIdxH;
uniform float uHSize, uVSize;
uniform float uGrayLevel;

/* ──────────── 常用 helper ──────────── */

float hw_round(float n) {
    return (n > 0.0) ? floor(n + 0.5) : ceil(n - 0.5);
}

/* 用 float index 讀取 DMC_LEVEL (if-else chain, GLES 2.0 safe) */
float getLV(float idx) {
    if (idx < 0.5) return uLV0;
    if (idx < 1.5) return uLV1;
    if (idx < 2.5) return uLV2;
    if (idx < 3.5) return uLV3;
    if (idx < 4.5) return uLV4;
    if (idx < 5.5) return uLV5;
    if (idx < 6.5) return uLV6;
    if (idx < 7.5) return uLV7;
    if (idx < 8.5) return uLV8;
    return uLV9;
}

float getCOEF(float idx) {
    if (idx < 0.5) return uCO0;
    if (idx < 1.5) return uCO1;
    if (idx < 2.5) return uCO2;
    if (idx < 3.5) return uCO3;
    if (idx < 4.5) return uCO4;
    if (idx < 5.5) return uCO5;
    if (idx < 6.5) return uCO6;
    return uCO7;
}

/* ch: 0.0=R, 1.0=G, 2.0=B */
float getOffset(float ch, float idx) {
    if (ch < 0.5) {
        if (idx<0.5) return uOR0; if (idx<1.5) return uOR1;
        if (idx<2.5) return uOR2; if (idx<3.5) return uOR3;
        if (idx<4.5) return uOR4; if (idx<5.5) return uOR5;
        return uOR6;
    } else if (ch < 1.5) {
        if (idx<0.5) return uOG0; if (idx<1.5) return uOG1;
        if (idx<2.5) return uOG2; if (idx<3.5) return uOG3;
        if (idx<4.5) return uOG4; if (idx<5.5) return uOG5;
        return uOG6;
    } else {
        if (idx<0.5) return uOB0; if (idx<1.5) return uOB1;
        if (idx<2.5) return uOB2; if (idx<3.5) return uOB3;
        if (idx<4.5) return uOB4; if (idx<5.5) return uOB5;
        return uOB6;
    }
}

float getMag(float ch, float idx) {
    if (ch < 0.5) {
        if (idx<0.5) return uMR0; if (idx<1.5) return uMR1;
        if (idx<2.5) return uMR2; if (idx<3.5) return uMR3;
        if (idx<4.5) return uMR4; if (idx<5.5) return uMR5;
        return uMR6;
    } else if (ch < 1.5) {
        if (idx<0.5) return uMG0; if (idx<1.5) return uMG1;
        if (idx<2.5) return uMG2; if (idx<3.5) return uMG3;
        if (idx<4.5) return uMG4; if (idx<5.5) return uMG5;
        return uMG6;
    } else {
        if (idx<0.5) return uMB0; if (idx<1.5) return uMB1;
        if (idx<2.5) return uMB2; if (idx<3.5) return uMB3;
        if (idx<4.5) return uMB4; if (idx<5.5) return uMB5;
        return uMB6;
    }
}

/* ──────────── Texture 讀取 ──────────── */

vec2 idxUV(float col, float row) {
    return vec2((col + 0.5) / uIdxW, (row + 0.5) / uIdxH);
}

/* 讀取 12-bit Index, ch: 0.0=R 1.0=G 2.0=B */
float readIdx(vec2 uv, float ch) {
    vec3 hi = texture2D(uIdxHigh, uv).rgb;
    vec3 lo = texture2D(uIdxLow, uv).rgb;
    float h, l;
    if (ch < 0.5)      { h = hi.r; l = lo.r; }
    else if (ch < 1.5)  { h = hi.g; l = lo.g; }
    else                { h = hi.b; l = lo.b; }
    return floor(h * 255.0 + 0.5) * 256.0 + floor(l * 255.0 + 0.5);
}

/* 讀取 Rule Table, ch: 0.0=R 1.0=G 2.0=B */
float readRule(float idx, float lv, float ch) {
    vec2 uv = vec2((idx + 0.5) / 4096.0, (lv + 0.5) / 7.0);
    vec3 val = texture2D(uRuleTable, uv).rgb;
    if (ch < 0.5)      return floor(val.r * 255.0 + 0.5);
    else if (ch < 1.5)  return floor(val.g * 255.0 + 0.5);
    else                return floor(val.b * 255.0 + 0.5);
}

/* ──────────── Find plane (全 float) ──────────── */

float find_plane(float pt) {
    if (pt >  uLV9)                          return 9.0;
    if (pt >= uLV8 && uLV8 != uLV9)         return 8.0;
    if (pt >= uLV7)                          return 7.0;
    if (pt >= uLV6)                          return 6.0;
    if (pt >= uLV5)                          return 5.0;
    if (pt >= uLV4)                          return 4.0;
    if (pt >= uLV3)                          return 3.0;
    if (pt >= uLV2)                          return 2.0;
    if (pt >= uLV1)                          return 1.0;
    return 0.0;
}

/* ──────────── Block interpolation ──────────── */

float block_interp(float A, float B, float C, float D,
                   float H, float V) {
    float HAB = A + (B - A) * H;
    HAB = hw_round(HAB * 4.0) / 4.0;
    float HCD = C + (D - C) * H;
    HCD = hw_round(HCD * 4.0) / 4.0;
    float r = HAB + (HCD - HAB) * V;
    return hw_round(r * 4.0) / 4.0;
}

/* ──────────── Mag point ──────────── */

float mag_point(float mag, float val) {
    float d;
    if      (mag < 0.5) d = val * 4.0;
    else if (mag < 1.5) d = val * 2.0;
    else if (mag < 2.5) d = val;
    else if (mag < 3.5) d = val / 2.0;
    else if (mag < 4.5) d = val / 4.0;
    else if (mag < 5.5) d = val / 8.0;
    else                d = val / 16.0;
    return floor(d * 16.0) / 16.0;
}

/* ──────────── Plane linear interpolation ──────────── */

float plane_lerp(float x, float x1, float v1, float v2, float coef) {
    float y = x - x1;
    if (y < 0.0) y = 0.0;
    y = y * coef / 262144.0;
    y = hw_round(y * 16384.0) / 16384.0;
    if (y >= 1.0) y = 1.0 - (1.0 / 16384.0);
    float d = v2 - v1;
    if (d < 0.0) d = 0.0;
    y = y * d;
    y = hw_round(y * 16.0) / 16.0;
    if (y > 1023.9375) y = 1023.9375;
    y = y + v1;
    return hw_round(y * 4.0) / 4.0;
}

/* ══════════════════════════════════════════════════════════
 * 單通道 DMC 計算 (全 float, 無陣列傳參, GLES 2.0 safe)
 * ch: 0.0=R, 1.0=G, 2.0=B
 * ══════════════════════════════════════════════════════════ */
float dmc_channel(float in12, float ch, float px, float py) {
    float bl = find_plane(in12);
    if (bl < 0.5 || bl > 8.5) return in12;

    float HS = uHSize;
    float VS = uVSize;
    float bx = floor(px / HS);
    float by = floor(py / VS);

    float iA = readIdx(idxUV(bx, by), ch);
    float iB = (HS > 1.5) ? readIdx(idxUV(bx+1.0, by), ch) : 0.0;
    float iC = (VS > 1.5) ? readIdx(idxUV(bx, by+1.0), ch) : 0.0;
    float iD = (HS > 1.5 && VS > 1.5) ? readIdx(idxUV(bx+1.0, by+1.0), ch) : 0.0;

    float Hf = mod(px, HS) / HS;
    float Vf = mod(py, VS) / VS;

    /* V1 */
    float V1;
    if (bl < 1.5) {
        V1 = getLV(bl);
    } else {
        float lv1 = bl - 2.0;
        float rA = readRule(iA, lv1, ch);
        float rB = readRule(iB, lv1, ch);
        float rC = readRule(iC, lv1, ch);
        float rD = readRule(iD, lv1, ch);
        float V1d = block_interp(rA, rB, rC, rD, Hf, Vf);
        V1 = getLV(bl) + getOffset(ch, bl - 2.0);
        if (V1 > 1023.75) V1 = 1023.75;
        V1 = mag_point(getMag(ch, bl - 2.0), V1d) + V1;
    }

    /* V2 */
    float V2;
    if (bl > 7.5) {
        V2 = getLV(bl + 1.0);
    } else {
        float lv2 = bl - 1.0;
        float rA = readRule(iA, lv2, ch);
        float rB = readRule(iB, lv2, ch);
        float rC = readRule(iC, lv2, ch);
        float rD = readRule(iD, lv2, ch);
        float V2d = block_interp(rA, rB, rC, rD, Hf, Vf);
        V2 = getLV(bl + 1.0) + getOffset(ch, bl - 1.0);
        if (V2 > 1023.75) V2 = 1023.75;
        V2 = mag_point(getMag(ch, bl - 1.0), V2d) + V2;
    }

    if (V1 > 1023.9375) V1 = 1023.9375;
    if (V1 < 0.0) V1 = 0.0;
    if (V2 > 1023.9375) V2 = 1023.9375;
    if (V2 < 0.0) V2 = 0.0;

    return plane_lerp(in12, getLV(bl), V1, V2, getCOEF(bl - 1.0));
}

/* ══════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════ */
void main() {
    float px = floor(gl_FragCoord.x);
    float py = floor(gl_FragCoord.y);
    float in12 = uGrayLevel * 4.0;

    float r = dmc_channel(in12, 0.0, px, py);
    float g = dmc_channel(in12, 1.0, px, py);
    float b = dmc_channel(in12, 2.0, px, py);

    gl_FragColor = vec4(
        clamp(r / 4.0 / 255.0, 0.0, 1.0),
        clamp(g / 4.0 / 255.0, 0.0, 1.0),
        clamp(b / 4.0 / 255.0, 0.0, 1.0),
        1.0
    );
}
)";

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 4: OpenGL Setup, Render, Readback
 * ═══════════════════════════════════════════════════════════════════════ */

GLuint programID;
GLuint fbo, fboTex;  /* FBO for off-screen rendering at exact resolution */

static const GLfloat quadVerts[] = {-1,-1, 1,-1, -1,1, 1,1};
static const GLfloat quadUVs[]   = { 0,0, 1,0, 0,1, 1,1};

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("Shader compile error:\n%s\n", log);
        return 0;
    }
    return sh;
}

static bool setup_gl(const DMC_State &s)
{
    /* Compile & Link */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return false;

    programID = glCreateProgram();
    glAttachShader(programID, vs);
    glAttachShader(programID, fs);
    glLinkProgram(programID);
    GLint linked; glGetProgramiv(programID, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        glGetProgramInfoLog(programID, sizeof(log), NULL, log);
        printf("Program link error:\n%s\n", log);
        return false;
    }
    glUseProgram(programID);

    /* Vertex attributes */
    GLint aP = glGetAttribLocation(programID, "aPosition");
    GLint aT = glGetAttribLocation(programID, "aTexCoord");
    glEnableVertexAttribArray(aP);
    glVertexAttribPointer(aP, 2, GL_FLOAT, GL_FALSE, 0, quadVerts);
    glEnableVertexAttribArray(aT);
    glVertexAttribPointer(aT, 2, GL_FLOAT, GL_FALSE, 0, quadUVs);

    /* Textures → Units */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texIdxHigh);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texIdxLow);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texRule);

    glUniform1i(glGetUniformLocation(programID, "uIdxHigh"),   0);
    glUniform1i(glGetUniformLocation(programID, "uIdxLow"),    1);
    glUniform1i(glGetUniformLocation(programID, "uRuleTable"), 2);

    /* Uniforms: DMC Parameters (逐一設定, 不用陣列) */
    float DMC_LEVEL[10] = {
        0, s.Level_B*4.0f, s.Level_1*4.0f, s.Level_2*4.0f,
        s.Level_3*4.0f, s.Level_4*4.0f, s.Level_5*4.0f,
        s.Level_6*4.0f, s.Level_7*4.0f, s.Level_W*4.0f+3.0f
    };
    glUniform1f(glGetUniformLocation(programID, "uLV0"), DMC_LEVEL[0]);
    glUniform1f(glGetUniformLocation(programID, "uLV1"), DMC_LEVEL[1]);
    glUniform1f(glGetUniformLocation(programID, "uLV2"), DMC_LEVEL[2]);
    glUniform1f(glGetUniformLocation(programID, "uLV3"), DMC_LEVEL[3]);
    glUniform1f(glGetUniformLocation(programID, "uLV4"), DMC_LEVEL[4]);
    glUniform1f(glGetUniformLocation(programID, "uLV5"), DMC_LEVEL[5]);
    glUniform1f(glGetUniformLocation(programID, "uLV6"), DMC_LEVEL[6]);
    glUniform1f(glGetUniformLocation(programID, "uLV7"), DMC_LEVEL[7]);
    glUniform1f(glGetUniformLocation(programID, "uLV8"), DMC_LEVEL[8]);
    glUniform1f(glGetUniformLocation(programID, "uLV9"), DMC_LEVEL[9]);

    glUniform1f(glGetUniformLocation(programID, "uCO0"), s.COEF[0]);
    glUniform1f(glGetUniformLocation(programID, "uCO1"), s.COEF[1]);
    glUniform1f(glGetUniformLocation(programID, "uCO2"), s.COEF[2]);
    glUniform1f(glGetUniformLocation(programID, "uCO3"), s.COEF[3]);
    glUniform1f(glGetUniformLocation(programID, "uCO4"), s.COEF[4]);
    glUniform1f(glGetUniformLocation(programID, "uCO5"), s.COEF[5]);
    glUniform1f(glGetUniformLocation(programID, "uCO6"), s.COEF[6]);
    glUniform1f(glGetUniformLocation(programID, "uCO7"), s.COEF[7]);

    /* Offset: CPU 端先解碼 */
    auto decode = [](float raw) -> float {
        if (raw >= 2048)
            return -((float)((~(unsigned)(int)raw) % 2048 + 1) / 4.0f);
        else
            return raw / 4.0f;
    };
    float offR[7], offG[7], offB[7];
    for (int i = 0; i < 7; i++) {
        offR[i] = decode(s.Offset_R[i]);
        offG[i] = decode(s.Offset_G[i]);
        offB[i] = decode(s.Offset_B[i]);
    }
    const char *orN[] = {"uOR0","uOR1","uOR2","uOR3","uOR4","uOR5","uOR6"};
    const char *ogN[] = {"uOG0","uOG1","uOG2","uOG3","uOG4","uOG5","uOG6"};
    const char *obN[] = {"uOB0","uOB1","uOB2","uOB3","uOB4","uOB5","uOB6"};
    const char *mrN[] = {"uMR0","uMR1","uMR2","uMR3","uMR4","uMR5","uMR6"};
    const char *mgN[] = {"uMG0","uMG1","uMG2","uMG3","uMG4","uMG5","uMG6"};
    const char *mbN[] = {"uMB0","uMB1","uMB2","uMB3","uMB4","uMB5","uMB6"};
    for (int i = 0; i < 7; i++) {
        glUniform1f(glGetUniformLocation(programID, orN[i]), offR[i]);
        glUniform1f(glGetUniformLocation(programID, ogN[i]), offG[i]);
        glUniform1f(glGetUniformLocation(programID, obN[i]), offB[i]);
        glUniform1f(glGetUniformLocation(programID, mrN[i]), s.Mag_R[i]);
        glUniform1f(glGetUniformLocation(programID, mgN[i]), s.Mag_G[i]);
        glUniform1f(glGetUniformLocation(programID, mbN[i]), s.Mag_B[i]);
    }

    /* Debug: 印出關鍵參數確認 */
    printf("DMC_LEVEL: ");
    for(int i=0;i<10;i++) printf("%.0f ", DMC_LEVEL[i]);
    printf("\nCOEF: ");
    for(int i=0;i<8;i++) printf("%.0f ", s.COEF[i]);
    printf("\nOffset_R: ");
    for(int i=0;i<7;i++) printf("%.2f ", offR[i]);
    printf("\nMag_R: ");
    for(int i=0;i<7;i++) printf("%.0f ", s.Mag_R[i]);
    printf("\n");
    glUniform1f(glGetUniformLocation(programID, "uIdxW"),   (float)s.Idx_table_W);
    glUniform1f(glGetUniformLocation(programID, "uIdxH"),   (float)s.Idx_table_H);
    glUniform1f(glGetUniformLocation(programID, "uHSize"),  (float)s.H_size);
    glUniform1f(glGetUniformLocation(programID, "uVSize"),  (float)s.V_size);

    /* FBO: off-screen rendering at exact panel resolution */
    /* ★ 切到不用的 texture unit，避免覆蓋 unit 0~2 的綁定 ★ */
    glActiveTexture(GL_TEXTURE7);
    glGenTextures(1, &fboTex);
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.Width, s.Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fboTex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO error: 0x%X\n", status);
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    printf("OpenGL setup OK.\n");
    return true;
}

static void render_frame(int gray_level, int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);

    /* ★ 每次渲染前重新綁定 texture，避免被 FBO 建立時覆蓋 ★ */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texIdxHigh);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texIdxLow);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texRule);

    glUniform1f(glGetUniformLocation(programID, "uGrayLevel"), (float)gray_level);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* 強制 GPU 完成 (精確計時) */
    glFinish();
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 5: Readback + BMP 寫出
 * ═══════════════════════════════════════════════════════════════════════ */

#pragma pack(push, 1)
struct BmpFH { uint16_t t; uint32_t sz; uint16_t r1,r2; uint32_t off; };
struct BmpIH { uint32_t sz; int32_t w,h; uint16_t pl,bp;
               uint32_t co,is; int32_t xr,yr; uint32_t cu,ci; };
#pragma pack(pop)

static void save_bmp(const char *path, int w, int h, const uint8_t *rgba)
{
    int stride = (w * 3 + 3) & ~3;
    int imgSz = stride * h;

    BmpFH fh = {}; fh.t = 0x4D42;
    fh.off = sizeof(BmpFH) + sizeof(BmpIH);
    fh.sz = fh.off + imgSz;

    BmpIH ih = {}; ih.sz = sizeof(BmpIH);
    ih.w = w; ih.h = h; ih.pl = 1; ih.bp = 24; ih.is = imgSz;

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);

    std::vector<uint8_t> row(stride, 0);
    for (int y = h - 1; y >= 0; y--) {
        memset(row.data(), 0, stride);
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4; /* RGBA from glReadPixels */
            row[x*3+0] = rgba[si+2]; /* B */
            row[x*3+1] = rgba[si+1]; /* G */
            row[x*3+2] = rgba[si+0]; /* R */
        }
        fwrite(row.data(), 1, stride, f);
    }
    fclose(f);
    printf("Saved: %s\n", path);
}

static void save_csv(const char *path, int w, int h, const uint8_t *rgba)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 4;
            /* 還原 12-bit 值: 8-bit × 4 (與 C# _value.csv 一致) */
            fprintf(f, "%d,%d,%d,",
                    (int)rgba[i+0] * 4,   /* R */
                    (int)rgba[i+1] * 4,   /* G */
                    (int)rgba[i+2] * 4);  /* B */
        }
        fprintf(f, "\n");
    }
    fclose(f);
    printf("Saved: %s\n", path);
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 6: main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: %s <input.bin> <output.bmp> <width> <height> [gray_level]\n", argv[0]);
        return 1;
    }

    const char *bin_path = argv[1];
    const char *out_base = argv[2];
    int panel_w = atoi(argv[3]);
    int panel_h = atoi(argv[4]);
    int single_lv = (argc >= 6) ? atoi(argv[5]) : -1;

    /* ── 讀取 BIN ── */
    FILE *fb = fopen(bin_path, "rb");
    if (!fb) { perror(bin_path); return 1; }
    fseek(fb, 0, SEEK_END); long bsz = ftell(fb); rewind(fb);
    uint8_t *bin = (uint8_t *)malloc(bsz);
    fread(bin, 1, bsz, fb); fclose(fb);

    /* ── 解析 BIN ── */
    DMC_State st = {};
    st.Width = panel_w; st.Height = panel_h;

    auto t0 = std::chrono::high_resolution_clock::now();
    if (!load_from_bin(st, bin, bsz)) { free(bin); return 1; }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms_load = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count()/1000.0;
    printf("Load_From_BIN: %.1f ms\n", ms_load);
    free(bin);

    /* ── 初始化 EGL + OpenGL ── */
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(1920, 1080);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface,
                   CoreEGL::surface, CoreEGL::context);
    eglSwapInterval(CoreEGL::display, 0);
    printf("EGL init OK. GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));

    /* ── 上傳 Textures ── */
    auto t2 = std::chrono::high_resolution_clock::now();
    prepare_textures(st);
    auto t3 = std::chrono::high_resolution_clock::now();
    double ms_tex = std::chrono::duration_cast<std::chrono::microseconds>(t3-t2).count()/1000.0;
    printf("Texture upload: %.1f ms\n", ms_tex);

    /* ── 編譯 Shader & 設定 Uniforms ── */
    if (!setup_gl(st)) return 1;

    /* ── 決定要跑的 levels ── */
    int levels[7]; int n_lv;
    if (single_lv >= 0) { levels[0] = single_lv; n_lv = 1; }
    else {
        for (int i = 0; i < 7; i++) levels[i] = st.Gray_Level[i];
        n_lv = 7;
    }

    /* ── Readback buffer ── */
    int total_px = panel_w * panel_h;
    std::vector<uint8_t> pixels(total_px * 4);

    printf("\n========================================\n");
    printf("GPU Rendering: %dx%d, %d level(s)\n", panel_w, panel_h, n_lv);
    printf("========================================\n\n");

    auto t_all_start = std::chrono::high_resolution_clock::now();

    for (int li = 0; li < n_lv; li++) {
        int gl = levels[li];

        /* ── GPU 渲染 + 計時 ── */
        auto tA = std::chrono::high_resolution_clock::now();
        render_frame(gl, panel_w, panel_h);
        auto tB = std::chrono::high_resolution_clock::now();
        double ms_gpu = std::chrono::duration_cast<std::chrono::microseconds>(tB-tA).count()/1000.0;

        /* ── Readback ── */
        auto tC = std::chrono::high_resolution_clock::now();
        glReadPixels(0, 0, panel_w, panel_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        auto tD = std::chrono::high_resolution_clock::now();
        double ms_read = std::chrono::duration_cast<std::chrono::microseconds>(tD-tC).count()/1000.0;

        /* ── 存檔 ── */
        char bmp_path[512], csv_path[512], base[512];
        strncpy(base, out_base, sizeof(base)-1);
        char *dot = strrchr(base, '.'); if (dot) *dot = '\0';
        snprintf(bmp_path, sizeof(bmp_path), "%s_%d_result.bmp", base, gl);
        snprintf(csv_path, sizeof(csv_path), "%s_%d_value.csv",  base, gl);

        auto tE = std::chrono::high_resolution_clock::now();
        save_bmp(bmp_path, panel_w, panel_h, pixels.data());
        save_csv(csv_path, panel_w, panel_h, pixels.data());
        auto tF = std::chrono::high_resolution_clock::now();
        double ms_io = std::chrono::duration_cast<std::chrono::microseconds>(tF-tE).count()/1000.0;

        printf("[Level %3d]  GPU: %7.2f ms | Readback: %7.2f ms | File IO: %7.2f ms | Total: %7.2f ms\n",
               gl, ms_gpu, ms_read, ms_io, ms_gpu + ms_read + ms_io);
    }

    auto t_all_end = std::chrono::high_resolution_clock::now();
    double ms_all = std::chrono::duration_cast<std::chrono::microseconds>(t_all_end-t_all_start).count()/1000.0;
    printf("\n========================================\n");
    printf("All %d levels done in %.1f ms\n", n_lv, ms_all);
    printf("========================================\n");

    /* ── 清理 ── */
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fboTex);
    glDeleteTextures(1, &texIdxHigh);
    glDeleteTextures(1, &texIdxLow);
    glDeleteTextures(1, &texRule);
    glDeleteProgram(programID);

    free(st.Idx_R); free(st.Idx_G); free(st.Idx_B);

    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}