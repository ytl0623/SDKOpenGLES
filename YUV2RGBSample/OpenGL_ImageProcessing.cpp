// make clean
// make
// ./YUV2RGBSample  Supportingfiles/blue.bmp  gray_90.bmp  gray_150.bmp gray_180.bmp gray_200.bmp gray_250.bmp
// last edited: 20251119
// 20251119 增加註解
// 20251117 計算各階段時間
// 20251111 針對RGB三通道插值計算

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
#include <cmath> // 為了 abs()

// 自定義標頭檔 (假設這些是封裝好的視窗與 EGL 工具)
#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

// ============================================================================
// 常數定義
// ============================================================================
// 場景視窗的寬度和高度 (解析度)
#define SCENE_WIDTH 512
#define SCENE_HEIGHT 512

using std::string;
using std::vector;

// ============================================================================
// 工具類別：計時器 (Timer)
// ============================================================================
/**
 * 用於測量程式碼區塊執行時間的輔助類別 (RAII 風格)
 * 建構時開始計時，解構時自動印出經過時間
 */
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
        printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0);
    }
    
    // 手動獲取經過時間 (毫秒)
    double getElapsedMs() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000.0;
    }
};

// ============================================================================
// BMP 檔案格式結構定義
// ============================================================================
#pragma pack(push, 1)  // 強制結構體以 1 byte 對齊，避免編譯器自動填充 (Padding) 造成讀取錯誤

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
#pragma pack(pop)  // 恢復預設的記憶體對齊方式

// ============================================================================
// 全域變數
// ============================================================================
string resourceDirectory = "Supportingfiles/";  // 資源檔案目錄路徑

// OpenGL Shader 程式相關 ID
GLuint programID;             // Shader 程式 ID (Program Object)
GLint iLocPosition = -1;      // Attribute: 頂點位置 (aPosition)
GLint iLocTexCoord = -1;      // Attribute: 紋理座標 (aTexCoord)

// Uniform Location: 傳遞給 Shader 的全域參數位置
GLint iLocInputTexture = -1;  // 原始輸入圖片紋理
GLint iLocControlPoint[5] = {-1, -1, -1, -1, -1};  // 5個控制點紋理 (用於曲線調整)
GLint iLocFixedX = -1;        // 曲線的 X 軸固定座標點

// OpenGL 紋理 ID (Handle)
GLuint inputTextureID;           // 輸入圖片的紋理 ID
GLuint controlPointTextureID[5]; // 5個控制點圖片的紋理 ID

// 圖片尺寸 (假設所有圖片尺寸相同)
int imageWidth = 0;
int imageHeight = 0;

// ============================================================================
// 曲線調整參數
// ============================================================================
// 這些是色彩曲線上的固定 X 座標節點 (Input Level)
// 數值已經正規化到 [0, 1] 範圍 (原值/255)
// 用於 Fragment Shader 中的分段線性插值
const float FIXED_X[5] = {
    90.0f/255.0f,   // 節點 1: 暗部 (Shadows)
    150.0f/255.0f,  // 節點 2: 中間調偏暗
    180.0f/255.0f,  // 節點 3: 中間調 (Midtones)
    200.0f/255.0f,  // 節點 4: 中間調偏亮
    250.0f/255.0f   // 節點 5: 亮部 (Highlights)
};

// ============================================================================
// 頂點資料 (Quad Data)
// ============================================================================
// 全螢幕四邊形的頂點座標 (Normalized Device Coordinates, NDC)
// 範圍從 -1.0 (左/下) 到 1.0 (右/上)
const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,  // 左下
     1.0f, -1.0f,  // 右下
    -1.0f,  1.0f,  // 左上
     1.0f,  1.0f   // 右上
};

// 對應的紋理座標 (UV Coordinates)
// 範圍從 0.0 (左/下) 到 1.0 (右/上)，對應整個紋理圖像
const GLfloat textureVertices[] = {
    0.0f, 0.0f,  // 左下
    1.0f, 0.0f,  // 右下
    0.0f, 1.0f,  // 左上
    1.0f, 1.0f   // 右上
};

// ============================================================================
// 函數：載入 BMP 圖片
// ============================================================================
/**
 * 讀取 24-bit BMP 檔案並轉換為 RGB 陣列
 * * @param filename 檔案路徑
 * @param data 輸出: 儲存像素資料的 vector (RGB順序)
 * @param width 輸出: 圖片寬度
 * @param height 輸出: 圖片高度
 * @return 成功回傳 true，失敗回傳 false
 */
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
/**
 * 處理每個頂點的程式。
 * 這裡主要負責傳遞位置和紋理座標。
 */
const char* vertexShaderSource = R"(
attribute vec2 aPosition;  // 輸入: 頂點位置 (x, y)
attribute vec2 aTexCoord;  // 輸入: 紋理座標 (u, v)
varying vec2 vTexCoord;    // 輸出: 插值後的紋理座標 (傳給 Fragment Shader)

