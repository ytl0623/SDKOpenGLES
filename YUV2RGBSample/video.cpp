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
// 簡單的計時器類別，用於測量程式碼區塊的執行時間
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

// 設定結構體對齊為 1 byte，避免編譯器自動補齊造成讀取 BMP 標頭錯誤
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

#pragma pack(pop) // 恢復原本的對齊設定

// ============================================================================
// 修改後的 VideoReader (移除 sws_scale，直接輸出 raw frame)
// 用途：負責透過 FFmpeg 解碼影片，但不進行任何軟體顏色轉換
// ============================================================================
class VideoReader {
public:
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    int video_stream_index = -1;
    AVFrame* frame = nullptr;   // 存放解碼後的原始 YUV 數據
    AVPacket* packet = nullptr; // 存放解碼前的壓縮數據
    int width = 0;
    int height = 0;

    VideoReader() {}

    ~VideoReader() {
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (format_ctx) avformat_close_input(&format_ctx);
    }

    // 開啟影片檔並初始化解碼器
    bool open(const char* filename) {
        // 1. 打開輸入流
        if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) != 0) return false;
        // 2. 讀取串流資訊
        if (avformat_find_stream_info(format_ctx, nullptr) < 0) return false;

        // 3. 尋找最佳的視訊串流
        const AVCodec *codec = NULL;
        video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (video_stream_index < 0) return false;

        // 4. 配置解碼器上下文
        codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar);

        // 5. 開啟解碼器
        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;

        width = codec_ctx->width;
        height = codec_ctx->height;
        printf("影片資訊: %dx%d, Codec: %s (YUV420P)\n", width, height, codec->name);

        frame = av_frame_alloc();
        packet = av_packet_alloc();
        
        // 注意：這裡移除了 sws_getContext，因為我們要在 Shader 中處理 YUV 轉 RGB
        return true;
    }

    // 讀取並解碼下一幀
    // 回傳：true 代表成功讀取到一幀，false 代表影片結束或錯誤
    bool readNextFrame() {
        while (av_read_frame(format_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                // 發送壓縮封包給解碼器
                if (avcodec_send_packet(codec_ctx, packet) == 0) {
                    // 從解碼器接收解碼後的幀 (Raw Frame)
                    int ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == 0) {
                        av_packet_unref(packet);
                        return true; // 成功取得一幀 YUV 數據
                    }
                }
            }
            av_packet_unref(packet);
        }
        // 影片播放完畢，跳回開頭循環播放
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

// 修改：需要 3 個紋理位置變數 (分別對應 Y, U, V 平面)
GLint iLocTextureY = -1;
GLint iLocTextureU = -1;
GLint iLocTextureV = -1;

// 控制點紋理與參數
GLint iLocControlPoint[5];
GLint iLocFixedX = -1;

// 修改：需要 3 個紋理 ID 來儲存 Y, U, V 數據
GLuint textureIdY;
GLuint textureIdU;
GLuint textureIdV;

GLuint controlPointTextureID[5];

int imageWidth = 1920;
int imageHeight = 1080;

// 定義 5 個控制點的 X 軸亮度分佈 (0~1 之間)
const float FIXED_X[5] = {
    32.0f/255.0f,
    64.0f/255.0f,
    128.0f/255.0f,
    192.0f/255.0f,
    224.0f/255.0f
};

// 頂點座標 (全螢幕四邊形)
const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,  // 左下
     1.0f, -1.0f,  // 右下
    -1.0f,  1.0f,  // 左上
     1.0f,  1.0f   // 右上
};

