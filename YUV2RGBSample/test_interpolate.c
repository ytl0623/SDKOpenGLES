
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

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

#define SCENE_WIDTH 1920
#define SCENE_HEIGHT 1080

using std::string;
using std::vector;

// ... (Timer 類別與 BMP 結構定義保持不變) ...
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
        // printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0);
    }
};

#pragma pack(push, 1)

// BMP 檔案標頭 (File Header) - 共 14 bytes
typedef struct {
    uint16_t type;        // 檔案類型標記，必須是 0x4D42 (ASCII 的 'BM')
    uint32_t size;        // 整個檔案的大小 (bytes)
    uint16_t reserved1;   // 保留欄位，必須為 0
    uint16_t reserved2;   // 保留欄位，必須為 0
    uint32_t offset;      // 像素資料在檔案中的起始偏移量 (Offset)
} BMPFileHeader;

// BMP 資訊標頭 (Info Header) - 共 40 bytes (Windows V3 Header)
typedef struct {
    uint32_t size;              // 此結構體的大小 (通常為 40)
    int32_t width;              // 圖像寬度 (pixels)
    int32_t height;             // 圖像高度 (pixels)
    uint16_t planes;            // 色彩平面數，必須為 1
    uint16_t bits;              // 每像素位元數 (如 24 代表 RGB 888)
    uint32_t compression;       // 壓縮類型 (0 = BI_RGB 無壓縮)
    uint32_t imagesize;         // 原始點陣圖資料大小 (bytes)
    int32_t xresolution;        // 水平解析度 (像素/米)
    int32_t yresolution;        // 垂直解析度 (像素/米)
    uint32_t ncolours;          // 調色盤使用的顏色數 (0 代表全部)
    uint32_t importantcolours;  // 重要顏色數
} BMPInfoHeader;

#pragma pack(pop)

// ============================================================================
// 修改後的 VideoReader (移除 sws_scale，直接輸出 raw frame)
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

        const AVCodec *codec = NULL;
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
        
        // 這裡不再需要 sws_getContext，因為我們不做 CPU 轉換了
        return true;
    }

    // 修改：直接回傳是否讀取成功，數據保留在 frame 成員變數中供外部存取
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

// 修改：需要 3 個紋理位置變數 (Y, U, V)
GLint iLocTextureY = -1;
GLint iLocTextureU = -1;
GLint iLocTextureV = -1;

GLint iLocControlPoint[5];
GLint iLocFixedX = -1;

// 修改：需要 3 個紋理 ID
GLuint textureIdY;
GLuint textureIdU;
GLuint textureIdV;

GLuint controlPointTextureID[5];

const float FIXED_X[5] = {
    32.0f/255.0f,
    64.0f/255.0f,
    128.0f/255.0f,
    192.0f/255.0f,
    255.0f/255.0f
};

const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,  // 左下
     1.0f, -1.0f,  // 右下
    -1.0f,  1.0f,  // 左上
     1.0f,  1.0f   // 右上
};

const GLfloat textureVertices[] = {
    0.0f, 0.0f,  // 左下
    1.0f, 0.0f,  // 右下
    0.0f, 1.0f,  // 左上
    1.0f, 1.0f   // 右上
};

bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("錯誤: 無法開啟檔案 %s\n", filename);
        return false;
    }
    
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    
    // 讀取標頭資訊
    if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 ||
        fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) {
        printf("錯誤: 讀取 BMP 標頭失敗 %s\n", filename);
        fclose(file);
        return false;
    }
    
    // 檢查魔術數字 (Magic Number)
    if (fileHeader.type != 0x4D42) {
        printf("錯誤: 不是有效的 BMP 檔案 %s\n", filename);
        fclose(file);
        return false;
    }
     
    // 本程式僅支援 24-bit (RGB) 格式
    if (infoHeader.bits != 24) {
        printf("錯誤: 僅支援 24-bit BMP %s\n", filename);
        fclose(file);
        return false;
    }
    
    width = infoHeader.width;
    height = std::abs(infoHeader.height); // 高度可能為負，表示由上而下儲存
    
    // BMP 的每一列 (Row) 資料長度必須是 4 bytes 的倍數
    // 計算每列包含 Padding 的實際位元組數
    int rowSize = ((width * 3 + 3) / 4) * 4;
    int imageSize = rowSize * height;
    
    vector<unsigned char> rawData(imageSize);
    fseek(file, fileHeader.offset, SEEK_SET); // 跳到像素資料開始處
    fread(&rawData[0], 1, imageSize, file);
    fclose(file);
    
    // 將 BGR (BMP 標準) 轉換為 RGB (OpenGL 標準) 並移除 Padding
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = y * rowSize + x * 3;      // 來源索引 (含 Padding)
            int dstIdx = y * width * 3 + x * 3;    // 目標索引 (緊密排列)
            
            // BMP 儲存順序為 B, G, R，需交換為 R, G, B
            data[dstIdx]     = rawData[srcIdx + 2]; // R
            data[dstIdx + 1] = rawData[srcIdx + 1]; // G
            data[dstIdx + 2] = rawData[srcIdx];     // B
        }
    }
    
    printf("成功載入: %s (%dx%d)\n", filename, width, height);
    return true;
}

// ============================================================================
// Vertex Shader (頂點著色器)
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

// 新的 Fragment Shader: YUV -> RGB 轉換 + Demura
const char* fragmentShaderSource = R"(
precision highp float;
varying vec2 vTexCoord;

// YUV 紋理輸入
uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

// 5張控制點紋理
uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;

// X軸座標定義
uniform float uFixedX[5];

// --- YUV 轉 RGB 函數 ---
// 使用 BT.709 標準 (適用於 HDTV/MP4 1920x1080)
// 如果顏色看起來太淡或太濃，可改回原本的 BT.601
vec3 yuv2rgb(vec2 uv) {
    float y = texture2D(uTextureY, uv).r;
    float u = texture2D(uTextureU, uv).r - 0.5;
    float v = texture2D(uTextureV, uv).r - 0.5;
    
    // BT.709 轉換矩陣 (HD 標準)
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    return vec3(r, g, b);
}

// 分段線性插值 (含原點 0,0 錨定邏輯)
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
    } else {
        x_low = x3;   y_low = y3;
        x_high = x4;  y_high = y4;
    }
    
    float denominator = x_high - x_low;
    if (denominator == 0.0) return y_high;

    float m = (y_high - y_low) / denominator;

    // 線性方程: y = y_low + m * (x - x_low)
    float y = y_low + m * (x - x_low);

    // 確保輸出值在 [0, 1] 範圍內 (防止溢出)
    return (y < 0.0f) ? 0.0f : ((y > 1.0f) ? 1.0f : y);
}