void main() {
    // 設定頂點位置 (z=0.0, w=1.0)
    gl_Position = vec4(aPosition, 0.0, 1.0);
    
    // 直接傳遞紋理座標
    vTexCoord = aTexCoord;
}
)";

// ============================================================================
// Fragment Shader (片段著色器)
// ============================================================================
/**
 * 處理每個像素顏色的程式。
 * 核心邏輯：讀取原始顏色，根據 5 個控制點紋理提供的 Y 值，進行曲線映射。
 */
const char* fragmentShaderSource = R"(
precision lowp float;      // 設定浮點數精度為低精度 (提升效能)
varying vec2 vTexCoord;    // 從 Vertex Shader 接收的紋理座標

// Uniforms: 外部傳入的紋理單元
uniform sampler2D uInputTexture;    // 原始影像
uniform sampler2D uControlPoint0;   // 控制點 0 的 Y 值圖 (對應 FIXED_X[0])
uniform sampler2D uControlPoint1;   // 控制點 1 的 Y 值圖
uniform sampler2D uControlPoint2;   // 控制點 2 的 Y 值圖
uniform sampler2D uControlPoint3;   // 控制點 3 的 Y 值圖
uniform sampler2D uControlPoint4;   // 控制點 4 的 Y 值圖
uniform vec2 uFixedX[5];            // 5 個控制點的 X 座標 (固定值)

// ----------------------------------------------------------------------------
// 函數: 分段線性插值 (Piecewise Linear Interpolation)
// ----------------------------------------------------------------------------
/**
 * 給定輸入值 x 和 5 個控制點的輸出值 y0~y4，計算對應的輸出 y。
 * 邏輯：找出 x 落在 X 軸的哪個區間，然後在該區間內做線性插值。
 */
float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    // 取出 X 軸節點值
    float x0 = uFixedX[0].x;
    float x1 = uFixedX[1].x;
    float x2 = uFixedX[2].x;
    float x3 = uFixedX[3].x;
    float x4 = uFixedX[4].x;
    
    float x_low, x_high, y_low, y_high;
    
    // 判斷 x 所在的區間
    if (x < x0) {
        // 區間 0 左側 (暗部): 使用 (x0, y0) 和 (x1, y1) 向外插值 (Extrapolation)
        x_low = x0; x_high = x1;
        y_low = y0; y_high = y1;
    } else if (x > x4) {
        // 區間 4 右側 (亮部): 使用 (x3, y3) 和 (x4, y4) 向外插值
        x_low = x3; x_high = x4;
        y_low = y3; y_high = y4;
    } else {
        // 正常範圍內
        if (x >= x0 && x <= x1) {
            x_low = x0; x_high = x1;
            y_low = y0; y_high = y1;
        } else if (x >= x1 && x <= x2) {
            x_low = x1; x_high = x2;
            y_low = y1; y_high = y2;
        } else if (x >= x2 && x <= x3) {
            x_low = x2; x_high = x3;
            y_low = y2; y_high = y3;
        } else {
            x_low = x3; x_high = x4;
            y_low = y3; y_high = y4;
        }
    }
    
    // 計算斜率 m
    float m = (y_high - y_low) / (x_high - x_low);
    
    // 線性方程: y = y_low + slope * (x - x_low)
    float y = y_low + m * (x - x_low);
    
    // 確保輸出值在 [0, 1] 範圍內
    return clamp(y, 0.0, 1.0);
}

void main() {
    // 1. 採樣原始圖片顏色
    vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;
    
    // 2. 採樣 5 個控制點紋理在當前位置的值 (這些紋理儲存了局部的曲線調整參數)
    // 假設控制圖是灰階的，取 R 通道即可代表亮度/數值
    float y0_r = texture2D(uControlPoint0, vTexCoord).r;
    float y1_r = texture2D(uControlPoint1, vTexCoord).r;
    float y2_r = texture2D(uControlPoint2, vTexCoord).r;
    float y3_r = texture2D(uControlPoint3, vTexCoord).r;
    float y4_r = texture2D(uControlPoint4, vTexCoord).r;

    float y0_g = texture2D(uControlPoint0, vTexCoord).g;
    float y1_g = texture2D(uControlPoint1, vTexCoord).g;
    float y2_g = texture2D(uControlPoint2, vTexCoord).g;
    float y3_g = texture2D(uControlPoint3, vTexCoord).g;
    float y4_g = texture2D(uControlPoint4, vTexCoord).g;

    float y0_b = texture2D(uControlPoint0, vTexCoord).b;
    float y1_b = texture2D(uControlPoint1, vTexCoord).b;
    float y2_b = texture2D(uControlPoint2, vTexCoord).b;
    float y3_b = texture2D(uControlPoint3, vTexCoord).b;
    float y4_b = texture2D(uControlPoint4, vTexCoord).b;
    
    // 3. 對 RGB 三個通道分別進行曲線映射
    float newR = interpolate(inputColor.r, y0_r, y1_r, y2_r, y3_r, y4_r);
    float newG = interpolate(inputColor.g, y0_g, y1_g, y2_g, y3_g, y4_g);
    float newB = interpolate(inputColor.b, y0_b, y1_b, y2_b, y3_b, y4_b);
    
    // 4. 輸出最終顏色
    gl_FragColor = vec4(newR, newG, newB, 1.0);
}
)";