// 紋理座標 (UV，左上角為 0,0)
const GLfloat textureVertices[] = {
    0.0f, 1.0f,  // 左下 (OpenGL 紋理座標 Y 軸向上，視情況調整)
    1.0f, 1.0f,  // 右下
    0.0f, 0.0f,  // 左上
    1.0f, 0.0f   // 右上
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
    
    // BMP 的每一列 (Row) 資料長度必須是 4 bytes 的倍數 (Padding)
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
// 用途：處理頂點位置與紋理座標的傳遞
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
// Fragment Shader (片段著色器) - 核心邏輯
// 用途：1. 將 YUV 轉為 RGB 
//       2. 讀取 5 張控制點圖
//       3. 根據當前像素亮度進行分段線性插值 (De-mura 補償)
// ============================================================================
const char* fragmentShaderSource = R"(
precision highp float;
varying vec2 vTexCoord;

// YUV 紋理輸入 (YUV420P 被拆成三個單通道紋理)
uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

// 5張控制點紋理 (用於亮度補償)
uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;

// X軸座標定義 (亮度分界點)
uniform float uFixedX[5];

// --- YUV 轉 RGB 函數 ---
// 使用 BT.709 標準 (適用於 HDTV/MP4 1920x1080)
// 這裡將 YUV 轉回 RGB 以便進行後續的顏色補償運算
vec3 yuv2rgb(vec2 uv) {
    float y = texture2D(uTextureY, uv).r;
    float u = texture2D(uTextureU, uv).r - 0.5; // GL讀取為 0~1，需平移回 -0.5~0.5
    float v = texture2D(uTextureV, uv).r - 0.5;
    
    // BT.709 轉換矩陣 (HD 標準)
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    return vec3(r, g, b);
}

// 分段線性插值函數 (含原點 0,0 錨定邏輯)
// x: 輸入的原始亮度值 (R, G, 或 B)
// y0~y4: 該像素位置在 5 張控制圖上的補償目標值
float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    float x0 = uFixedX[0];
    float x1 = uFixedX[1];
    float x2 = uFixedX[2];
    float x3 = uFixedX[3];
    float x4 = uFixedX[4];
    
    float x_low, x_high, y_low, y_high;
    
    // 搜尋 x 所在的區間 [x_low, x_high] 以及對應的 y 值
    if (x < x0) {
        // [區間 1] 0 (全黑) 到 P0 (0 ~ 32)
        x_low = 0.0;  y_low = 0.0; 
        x_high = x0;  y_high = y0;
    } else if (x < x1) {
        // [區間 2] P0 到 P1 (32 ~ 64)
        x_low = x0;   y_low = y0;
        x_high = x1;  y_high = y1;
    } else if (x < x2) {
        // [區間 3] P1 到 P2 (64 ~ 128)
        x_low = x1;   y_low = y1;
        x_high = x2;  y_high = y2;
    } else if (x < x3) {
        // [區間 4] P2 到 P3 (128 ~ 192)
        x_low = x2;   y_low = y2;
        x_high = x3;  y_high = y3;
    } else if (x < x4) {
        // [區間 5] P3 到 P4 (192 ~ 224)
        x_low = x3;   y_low = y3;
        x_high = x4;  y_high = y4;
    } else {
        // [區間 6] P4 到 255 (224 ~ 255)
        x_low = x4;   y_low = y4;
        x_high = 1.0; y_high = 1.0;
    }
    
    // 計算區間寬度
    float denominator = x_high - x_low;
    // 防止除以零
    if (denominator == 0.0) return y_high;

    // 計算斜率 m
    float m = (y_high - y_low) / denominator;

    // 點斜式公式: y = y_start + slope * (x - x_start)
    float y = y_low + m * (x - x_low);

    // 飽和度截斷 (Clamping)
    return (y < 0.0) ? 0.0 : ((y > 1.0) ? 1.0 : y);
}

