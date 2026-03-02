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

// --- 新增：用於 Linux 終端機鍵盤偵測的標頭檔 ---
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

// 自定義標頭檔
#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

// ============================================================================
// 常數定義
// ============================================================================
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
// 工具類別：計時器 (Timer)
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
        printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0);
    }
};

// ============================================================================
// BMP 檔案格式結構定義
// ============================================================================
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
// 全域變數
// ============================================================================
string resourceDirectory = "Supportingfiles/";

GLuint programID;
GLint iLocPosition = -1;
GLint iLocTexCoord = -1;

GLint iLocInputTexture = -1;
GLint iLocControlPoint[5] = {-1, -1, -1, -1, -1};
GLint iLocFixedX = -1;

// --- 新增：Shader 開關變數位置 ---
GLint iLocEnableDemura = -1; 

GLuint inputTextureID;
GLuint controlPointTextureID[5];

int imageWidth = 0;
int imageHeight = 0;

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
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f
};

// ============================================================================
// 函數：載入 BMP 圖片
// ============================================================================
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

uniform sampler2D uInputTexture;

uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;

uniform float uFixedX[5];

// --- 新增：接收來自 C++ 的開關 ---
uniform int uEnableDemura;

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
    // 取得原圖顏色
    vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;

    // --- 新增：判斷是否開啟 DEMURA ---
    if (uEnableDemura == 0) {
        // 如果沒有開啟，直接輸出原圖顏色並結束
        gl_FragColor = vec4(inputColor, 1.0);
        return;
    }

    // 以下為 DEMURA 處理邏輯
    float r0 = texture2D(uControlPoint0, vTexCoord).r;
    float r1 = texture2D(uControlPoint1, vTexCoord).r;
    float r2 = texture2D(uControlPoint2, vTexCoord).r;
    float r3 = texture2D(uControlPoint3, vTexCoord).r;
    float r4 = texture2D(uControlPoint4, vTexCoord).r;

    float g0 = texture2D(uControlPoint0, vTexCoord).g;
    float g1 = texture2D(uControlPoint1, vTexCoord).g;
    float g2 = texture2D(uControlPoint2, vTexCoord).g;
    float g3 = texture2D(uControlPoint3, vTexCoord).g;
    float g4 = texture2D(uControlPoint4, vTexCoord).g;

    float b0 = texture2D(uControlPoint0, vTexCoord).b;
    float b1 = texture2D(uControlPoint1, vTexCoord).b;
    float b2 = texture2D(uControlPoint2, vTexCoord).b;
    float b3 = texture2D(uControlPoint3, vTexCoord).b;
    float b4 = texture2D(uControlPoint4, vTexCoord).b;

    float newR = interpolate(inputColor.r, r0, r1, r2, r3, r4);
    float newG = interpolate(inputColor.g, g0, g1, g2, g3, g4);
    float newB = interpolate(inputColor.b, b0, b1, b2, b3, b4);

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
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ============================================================================
// 函數：初始化圖形系統
// ============================================================================
bool prepareGraphics(const char* inputFile, const char* controlFiles[5]) {
    printf("正在初始化圖形資源...\n");
    
    vector<unsigned char> inputData;
    if (!loadBMP(inputFile, inputData, imageWidth, imageHeight)) return false;
    
    vector<unsigned char> controlData[5];
    for (int i = 0; i < 5; i++) {
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
    
    iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    
    // --- 新增：取得開關變數位置 ---
    iLocEnableDemura = glGetUniformLocation(programID, "uEnableDemura");

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
    glGenTextures(1, &inputTextureID);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &inputData[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    for (int i = 0; i < 5; i++) {
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    return true;
}

// ============================================================================
// 函數：圖形渲染迴圈
// --- 修改：接收 enableDemura 參數 ---
// ============================================================================
void GraphicsUpdate(bool enableDemura) {
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glUniform1i(iLocInputTexture, 0);
    
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 1 + i);
    }
    
    glUniform1fv(iLocFixedX, 5, (GLfloat*)FIXED_X);

    // --- 新增：將開關狀態傳送給 Shader ---
    glUniform1i(iLocEnableDemura, enableDemura ? 1 : 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); 
}

// ============================================================================
// 主程式 (Entry Point)
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("使用方法: %s <輸入BMP> <點1> <點2> <點3> <點4> <點5>\n", argv[0]);
        return 1;
    }
    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};

    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

    eglSwapInterval(CoreEGL::display, 0); // 關閉 VSync

    if (!prepareGraphics(argv[1], controlFiles)) return 1;

    printf("\n=======================================================\n");
    printf("  使用提示：請在「終端機視窗」中按下【空白鍵 (Space)】\n");
    printf("  即可切換 (Toggle) DEMURA 校正效果與顯示原圖。\n");
    printf("=======================================================\n\n");
    
    bool end = false;
    int frame_count = 0;

    // --- 新增：控制 DEMURA 是否開啟的布林變數 (預設開啟) ---
    bool isDemuraEnabled = true;

    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

        // --- 新增：偵測鍵盤輸入 (非阻塞) ---
        if (kbhit()) {
            char ch = getchar();
            if (ch == ' ') { // 如果按下空白鍵
                isDemuraEnabled = !isDemuraEnabled; // 切換狀態
                printf("\n>>> 目前狀態： %s <<<\n\n", isDemuraEnabled ? "DEMURA 校正中" : "顯示原圖");
            }
        }

        auto start_total = std::chrono::high_resolution_clock::now();
        glFinish(); 
        
        auto start_gpu = std::chrono::high_resolution_clock::now();
        
        // --- 修改：傳入開關狀態 ---
        GraphicsUpdate(isDemuraEnabled); 

        glFinish(); 
        
        auto end_gpu = std::chrono::high_resolution_clock::now();
        
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
        
        auto end_total = std::chrono::high_resolution_clock::now();

        double gpu_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count() / 1000.0;
        double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total).count() / 1000.0;
        double wait_ms = total_ms - gpu_ms;

        frame_count++;
        
        // 每 60 幀印出一次，避免文字洗版干擾鍵盤偵測
        if (frame_count % 60 == 0) {
            printf("Frame %d | GPU: %6.3f ms | Total: %6.3f ms\r", frame_count, gpu_ms, total_ms);
            fflush(stdout); // 強制刷新輸出緩衝
        }
    }

    // 資源釋放
    glDeleteTextures(1, &inputTextureID);
    glDeleteTextures(5, controlPointTextureID);
    
    glDeleteProgram(programID);
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}