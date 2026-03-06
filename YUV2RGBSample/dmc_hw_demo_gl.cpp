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
#include <signal.h>

/* Terminal non-blocking keyboard input */
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

/* ═══════════════════════════════════════════════════════════════════════
 * Ctrl+C 安全停止
 * ═══════════════════════════════════════════════════════════════════════ */
static volatile sig_atomic_t g_running = 1;
static void sig_handler(int) { g_running = 0; }

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 1: BIN 解析
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

    int ptr=P+256+2;
    int lut2ch=4096*7, rules=(int)s.Max_rule_number;
    memset(s.Rule_R,0,sizeof(s.Rule_R));
    memset(s.Rule_G,0,sizeof(s.Rule_G));
    memset(s.Rule_B,0,sizeof(s.Rule_B));
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_R[i/7][i%7]=bin[ptr];ptr++;}
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_G[i/7][i%7]=bin[ptr];ptr++;}
    for(int i=0;i<lut2ch;i++){if(i/7<rules)s.Rule_B[i/7][i%7]=bin[ptr];ptr++;}

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
 * ═══════════════════════════════════════════════════════════════════════ */

GLuint texIdxHigh, texIdxLow, texRule;

static void prepare_textures(const DMC_State &s)
{
    int W = s.Idx_table_W, H = s.Idx_table_H;
    int sz = W * H;

    std::vector<uint8_t> idxH(sz * 3), idxL(sz * 3);
    for (int i = 0; i < sz; i++) {
        int r = (int)s.Idx_R[i], g = (int)s.Idx_G[i], b = (int)s.Idx_B[i];
        idxH[i*3+0] = (r >> 8) & 0xFF;  idxL[i*3+0] = r & 0xFF;
        idxH[i*3+1] = (g >> 8) & 0xFF;  idxL[i*3+1] = g & 0xFF;
        idxH[i*3+2] = (b >> 8) & 0xFF;  idxL[i*3+2] = b & 0xFF;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

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
 * SECTION 3: Fragment Shader — 加入 uBypass 開關
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

uniform sampler2D uIdxHigh;
uniform sampler2D uIdxLow;
uniform sampler2D uRuleTable;

uniform float uLV0, uLV1, uLV2, uLV3, uLV4;
uniform float uLV5, uLV6, uLV7, uLV8, uLV9;
uniform float uCO0, uCO1, uCO2, uCO3, uCO4, uCO5, uCO6, uCO7;

uniform float uOR0,uOR1,uOR2,uOR3,uOR4,uOR5,uOR6;
uniform float uOG0,uOG1,uOG2,uOG3,uOG4,uOG5,uOG6;
uniform float uOB0,uOB1,uOB2,uOB3,uOB4,uOB5,uOB6;
uniform float uMR0,uMR1,uMR2,uMR3,uMR4,uMR5,uMR6;
uniform float uMG0,uMG1,uMG2,uMG3,uMG4,uMG5,uMG6;
uniform float uMB0,uMB1,uMB2,uMB3,uMB4,uMB5,uMB6;

uniform float uIdxW, uIdxH;
uniform float uHSize, uVSize;
uniform float uGrayLevel;
uniform float uScreenW, uScreenH;
uniform float uPanelW, uPanelH;

/* ★ 0.0 = DMC 補償, 1.0 = 原圖 bypass ★ */
uniform float uBypass;

float hw_round(float n) {
    return (n > 0.0) ? floor(n + 0.5) : ceil(n - 0.5);
}

float getLV(float idx) {
    if (idx < 0.5) return uLV0; if (idx < 1.5) return uLV1;
    if (idx < 2.5) return uLV2; if (idx < 3.5) return uLV3;
    if (idx < 4.5) return uLV4; if (idx < 5.5) return uLV5;
    if (idx < 6.5) return uLV6; if (idx < 7.5) return uLV7;
    if (idx < 8.5) return uLV8; return uLV9;
}
float getCOEF(float idx) {
    if (idx < 0.5) return uCO0; if (idx < 1.5) return uCO1;
    if (idx < 2.5) return uCO2; if (idx < 3.5) return uCO3;
    if (idx < 4.5) return uCO4; if (idx < 5.5) return uCO5;
    if (idx < 6.5) return uCO6; return uCO7;
}
float getOffset(float ch, float idx) {
    if (ch < 0.5) {
        if (idx<0.5) return uOR0; if (idx<1.5) return uOR1;
        if (idx<2.5) return uOR2; if (idx<3.5) return uOR3;
        if (idx<4.5) return uOR4; if (idx<5.5) return uOR5; return uOR6;
    } else if (ch < 1.5) {
        if (idx<0.5) return uOG0; if (idx<1.5) return uOG1;
        if (idx<2.5) return uOG2; if (idx<3.5) return uOG3;
        if (idx<4.5) return uOG4; if (idx<5.5) return uOG5; return uOG6;
    } else {
        if (idx<0.5) return uOB0; if (idx<1.5) return uOB1;
        if (idx<2.5) return uOB2; if (idx<3.5) return uOB3;
        if (idx<4.5) return uOB4; if (idx<5.5) return uOB5; return uOB6;
    }
}
float getMag(float ch, float idx) {
    if (ch < 0.5) {
        if (idx<0.5) return uMR0; if (idx<1.5) return uMR1;
        if (idx<2.5) return uMR2; if (idx<3.5) return uMR3;
        if (idx<4.5) return uMR4; if (idx<5.5) return uMR5; return uMR6;
    } else if (ch < 1.5) {
        if (idx<0.5) return uMG0; if (idx<1.5) return uMG1;
        if (idx<2.5) return uMG2; if (idx<3.5) return uMG3;
        if (idx<4.5) return uMG4; if (idx<5.5) return uMG5; return uMG6;
    } else {
        if (idx<0.5) return uMB0; if (idx<1.5) return uMB1;
        if (idx<2.5) return uMB2; if (idx<3.5) return uMB3;
        if (idx<4.5) return uMB4; if (idx<5.5) return uMB5; return uMB6;
    }
}

vec2 idxUV(float col, float row) {
    return vec2((col + 0.5) / uIdxW, (row + 0.5) / uIdxH);
}
float readIdx(vec2 uv, float ch) {
    vec3 hi = texture2D(uIdxHigh, uv).rgb;
    vec3 lo = texture2D(uIdxLow, uv).rgb;
    float h, l;
    if (ch < 0.5)      { h = hi.r; l = lo.r; }
    else if (ch < 1.5)  { h = hi.g; l = lo.g; }
    else                { h = hi.b; l = lo.b; }
    return floor(h * 255.0 + 0.5) * 256.0 + floor(l * 255.0 + 0.5);
}
float readRule(float idx, float lv, float ch) {
    vec2 uv = vec2((idx + 0.5) / 4096.0, (lv + 0.5) / 7.0);
    vec3 val = texture2D(uRuleTable, uv).rgb;
    if (ch < 0.5)      return floor(val.r * 255.0 + 0.5);
    else if (ch < 1.5)  return floor(val.g * 255.0 + 0.5);
    else                return floor(val.b * 255.0 + 0.5);
}

float find_plane(float pt) {
    if (pt >  uLV9)                              return 9.0;
    if (pt >= uLV8 && abs(uLV8 - uLV9) > 0.5)   return 8.0;
    if (pt >= uLV7) return 7.0; if (pt >= uLV6) return 6.0;
    if (pt >= uLV5) return 5.0; if (pt >= uLV4) return 4.0;
    if (pt >= uLV3) return 3.0; if (pt >= uLV2) return 2.0;
    if (pt >= uLV1) return 1.0; return 0.0;
}
float block_interp(float A, float B, float C, float D, float H, float V) {
    float HAB = A + (B - A) * H;  HAB = hw_round(HAB * 4.0) / 4.0;
    float HCD = C + (D - C) * H;  HCD = hw_round(HCD * 4.0) / 4.0;
    float r = HAB + (HCD - HAB) * V;
    return hw_round(r * 4.0) / 4.0;
}
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
float plane_lerp(float x, float x1, float v1, float v2, float coef) {
    float y = x - x1;
    if (y < 0.0) y = 0.0;
    y = y * coef / 262144.0;
    y = hw_round(y * 16384.0) / 16384.0;
    if (y >= 1.0) y = 1.0 - (1.0 / 16384.0);
    float d = v2 - v1;  if (d < 0.0) d = 0.0;
    y = y * d;
    y = hw_round(y * 16.0) / 16.0;
    if (y > 1023.9375) y = 1023.9375;
    y = y + v1;
    return hw_round(y * 4.0) / 4.0;
}

float dmc_channel(float in12, float ch, float px, float py) {
    float bl = find_plane(in12);
    if (bl < 0.5 || bl > 8.5) return in12;

    float HS = uHSize, VS = uVSize;
    float bx = floor(px / HS), by = floor(py / VS);

    float iA = readIdx(idxUV(bx, by), ch);
    float iB = (HS > 1.5) ? readIdx(idxUV(bx+1.0, by), ch) : 0.0;
    float iC = (VS > 1.5) ? readIdx(idxUV(bx, by+1.0), ch) : 0.0;
    float iD = (HS > 1.5 && VS > 1.5) ? readIdx(idxUV(bx+1.0, by+1.0), ch) : 0.0;

    float Hf = mod(px, HS) / HS;
    float Vf = mod(py, VS) / VS;

    float V1;
    if (bl < 1.5) { V1 = getLV(bl); }
    else {
        float lv1 = bl - 2.0;
        float V1d = block_interp(readRule(iA,lv1,ch), readRule(iB,lv1,ch),
                                  readRule(iC,lv1,ch), readRule(iD,lv1,ch), Hf, Vf);
        V1 = getLV(bl) + getOffset(ch, bl - 2.0);
        if (V1 > 1023.75) V1 = 1023.75;
        V1 = mag_point(getMag(ch, bl - 2.0), V1d) + V1;
    }
    float V2;
    if (bl > 7.5) { V2 = getLV(bl + 1.0); }
    else {
        float lv2 = bl - 1.0;
        float V2d = block_interp(readRule(iA,lv2,ch), readRule(iB,lv2,ch),
                                  readRule(iC,lv2,ch), readRule(iD,lv2,ch), Hf, Vf);
        V2 = getLV(bl + 1.0) + getOffset(ch, bl - 1.0);
        if (V2 > 1023.75) V2 = 1023.75;
        V2 = mag_point(getMag(ch, bl - 1.0), V2d) + V2;
    }
    V1 = clamp(V1, 0.0, 1023.9375);
    V2 = clamp(V2, 0.0, 1023.9375);
    return plane_lerp(in12, getLV(bl), V1, V2, getCOEF(bl - 1.0));
}

void main() {
    /* ★ Bypass: 直接輸出原始灰階 ★ */
    if (uBypass > 0.5) {
        float c = clamp(uGrayLevel / 255.0, 0.0, 1.0);
        gl_FragColor = vec4(c, c, c, 1.0);
        return;
    }

    /* DMC 補償 */
    float px = floor(gl_FragCoord.x * uPanelW / uScreenW);
    float py = floor(gl_FragCoord.y * uPanelH / uScreenH);
    if (px >= uPanelW || py >= uPanelH) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float in12 = uGrayLevel * 4.0;
    float r = dmc_channel(in12, 0.0, px, py);
    float g = dmc_channel(in12, 1.0, px, py);
    float b = dmc_channel(in12, 2.0, px, py);

    gl_FragColor = vec4(
        clamp(r / 4.0 / 255.0, 0.0, 1.0),
        clamp(g / 4.0 / 255.0, 0.0, 1.0),
        clamp(b / 4.0 / 255.0, 0.0, 1.0),
        1.0);
}
)";

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 4: OpenGL Setup
 * ═══════════════════════════════════════════════════════════════════════ */

GLuint programID;
GLint  locBypass, locGrayLevel;

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

static bool setup_gl(const DMC_State &s, int screenW, int screenH)
{
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

    GLint aP = glGetAttribLocation(programID, "aPosition");
    GLint aT = glGetAttribLocation(programID, "aTexCoord");
    glEnableVertexAttribArray(aP);
    glVertexAttribPointer(aP, 2, GL_FLOAT, GL_FALSE, 0, quadVerts);
    glEnableVertexAttribArray(aT);
    glVertexAttribPointer(aT, 2, GL_FLOAT, GL_FALSE, 0, quadUVs);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texIdxHigh);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texIdxLow);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texRule);
    glUniform1i(glGetUniformLocation(programID, "uIdxHigh"),   0);
    glUniform1i(glGetUniformLocation(programID, "uIdxLow"),    1);
    glUniform1i(glGetUniformLocation(programID, "uRuleTable"), 2);

    float DMC_LEVEL[10] = {
        0, s.Level_B*4.0f, s.Level_1*4.0f, s.Level_2*4.0f,
        s.Level_3*4.0f, s.Level_4*4.0f, s.Level_5*4.0f,
        s.Level_6*4.0f, s.Level_7*4.0f, s.Level_W*4.0f+3.0f
    };
    const char *lvN[] = {"uLV0","uLV1","uLV2","uLV3","uLV4","uLV5","uLV6","uLV7","uLV8","uLV9"};
    for (int i = 0; i < 10; i++)
        glUniform1f(glGetUniformLocation(programID, lvN[i]), DMC_LEVEL[i]);

    const char *coN[] = {"uCO0","uCO1","uCO2","uCO3","uCO4","uCO5","uCO6","uCO7"};
    for (int i = 0; i < 8; i++)
        glUniform1f(glGetUniformLocation(programID, coN[i]), s.COEF[i]);

    auto decode = [](float raw) -> float {
        if (raw >= 2048)
            return -((float)(((~(unsigned)(int)raw) & 0x7FF) + 1) / 4.0f);
        else
            return raw / 4.0f;
    };
    float offR[7], offG[7], offB[7];
    for (int i = 0; i < 7; i++) {
        offR[i] = decode(s.Offset_R[i]);
        offG[i] = decode(s.Offset_G[i]);
        offB[i] = decode(s.Offset_B[i]);
    }
    const char *orN[]={"uOR0","uOR1","uOR2","uOR3","uOR4","uOR5","uOR6"};
    const char *ogN[]={"uOG0","uOG1","uOG2","uOG3","uOG4","uOG5","uOG6"};
    const char *obN[]={"uOB0","uOB1","uOB2","uOB3","uOB4","uOB5","uOB6"};
    const char *mrN[]={"uMR0","uMR1","uMR2","uMR3","uMR4","uMR5","uMR6"};
    const char *mgN[]={"uMG0","uMG1","uMG2","uMG3","uMG4","uMG5","uMG6"};
    const char *mbN[]={"uMB0","uMB1","uMB2","uMB3","uMB4","uMB5","uMB6"};
    for (int i = 0; i < 7; i++) {
        glUniform1f(glGetUniformLocation(programID, orN[i]), offR[i]);
        glUniform1f(glGetUniformLocation(programID, ogN[i]), offG[i]);
        glUniform1f(glGetUniformLocation(programID, obN[i]), offB[i]);
        glUniform1f(glGetUniformLocation(programID, mrN[i]), s.Mag_R[i]);
        glUniform1f(glGetUniformLocation(programID, mgN[i]), s.Mag_G[i]);
        glUniform1f(glGetUniformLocation(programID, mbN[i]), s.Mag_B[i]);
    }

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
    glUniform1f(glGetUniformLocation(programID, "uScreenW"), (float)screenW);
    glUniform1f(glGetUniformLocation(programID, "uScreenH"), (float)screenH);
    glUniform1f(glGetUniformLocation(programID, "uPanelW"),  (float)s.Width);
    glUniform1f(glGetUniformLocation(programID, "uPanelH"),  (float)s.Height);

    /* cache 頻繁切換的 uniform location */
    locBypass    = glGetUniformLocation(programID, "uBypass");
    locGrayLevel = glGetUniformLocation(programID, "uGrayLevel");
    glUniform1f(locBypass, 0.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    printf("OpenGL setup OK (direct display mode).\n");
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 5: Render
 * ═══════════════════════════════════════════════════════════════════════ */

static void render_to_screen(int gray_level, bool bypass, int screenW, int screenH)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenW, screenH);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texIdxHigh);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texIdxLow);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texRule);

    glUniform1f(locGrayLevel, (float)gray_level);
    glUniform1f(locBypass,    bypass ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 6: save_bmp / save_csv (已停用)
 * ═══════════════════════════════════════════════════════════════════════ */
#if 0
/* ... 略 ... */
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 7: Terminal 非阻塞鍵盤輸入
 * ═══════════════════════════════════════════════════════════════════════ */

static struct termios g_orig_termios;
static bool g_termios_saved = false;

static void init_terminal_input()
{
    /* 儲存原始終端設定 */
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    g_termios_saved = true;

    /* 設定 raw mode: 不等 Enter、不回顯 */
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;   /* non-blocking */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    /* stdin 設為非阻塞 */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    printf("Terminal keyboard input ready.\n");
}

static void cleanup_terminal_input()
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    }
}

