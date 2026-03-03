#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <stdio.h>
#include <cmath>
#include <thread>

// --- 用於 Linux 終端機鍵盤偵測的標頭檔 ---
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

// --- 修改 1：解析度改為 1920x1080 ---
#define SCENE_WIDTH 1920
#define SCENE_HEIGHT 1080

using std::string;
using std::vector;

// ============================================================================
// Linux 終端機非阻塞鍵盤偵測函數 (kbhit)
// ============================================================================
int kbhit(void) {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // 關閉緩衝區與回顯
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK); // 設為非阻塞

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

// ============================================================================
// 工具類別：計時器 (Timer) 與 BMP 結構
// ============================================================================
class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
    const char* name;
public:
    Timer(const char* timer_name) : name(timer_name) {
        start_time = std::chrono::high_resolution_clock::now();
    }
    ~Timer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    }
};

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} BMPFileHeader;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits;
    uint32_t compression;
    uint32_t imagesize;
    int32_t xresolution;
    int32_t yresolution;
    uint32_t ncolours;
    uint32_t importantcolours;
} BMPInfoHeader;
#pragma pack(pop)

// ============================================================================
// VideoReader 類別
// ============================================================================
class VideoReader {
public:
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    int video_stream_index = -1;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int width = 0;
    int height = 0;

    VideoReader() {}

    ~VideoReader() {
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (format_ctx) avformat_close_input(&format_ctx);
    }

    bool open(const char* filename) {
        if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) != 0) return false;
        if (avformat_find_stream_info(format_ctx, nullptr) < 0) return false;

        AVCodec *codec = NULL; // 根據您先前的修復移除了 const
        video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (video_stream_index < 0) return false;

        codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar);

        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;

        width = codec_ctx->width;
        height = codec_ctx->height;
        printf("影片資訊: %dx%d, Codec: %s (YUV420P)\n", width, height, codec->name);

        frame = av_frame_alloc();
        packet = av_packet_alloc();
        return true;
    }

    bool readNextFrame() {
        while (av_read_frame(format_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                if (avcodec_send_packet(codec_ctx, packet) == 0) {
                    int ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == 0) {
                        av_packet_unref(packet);
                        return true;
                    }
                }
            }
            av_packet_unref(packet);
        }
        av_seek_frame(format_ctx, video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
        return false; 
    }
};

// ============================================================================
// 全域變數
// ============================================================================
VideoReader videoReader;
string resourceDirectory = "Supportingfiles/";

GLuint programID;
GLint iLocPosition = -1;
GLint iLocTexCoord = -1;

GLint iLocTextureY = -1;
GLint iLocTextureU = -1;
GLint iLocTextureV = -1;

// --- 修改 2：控制點改為 5 個 ---
GLint iLocControlPoint[5];
GLint iLocFixedX = -1;
GLint iLocEnableDemura = -1;

GLuint textureIdY;
GLuint textureIdU;
GLuint textureIdV;

GLuint controlPointTextureID[5];

int imageWidth = 1920;
int imageHeight = 1080; // --- 對應修改為 1080 ---

// --- 修改 3：定義 5 個控制點的 X 軸亮度分佈 ---
const float FIXED_X[5] = {
    32.0f/255.0f,
    64.0f/255.0f,
    128.0f/255.0f,
    192.0f/255.0f,
    224.0f/255.0f
};

const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f
};

const GLfloat textureVertices[] = {
    0.0f, 1.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 0.0f
};

// 讀取 BMP 圖片函式
bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("錯誤: 無法開啟檔案 %s\n", filename);
        return false;
    }
    
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    
    if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 ||
        fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) {
        fclose(file);
        return false;
    }
    
    if (fileHeader.type != 0x4D42 || infoHeader.bits != 24) {
        fclose(file);
        return false;
    }
    
    width = infoHeader.width;
    height = std::abs(infoHeader.height);
    
    int rowSize = ((width * 3 + 3) / 4) * 4;
    int imageSize = rowSize * height;
    
    vector<unsigned char> rawData(imageSize);
    fseek(file, fileHeader.offset, SEEK_SET);
    fread(&rawData[0], 1, imageSize, file);
    fclose(file);
    
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = y * rowSize + x * 3;
            int dstIdx = y * width * 3 + x * 3;
            
            data[dstIdx]     = rawData[srcIdx + 2];
            data[dstIdx + 1] = rawData[srcIdx + 1];
            data[dstIdx + 2] = rawData[srcIdx];
        }
    }
    return true;
}