// ============================================================================
// 函數：編譯 Shader
// ============================================================================
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

// ============================================================================
// 函數：初始化圖形系統
// ============================================================================
bool prepareGraphics(const char* inputFile, const char* controlFiles[5]) {
    printf("正在初始化圖形資源 (解析度: %dx%d)...\n", SCENE_WIDTH, SCENE_HEIGHT);
    
    // 1. 載入原始圖片
    vector<unsigned char> inputData;
    if (!loadBMP(inputFile, inputData, imageWidth, imageHeight)) {
        return false;
    }
    
    // 2. 載入 5 張控制點圖片
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
    
    // 3. 編譯 Shader
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    if (vertShader == 0 || fragShader == 0) return false;
    
    // 4. 連結 Shader Program
    programID = glCreateProgram();
    glAttachShader(programID, vertShader);
    glAttachShader(programID, fragShader);
    glLinkProgram(programID);
    glUseProgram(programID);
    
    // 5. 獲取變數位置 (Locations)
    iLocPosition = glGetAttribLocation(programID, "aPosition");
    iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    
    iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    
    // 6. 設定 VBO (Vertex Attributes)
    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
    // 7. 建立並上傳輸入紋理
    glGenTextures(1, &inputTextureID);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &inputData[0]);
    
    // 設定紋理過濾與 Wrap 模式
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // 8. 建立並上傳 5 個控制點紋理
    for (int i = 0; i < 5; i++) {
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    // 9. 基本 OpenGL 設定
    glClearColor(0.0f, 0.0f, 0.2f, 1.0f); // 背景設為深藍色
    glDisable(GL_DEPTH_TEST); // 2D 圖像處理不需要深度測試
    
    printf("OpenGL 初始化完成。\n");
    return true;
}

// ============================================================================
// 函數：圖形渲染迴圈
// ============================================================================
void GraphicsUpdate() {
    // 清除畫面
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 設定視埠 (Viewport) 大小
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    
    // 1. 綁定紋理到對應的 Texture Unit
    // Unit 0: 原始影像
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glUniform1i(iLocInputTexture, 0);
    
    // Unit 1~5: 控制點影像
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 1 + i);
    }
    
    // 2. 更新 Uniform 變數
    glUniform2fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
    
    // 3. 繪製四邊形 (觸發 Fragment Shader)
    // GL_TRIANGLE_STRIP: 使用 4 個頂點繪製 2 個三角形組成的矩形
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ============================================================================
// 主程式 (Entry Point)
// ============================================================================
int main(int argc, char* argv[]) {
    auto program_start = std::chrono::high_resolution_clock::now();
    
    // 檢查參數數量
    if (argc != 7) {
        printf("使用方法: %s <輸入BMP> <點1> <點2> <點3> <點4> <點5>\n", argv[0]);
        return 1;
    }
    
    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};
    
    // 1. 初始化視窗與 EGL 環境
    Timer timer("1. 系統初始化");
    
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT); // 建立視窗
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);       // 初始化 EGL context
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
    // 2. 初始化 OpenGL 資源 (Shader, Textures)
    {
        Timer timer("2. 圖形資源載入");
        if (!prepareGraphics(argv[1], controlFiles)) {
            printf("圖形初始化失敗！\n");
            return 1;
        }
    }
    
    printf("\n--- 系統就緒，開始渲染迴圈 ---\n");
    printf("按任意鍵或關閉視窗以退出...\n\n");
    
    // 3. 主渲染迴圈
    bool end = false;
    int frame_count = 0;
    double total_render_time = 0.0;
    
    auto render_loop_start = std::chrono::high_resolution_clock::now();
    
    while (!end) {
        // 檢查視窗事件 (如關閉按鈕)
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
            end = true;
        }
        
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        // 執行渲染
        GraphicsUpdate();
        
        // 交換前後緩衝區 (顯示畫面)
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
        
        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_ms = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count() / 1000.0;
        
        total_render_time += frame_ms;
        frame_count++;
        printf("第 %d 幀 FPS: %.1f (Frame Time: %.3f ms)\n", frame_count, 1000.0 / frame_ms, frame_ms);
    }
    
    // 4. 清理資源
    {
        Timer timer("4. 資源釋放");
        glDeleteTextures(1, &inputTextureID);
        glDeleteTextures(5, controlPointTextureID);
        glDeleteProgram(programID);
        CoreEGL::terminateEGL();
        podium->destroyWindow();
        delete podium;
    }
    
    auto program_end = std::chrono::high_resolution_clock::now();
    printf("總執行時間: %.3f 秒\n", std::chrono::duration_cast<std::chrono::milliseconds>(program_end - program_start).count() / 1000.0);
    
    return 0;
}