void main() {
    vec3 inputColor = yuv2rgb(vTexCoord);
 
    // R Channel Controls
    float y0_r = texture2D(uControlPoint0, vTexCoord).r;
    float y1_r = texture2D(uControlPoint1, vTexCoord).r;
    float y2_r = texture2D(uControlPoint2, vTexCoord).r;
    float y3_r = texture2D(uControlPoint3, vTexCoord).r;
    float y4_r = texture2D(uControlPoint4, vTexCoord).r;

    // G Channel Controls
    float y0_g = texture2D(uControlPoint0, vTexCoord).g;
    float y1_g = texture2D(uControlPoint1, vTexCoord).g;
    float y2_g = texture2D(uControlPoint2, vTexCoord).g;
    float y3_g = texture2D(uControlPoint3, vTexCoord).g;
    float y4_g = texture2D(uControlPoint4, vTexCoord).g;

    // B Channel Controls
    float y0_b = texture2D(uControlPoint0, vTexCoord).b;
    float y1_b = texture2D(uControlPoint1, vTexCoord).b;
    float y2_b = texture2D(uControlPoint2, vTexCoord).b;
    float y3_b = texture2D(uControlPoint3, vTexCoord).b;
    float y4_b = texture2D(uControlPoint4, vTexCoord).b;

    // 3. 執行插值補償
    float newR = interpolate(inputColor.r, y0_r, y1_r, y2_r, y3_r, y4_r);
    float newG = interpolate(inputColor.g, y0_g, y1_g, y2_g, y3_g, y4_g);
    float newB = interpolate(inputColor.b, y0_b, y1_b, y2_b, y3_b, y4_b);


    // 4. 輸出最終顏色
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
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char* infoLog = (char*)malloc(infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            printf("Shader 編譯失敗:\n%s\n", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool prepareGraphics(const char* videoFile, const char* controlFiles[5]) {
    if (!videoReader.open(videoFile)) {
        return false;
    }
    
    vector<unsigned char> controlData[5];
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(controlFiles[i], controlData[i], w, h)) {
            return false;
        }

        // 檢查尺寸一致性：控制圖必須與原圖大小相同
        if (w != imageWidth || h != imageHeight) {
            printf("錯誤: 控制點圖片尺寸 (%dx%d) 與原圖不符\n", w, h);
            return false;
        }
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
    
    // 獲取新的 Uniform Locations
    iLocTextureY = glGetUniformLocation(programID, "uTextureY");
    iLocTextureU = glGetUniformLocation(programID, "uTextureU");
    iLocTextureV = glGetUniformLocation(programID, "uTextureV");
    
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);

    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    // Texture Y
    glGenTextures(1, &textureIdY);
    glBindTexture(GL_TEXTURE_2D, textureIdY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width, videoReader.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture U: 寬/2 x 高/2 (YUV420p)
    glGenTextures(1, &textureIdU);
    glBindTexture(GL_TEXTURE_2D, textureIdU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture V: 寬/2 x 高/2
    glGenTextures(1, &textureIdV);
    glBindTexture(GL_TEXTURE_2D, textureIdV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 7. 建立控制點紋理 (保持不變)
    for (int i = 0; i < 5; i++) {
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
    
    printf("OpenGL 初始化完成。\n");
    return true;
}

void GraphicsUpdate() {
    // 1. 從 FFmpeg 讀取下一幀 (資料在 videoReader.frame 中)
    if (videoReader.readNextFrame()) {
        // 2. 分別更新 Y, U, V 紋理
        // 注意：data[0] 是 Y, data[1] 是 U, data[2] 是 V
        
        // 更新 Y 平面
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureIdY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width, videoReader.height, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[0]);

        // 更新 U 平面 (寬高減半)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textureIdU);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[1]);

        // 更新 V 平面 (寬高減半)
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textureIdV);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[2]);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

    // 3. 綁定紋理單元並傳給 Shader
    // Unit 0: Y
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureIdY);
    glUniform1i(iLocTextureY, 0);

    // Unit 1: U
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureIdU);
    glUniform1i(iLocTextureU, 1);

    // Unit 2: V
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textureIdV);
    glUniform1i(iLocTextureV, 2);

    // Unit 3~7: 控制點 (原本是 1~5，現在往後順延)
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE3 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 3 + i);
    }

    glUniform2fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("使用方法: %s <影片.mp4> <點1.bmp> <點2.bmp> <點3.bmp> <點4.bmp> <點5.bmp>\n", argv[0]);
        return 1;
    }

    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};

    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

    if (!prepareGraphics(argv[1], controlFiles)) {
        printf("初始化失敗\n");
        return 1;
    }

    printf("\n--- GPU 加速播放與補償 (YUV Direct) ---\n");
    
    bool end = false;
    int frame_count = 0;

    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

        auto start_time = std::chrono::high_resolution_clock::now();

        GraphicsUpdate();
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);

        frame_count++;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        printf("Frame: %d | Cost: %.3f ms\n", frame_count, elapsed);
    }

    // 清理
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