// ============================================================================
// Vertex Shader
// ============================================================================
const char* vertexShaderSource = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

// ============================================================================
// Fragment Shader
// ============================================================================
const char* fragmentShaderSource = R"(
precision highp float;
varying vec2 vTexCoord;

uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

// --- 修改 4：Shader 內增加 uControlPoint4 ---
uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;

// X軸改為 5 點
uniform float uFixedX[5];

uniform int uEnableDemura;

vec3 yuv2rgb(vec2 uv) {
    float y = texture2D(uTextureY, uv).r;
    float u = texture2D(uTextureU, uv).r - 0.5;
    float v = texture2D(uTextureV, uv).r - 0.5;
    
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    return vec3(r, g, b);
}

// --- 修改 5：內插演算法擴充為接收 5 個點的目標值 ---
float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    float x0 = uFixedX[0];
    float x1 = uFixedX[1];
    float x2 = uFixedX[2];
    float x3 = uFixedX[3];
    float x4 = uFixedX[4];
    
    float x_low, x_high, y_low, y_high;
    
    if (x < x0) {
        x_low = 0.0;  y_low = 0.0; 
        x_high = x0;  y_high = y0;
    } else if (x < x1) {
        x_low = x0;   y_low = y0;
        x_high = x1;  y_high = y1;
    } else if (x < x2) {
        x_low = x1;   y_low = y1;
        x_high = x2;  y_high = y2;
    } else if (x < x3) {
        x_low = x2;   y_low = y2;
        x_high = x3;  y_high = y3;
    } else if (x < x4) {
        x_low = x3;   y_low = y3;
        x_high = x4;  y_high = y4;
    } else {
        x_low = x4;   y_low = y4;
        x_high = 1.0; y_high = 1.0;
    }
    
    float denominator = x_high - x_low;
    if (denominator == 0.0) return y_high;

    float m = (y_high - y_low) / denominator;
    float y = y_low + m * (x - x_low);

    return (y < 0.0) ? 0.0 : ((y > 1.0) ? 1.0 : y);
}