void main() {
    // 1. 將 YUV 轉為 RGB 取得原始像素顏色
    vec3 inputColor = yuv2rgb(vTexCoord);

    // 2. 採樣 5 個控制點紋理
    // 分別取得該像素在不同亮度等級下的補償值 (Texture Lookup)
    
    // --- R Channel (紅色通道) ---
    float r0 = texture2D(uControlPoint0, vTexCoord).r;
    float r1 = texture2D(uControlPoint1, vTexCoord).r;
    float r2 = texture2D(uControlPoint2, vTexCoord).r;
    float r3 = texture2D(uControlPoint3, vTexCoord).r;
    float r4 = texture2D(uControlPoint4, vTexCoord).r;

    // --- G Channel (綠色通道) ---
    float g0 = texture2D(uControlPoint0, vTexCoord).g;
    float g1 = texture2D(uControlPoint1, vTexCoord).g;
    float g2 = texture2D(uControlPoint2, vTexCoord).g;
    float g3 = texture2D(uControlPoint3, vTexCoord).g;
    float g4 = texture2D(uControlPoint4, vTexCoord).g;

    // --- B Channel (藍色通道) ---
    float b0 = texture2D(uControlPoint0, vTexCoord).b;
    float b1 = texture2D(uControlPoint1, vTexCoord).b;
    float b2 = texture2D(uControlPoint2, vTexCoord).b;
    float b3 = texture2D(uControlPoint3, vTexCoord).b;
    float b4 = texture2D(uControlPoint4, vTexCoord).b;

    // 3. 執行插值補償
    // 根據原始輸入值 (inputColor.r/g/b) 與控制點 (r0~r4 等) 計算最終輸出
    float newR = interpolate(inputColor.r, r0, r1, r2, r3, r4);
    float newG = interpolate(inputColor.g, g0, g1, g2, g3, g4);
    float newB = interpolate(inputColor.b, b0, b1, b2, b3, b4);

    // 4. 輸出最終顏色
    gl_FragColor = vec4(newR, newG, newB, 1.0);
}

)";

// 編譯 Shader 的輔助函式
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