enum KeyAction { KEY_NONE, KEY_SPACE, KEY_ESC };

static KeyAction poll_key()
{
    KeyAction action = KEY_NONE;
    char c;
    /* 一次讀完所有 pending 的按鍵，只取最後一個有效動作 */
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == ' ')       action = KEY_SPACE;
        else if (c == 27)   action = KEY_ESC;   /* ESC = 0x1B */
        else if (c == 'q' || c == 'Q') action = KEY_ESC;
    }
    return action;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SECTION 8: main — 永久迴圈 + 空白鍵切換 + CPU/GPU 計時
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: %s <input.bin> <width> <height> <gray_level>\n", argv[0]);
        printf("  [SPACE] Toggle DMC / Original\n");
        printf("  [ESC]   or [Q] or Ctrl+C to quit\n");
        return 1;
    }

    const char *bin_path = argv[1];
    int panel_w = atoi(argv[2]);
    int panel_h = atoi(argv[3]);
    int gray_lv = atoi(argv[4]);

    /* Ctrl+C handler */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

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
    if (!load_from_bin(st, bin, bsz)) {
        printf("ERROR: load_from_bin failed\n");
        free(bin); return 1;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("Load_From_BIN: %.1f ms\n",
           std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count()/1000.0);
    free(bin);

    /* ── EGL ── */
    int screenW = 1920, screenH = 1080;
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(screenW, screenH);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface,
                   CoreEGL::surface, CoreEGL::context);
    eglSwapInterval(CoreEGL::display, 0);
    printf("EGL init OK. GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));

    /* ── Terminal keyboard ── */
    init_terminal_input();

    /* ── Textures ── */
    auto t2 = std::chrono::high_resolution_clock::now();
    prepare_textures(st);
    auto t3 = std::chrono::high_resolution_clock::now();
    printf("Texture upload: %.1f ms\n",
           std::chrono::duration_cast<std::chrono::microseconds>(t3-t2).count()/1000.0);

    /* ── Shader & Uniforms ── */
    if (!setup_gl(st, screenW, screenH)) return 1;

    /* ══════════════════════════════════════════════════════════════════
     * 主迴圈
     * ══════════════════════════════════════════════════════════════════ */
    printf("\n========================================\n");
    printf("Panel %dx%d  Gray=%d\n", panel_w, panel_h, gray_lv);
    printf("[SPACE] Toggle DMC <-> Original\n");
    printf("[ESC] or [Q] or Ctrl+C to quit\n");
    printf("========================================\n\n");

    bool dmc_on = true;
    long total_frames = 0;
    long dmc_frames = 0, raw_frames = 0;
    double dmc_gpu_total_us = 0, raw_gpu_total_us = 0;
    double dmc_cpu_total_us = 0, raw_cpu_total_us = 0;

    auto t_loop = std::chrono::high_resolution_clock::now();
    auto t_report = t_loop;
    long frames_interval = 0;

    while (g_running) {
        /* ── 鍵盤 ── */
        KeyAction key = poll_key();
        if (key == KEY_ESC) break;
        if (key == KEY_SPACE) {
            dmc_on = !dmc_on;
            printf(">>> Mode: %s\n", dmc_on ? "DMC Compensated" : "Original (bypass)");
        }

        /* ── CPU: 送出 draw command ── */
        auto tA = std::chrono::high_resolution_clock::now();
        render_to_screen(gray_lv, !dmc_on, screenW, screenH);
        auto tB = std::chrono::high_resolution_clock::now();

        /* ── GPU: 等 GPU 完成 ── */
        glFinish();
        auto tC = std::chrono::high_resolution_clock::now();

        double cpu_us = std::chrono::duration_cast<std::chrono::nanoseconds>(tB - tA).count() / 1000.0;
        double gpu_us = std::chrono::duration_cast<std::chrono::nanoseconds>(tC - tB).count() / 1000.0;

        if (dmc_on) {
            dmc_cpu_total_us += cpu_us;
            dmc_gpu_total_us += gpu_us;
            dmc_frames++;
        } else {
            raw_cpu_total_us += cpu_us;
            raw_gpu_total_us += gpu_us;
            raw_frames++;
        }

        /* ── Swap ── */
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
        total_frames++;
        frames_interval++;

        /* ── 每秒印 FPS ── */
        auto tNow = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration_cast<std::chrono::microseconds>(
            tNow - t_report).count() / 1e6;

        if (sec >= 1.0) {
            double fps = frames_interval / sec;
            printf("[%s] Frame %ld | FPS: %.1f | CPU: %.3f ms | GPU: %.3f ms | Total: %.3f ms\n",
                   dmc_on ? "DMC" : "RAW",
                   total_frames, fps,
                   cpu_us / 1000.0, gpu_us / 1000.0,
                   (cpu_us + gpu_us) / 1000.0);
            t_report = tNow;
            frames_interval = 0;
        }
    }

    /* ══════════════════════════════════════════════════════════════════
     * 最終統計
     * ══════════════════════════════════════════════════════════════════ */
    auto t_end = std::chrono::high_resolution_clock::now();
    double total_sec = std::chrono::duration_cast<std::chrono::microseconds>(
        t_end - t_loop).count() / 1e6;

    printf("\n════════════════════════════════════════\n");
    printf("         PERFORMANCE SUMMARY\n");
    printf("════════════════════════════════════════\n");
    printf("Total: %ld frames in %.2f sec (%.1f FPS avg)\n\n",
           total_frames, total_sec, total_frames / total_sec);

    if (dmc_frames > 0) {
        double ac = dmc_cpu_total_us / dmc_frames / 1000.0;
        double ag = dmc_gpu_total_us / dmc_frames / 1000.0;
        printf("  [DMC Compensated]  %ld frames\n", dmc_frames);
        printf("    Avg CPU time : %7.3f ms\n", ac);
        printf("    Avg GPU time : %7.3f ms\n", ag);
        printf("    Avg Total    : %7.3f ms  (%.1f FPS)\n", ac+ag, 1000.0/(ac+ag));
    }
    if (raw_frames > 0) {
        double ac = raw_cpu_total_us / raw_frames / 1000.0;
        double ag = raw_gpu_total_us / raw_frames / 1000.0;
        printf("  [Original bypass]  %ld frames\n", raw_frames);
        printf("    Avg CPU time : %7.3f ms\n", ac);
        printf("    Avg GPU time : %7.3f ms\n", ag);
        printf("    Avg Total    : %7.3f ms  (%.1f FPS)\n", ac+ag, 1000.0/(ac+ag));
    }
    if (dmc_frames > 0 && raw_frames > 0) {
        double dt = (dmc_cpu_total_us + dmc_gpu_total_us) / dmc_frames;
        double rt = (raw_cpu_total_us + raw_gpu_total_us) / raw_frames;
        printf("\n  DMC overhead vs Original: %.2fx\n", dt / rt);
    }
    printf("════════════════════════════════════════\n");

    /* ── 清理 ── */
    cleanup_terminal_input();
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