void main() {
    vec3 inputColor = yuv2rgb(vTexCoord);

    if (uEnableDemura == 0) {
        gl_FragColor = vec4(inputColor, 1.0);
        return;
    }

    float r0 = texture2D(uControlPoint0, vTexCoord).r;
    float r1 = texture2D(uControlPoint1, vTexCoord).r;
    float r2 = texture2D(uControlPoint2, vTexCoord).r;
    float r3 = texture2D(uControlPoint3, vTexCoord).r;
    float r4 = texture2D(uControlPoint4, vTexCoord).r; // 新增

    float g0 = texture2D(uControlPoint0, vTexCoord).g;
    float g1 = texture2D(uControlPoint1, vTexCoord).g;
    float g2 = texture2D(uControlPoint2, vTexCoord).g;
    float g3 = texture2D(uControlPoint3, vTexCoord).g;
    float g4 = texture2D(uControlPoint4, vTexCoord).g; // 新增

    float b0 = texture2D(uControlPoint0, vTexCoord).b;
    float b1 = texture2D(uControlPoint1, vTexCoord).b;
    float b2 = texture2D(uControlPoint2, vTexCoord).b;
    float b3 = texture2D(uControlPoint3, vTexCoord).b;
    float b4 = texture2D(uControlPoint4, vTexCoord).b; // 新增

    float newR = interpolate(inputColor.r, r0, r1, r2, r3, r4);
    float newG = interpolate(inputColor.g, g0, g1, g2, g3, g4);
    float newB = interpolate(inputColor.b, b0, b1, b2, b3, b4);

    gl_FragColor = vec4(newR, newG, newB, 1.0);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool prepareGraphics(const char* videoFile, const char* controlFiles[5]) { // 參數改為 5
    if (!videoReader.open(videoFile)) return false;
    
    vector<unsigned char> controlData[5];
    for (int i = 0; i < 5; i++) { // 迴圈改為 5
        int w, h;
        if (!loadBMP(controlFiles[i], controlData[i], w, h)) return false;
    }

    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (vertShader == 0 || fragShader == 0) return false;

    programID = glCreateProgram();
    glAttachShader(programID, vertShader);
    glAttachShader(programID, fragShader);
    glLinkProgram(programID);
    glUseProgram(programID);

    iLocPosition = glGetAttribLocation(programID, "aPosition");
    iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    
    iLocTextureY = glGetUniformLocation(programID, "uTextureY");
    iLocTextureU = glGetUniformLocation(programID, "uTextureU");
    iLocTextureV = glGetUniformLocation(programID, "uTextureV");
    
    // --- 修改 6：增加取得第 5 個控制點位置 ---
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    iLocEnableDemura = glGetUniformLocation(programID, "uEnableDemura");

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);

    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    glGenTextures(1, &textureIdY);
    glBindTexture(GL_TEXTURE_2D, textureIdY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width, videoReader.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &textureIdU);
    glBindTexture(GL_TEXTURE_2D, textureIdU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &textureIdV);
    glBindTexture(GL_TEXTURE_2D, textureIdV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    for (int i = 0; i < 5; i++) { // 迴圈改為 5
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, imageWidth, imageHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &controlData[i][0]);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    
    return true;
}

void GraphicsUpdate(bool hasNewFrame, bool enableDemura) {
    if (hasNewFrame) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureIdY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width, videoReader.height, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[0]);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textureIdU);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[1]);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textureIdV);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[2]);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, textureIdY); glUniform1i(iLocTextureY, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textureIdU); glUniform1i(iLocTextureU, 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, textureIdV); glUniform1i(iLocTextureV, 2);

    // --- 修改 7：將控制圖綁定到 GL_TEXTURE3 ~ GL_TEXTURE7 ---
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE3 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 3 + i);
    }
    glUniform1fv(iLocFixedX, 5, (GLfloat*)FIXED_X); // 參數改為 5
    
    glUniform1i(iLocEnableDemura, enableDemura ? 1 : 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ============================================================================
// 主程式 (Entry Point)
// ============================================================================
int main(int argc, char* argv[]) {
    // --- 修改 8：執行所需參數變成 7 個 (執行檔 + 影片檔 + 5張控制點圖) ---
    if (argc != 7) {
        printf("使用方法: %s <影片.mp4> <點1.bmp> <點2.bmp> <點3.bmp> <點4.bmp> <點5.bmp>\n", argv[0]);
        return 1;
    }
    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};

    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
    eglSwapInterval(CoreEGL::display, 0);

    if (!prepareGraphics(argv[1], controlFiles)) return 1;

    printf("\n=======================================================\n");
    printf("  使用提示：請在「終端機視窗」中按下【空白鍵 (Space)】\n");
    printf("  即可切換 (Toggle) DEMURA 校正效果與顯示原圖。\n");
    printf("=======================================================\n\n");
    
    bool end = false;
    int frame_count = 0;
    bool isDemuraEnabled = true;

    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

        if (kbhit()) {
            char ch = getchar();
            if (ch == ' ') { 
                isDemuraEnabled = !isDemuraEnabled;
                printf("\n>>> 目前狀態： %s <<<\n\n", isDemuraEnabled ? "DEMURA 校正中" : "顯示原圖");
            }
        }

        auto start_total = std::chrono::high_resolution_clock::now();
        auto start_cpu = std::chrono::high_resolution_clock::now();
        
        bool hasNewFrame = videoReader.readNextFrame();
        
        auto end_cpu = std::chrono::high_resolution_clock::now();

        glFinish(); 
        
        auto start_gpu = std::chrono::high_resolution_clock::now();

        GraphicsUpdate(hasNewFrame, isDemuraEnabled);

        glFinish();

        auto end_gpu = std::chrono::high_resolution_clock::now();
        
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);

        auto end_total = std::chrono::high_resolution_clock::now();

        double ms_cpu_decode = std::chrono::duration_cast<std::chrono::microseconds>(end_cpu - start_cpu).count() / 1000.0;
        double ms_gpu_render = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count() / 1000.0;
        double ms_total = std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total).count() / 1000.0;
        
        double ms_wait = ms_total - (ms_cpu_decode + ms_gpu_render);
        if (ms_wait < 0) ms_wait = 0;

        frame_count++;
        
        if (frame_count % 60 == 0) {
            printf("Frame %d | GPU: %6.3f ms | Total: %6.3f ms | Idle: %6.3f ms\r", 
                   frame_count, ms_gpu_render, ms_total, ms_wait);
            fflush(stdout);
        }
    }

    // --- 修改 9：確保釋放 5 個紋理資源 ---
    glDeleteTextures(1, &textureIdY);
    glDeleteTextures(1, &textureIdU);
    glDeleteTextures(1, &textureIdV);
    glDeleteTextures(5, controlPointTextureID);
    
    glDeleteProgram(programID);
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}