// 初始化圖形資源：讀取影片、讀取控制圖、編譯 Shader、建立紋理
bool prepareGraphics(const char* videoFile, const char* controlFiles[5]) {
    // 1. 初始化影片讀取器
    if (!videoReader.open(videoFile)) {
        return false;
    }
    
    // 2. 讀取 5 張 BMP 控制圖
    vector<unsigned char> controlData[5];
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(controlFiles[i], controlData[i], w, h)) {
            return false;
        }

        // 檢查尺寸一致性：控制圖必須與原圖大小相同 (Mura 補償通常是 Pixel-to-Pixel)
        if (w != imageWidth || h != imageHeight) {
            printf("錯誤: 控制點圖片尺寸 (%dx%d) 與原圖不符\n", w, h);
            return false;
        }
    }

    // 3. 編譯與連結 Shader Program
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    if (vertShader == 0 || fragShader == 0) return false;

    programID = glCreateProgram();
    glAttachShader(programID, vertShader);
    glAttachShader(programID, fragShader);
    glLinkProgram(programID);
    glUseProgram(programID);

    // 4. 獲取 Shader 變數位置 (Uniform Location)
    iLocPosition = glGetAttribLocation(programID, "aPosition");
    iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    
    // YUV 三個通道的 Texture Sampler
    iLocTextureY = glGetUniformLocation(programID, "uTextureY");
    iLocTextureU = glGetUniformLocation(programID, "uTextureU");
    iLocTextureV = glGetUniformLocation(programID, "uTextureV");
    
    // 控制點 Texture Sampler
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");

    // 5. 設定頂點屬性
    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);

    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    // 6. 建立並設置 YUV 紋理 (預先分配記憶體)
    // Texture Y: 解析度 WxH, 單通道 (GL_LUMINANCE)
    glGenTextures(1, &textureIdY);
    glBindTexture(GL_TEXTURE_2D, textureIdY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width, videoReader.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture U: 解析度 W/2 x H/2 (因為是 YUV420P)
    glGenTextures(1, &textureIdU);
    glBindTexture(GL_TEXTURE_2D, textureIdU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture V: 解析度 W/2 x H/2
    glGenTextures(1, &textureIdV);
    glBindTexture(GL_TEXTURE_2D, textureIdV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 7. 建立控制點紋理並上傳資料 (靜態圖，只傳一次)
    for (int i = 0; i < 5; i++) {
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, imageWidth, imageHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &controlData[i][0]);
        
        // 使用線性插值 (GL_LINEAR) 使控制圖平滑
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST); // 2D 影片播放不需要深度測試
    
    printf("OpenGL 初始化完成。\n");
    return true;
}

// 每一幀的渲染循環
void GraphicsUpdate(bool hasNewFrame) {
    // 只有當有新的一幀解碼出來時，才執行耗時的紋理上傳 (CPU -> GPU)
    if (hasNewFrame) {
        // 更新 Y 平面
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureIdY);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width, videoReader.height, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[0]);

        // 更新 U 平面
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, textureIdU);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[1]);

        // 更新 V 平面
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, textureIdV);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width / 2, videoReader.height / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, videoReader.frame->data[2]);
    }

    // 渲染與 Shader 計算 (這部分是純 GPU 運算)
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

    // 綁定紋理單元
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, textureIdY); glUniform1i(iLocTextureY, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textureIdU); glUniform1i(iLocTextureU, 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, textureIdV); glUniform1i(iLocTextureV, 2);

    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE3 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 3 + i);
    }
    glUniform1fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
    
    // 觸發 GPU Pipeline
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ============================================================================
// 主程式 (Entry Point) - 包含詳細時間測量邏輯
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("使用方法: %s <影片.mp4> <點1.bmp> ...\n", argv[0]);
        return 1;
    }
    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};

    // 1. 系統初始化
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
    // *** 關鍵設定：控制 VSync ***
    // 設為 1: 開啟 VSync (鎖定 60FPS)，總時間會包含等待時間
    // 設為 0: 關閉 VSync，總時間即為真實運算極限
    eglSwapInterval(CoreEGL::display, 0);

    if (!prepareGraphics(argv[1], controlFiles)) return 1;

    printf("\n--- 開始效能測量 (CPU Decode vs GPU Render) ---\n");
    
    bool end = false;
    int frame_count = 0;

    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

        // ==========================================
        // 1. 總幀時間起點 (Total Start)
        // ==========================================
        auto start_total = std::chrono::high_resolution_clock::now();

        // ==========================================
        // 2. CPU 解碼 (FFmpeg)
        // ==========================================
        auto start_cpu = std::chrono::high_resolution_clock::now();
        
        // 將解碼從 GraphicsUpdate 移出來，獨立測量
        bool hasNewFrame = videoReader.readNextFrame();
        
        auto end_cpu = std::chrono::high_resolution_clock::now();

        // ==========================================
        // 3. GPU 渲染 (OpenGL ES)
        // ==========================================
        // [前置同步] 確保 GPU 在計時開始前是閒置的 (清除上一幀的影響)
        glFinish(); 
        
        auto start_gpu = std::chrono::high_resolution_clock::now();

        // 執行紋理上傳與繪製
        GraphicsUpdate(hasNewFrame);

        // [後置同步] 強制 CPU 等待 GPU 畫完所有像素 (這對於測量 GPU 時間至關重要)
        glFinish();

        auto end_gpu = std::chrono::high_resolution_clock::now();

        // ==========================================
        // 4. 顯示與 VSync 等待
        // ==========================================
        // 如果開啟 VSync，CPU 會在這裡停下來等待螢幕刷新
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);

        auto end_total = std::chrono::high_resolution_clock::now();

        // ==========================================
        // 計算時間 (毫秒)
        // ==========================================
        double ms_cpu_decode = std::chrono::duration_cast<std::chrono::microseconds>(end_cpu - start_cpu).count() / 1000.0;
        double ms_gpu_render = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count() / 1000.0;
        double ms_total = std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total).count() / 1000.0;
        
        // 剩餘時間 (VSync 等待 + 驅動 overhead)
        double ms_wait = ms_total - (ms_cpu_decode + ms_gpu_render);
        if (ms_wait < 0) ms_wait = 0; // 避免極小誤差導致負數

        frame_count++;
        
        // 為了避免洗版，每 60 幀印出一次平均值，或單幀印出
        // 這裡示範單幀印出
        printf("Frame %d | GPU: %6.3f ms | Total: %6.3f ms | Idle: %6.3f ms\n", 
               frame_count, ms_gpu_render, ms_total, ms_wait);
    }

    // ... (資源清理保持不變) ...
    return 0;
}