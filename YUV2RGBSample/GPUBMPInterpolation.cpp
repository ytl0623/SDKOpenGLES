#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

#define SCENE_WIDTH 512
#define SCENE_HEIGHT 512

using std::string;
using std::vector;

// BMP 結構定義
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

// 全域變數
string resourceDirectory = "Supportingfiles/";
GLuint programID;
GLint iLocPosition = -1;
GLint iLocTexCoord = -1;
GLint iLocInputTexture = -1;
GLint iLocControlPoint[5] = {-1, -1, -1, -1, -1};
GLint iLocFixedX = -1;

GLuint inputTextureID;
GLuint controlPointTextureID[5];

int imageWidth = 0;
int imageHeight = 0;

// 固定的 X 座標
const float FIXED_X[5] = {90.0f/255.0f, 150.0f/255.0f, 180.0f/255.0f, 200.0f/255.0f, 250.0f/255.0f};

// 頂點座標 (全螢幕四邊形)
const GLfloat vertexVertices[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f
};

// 紋理座標
const GLfloat textureVertices[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f
};

/**
 * 載入 BMP 圖片
 */
bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("無法開啟檔案: %s\n", filename);
        return false;
    }
    
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    
    fread(&fileHeader, sizeof(BMPFileHeader), 1, file);
    fread(&infoHeader, sizeof(BMPInfoHeader), 1, file);
    
    if (fileHeader.type != 0x4D42) {
        printf("不是有效的 BMP 檔案: %s\n", filename);
        fclose(file);
        return false;
    }
     
    if (infoHeader.bits != 24) {
        printf("必須是 24-bit BMP: %s\n", filename);
        fclose(file);
        return false;
    }
    
    width = infoHeader.width;
    height = abs(infoHeader.height);
    
    int rowSize = ((width * 3 + 3) / 4) * 4;
    int imageSize = rowSize * height;
    
    vector<unsigned char> rawData(imageSize);
    fseek(file, fileHeader.offset, SEEK_SET);
    fread(&rawData[0], 1, imageSize, file);
    fclose(file);
    
    // 轉換 BGR 到 RGB 並移除 padding
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = y * rowSize + x * 3;
            int dstIdx = y * width * 3 + x * 3;
            data[dstIdx] = rawData[srcIdx + 2];     // R
            data[dstIdx + 1] = rawData[srcIdx + 1]; // G
            data[dstIdx + 2] = rawData[srcIdx];     // B
        }
    }
    
    printf("已載入: %s (%dx%d)\n", filename, width, height);
    return true;
}

/**
 * Fragment Shader (在 GPU 上執行插值)
 */
const char* fragmentShaderSource = R"(
precision highp float;

varying vec2 vTexCoord;

uniform sampler2D uInputTexture;
uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;
uniform vec2 uFixedX[5];

// 分段線性插值函數
float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    float x0 = uFixedX[0].x;
    float x1 = uFixedX[1].x;
    float x2 = uFixedX[2].x;
    float x3 = uFixedX[3].x;
    float x4 = uFixedX[4].x;
    
    float x_low, x_high, y_low, y_high;
    
    if (x < x0) {
        x_low = x0; x_high = x1;
        y_low = y0; y_high = y1;
    } else if (x > x4) {
        x_low = x3; x_high = x4;
        y_low = y3; y_high = y4;
    } else {
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
    
    float m = (y_high - y_low) / (x_high - x_low);
    float y = y_low + m * (x - x_low);
    
    return clamp(y, 0.0, 1.0);
}

void main() {
    // 讀取輸入像素
    vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;
    
    // 讀取該像素的5個控制點 Y 值
    float y0 = texture2D(uControlPoint0, vTexCoord).r;
    float y1 = texture2D(uControlPoint1, vTexCoord).r;
    float y2 = texture2D(uControlPoint2, vTexCoord).r;
    float y3 = texture2D(uControlPoint3, vTexCoord).r;
    float y4 = texture2D(uControlPoint4, vTexCoord).r;
    
    // 對每個通道進行插值
    float newR = interpolate(inputColor.r, y0, y1, y2, y3, y4);
    float newG = interpolate(inputColor.g, y0, y1, y2, y3, y4);
    float newB = interpolate(inputColor.b, y0, y1, y2, y3, y4);
    
    gl_FragColor = vec4(newR, newG, newB, 1.0);
}
)";

/**
 * Vertex Shader
 */
const char* vertexShaderSource = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

/**
 * 編譯 Shader
 */
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
            printf("Shader 編譯錯誤:\n%s\n", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

/**
 * 初始化 OpenGL
 */
bool prepareGraphics(const char* inputFile, const char* controlFiles[5]) {
    printf("setupGraphics(%d, %d)\n", SCENE_WIDTH, SCENE_HEIGHT);
    
    // 載入輸入圖片
    vector<unsigned char> inputData;
    if (!loadBMP(inputFile, inputData, imageWidth, imageHeight)) {
        return false;
    }
    
    // 載入5張控制點圖片
    vector<unsigned char> controlData[5];
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(controlFiles[i], controlData[i], w, h)) {
            return false;
        }
        if (w != imageWidth || h != imageHeight) {
            printf("控制點圖片尺寸不符: %s\n", controlFiles[i]);
            return false;
        }
    }
    
    // 編譯 Shader
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    if (vertShader == 0 || fragShader == 0) {
        return false;
    }
    
    // 建立 Program
    programID = glCreateProgram();
    glAttachShader(programID, vertShader);
    glAttachShader(programID, fragShader);
    glLinkProgram(programID);
    glUseProgram(programID);
    
    // 取得 Attribute 和 Uniform 位置
    iLocPosition = glGetAttribLocation(programID, "aPosition");
    iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
    iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
    iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
    iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
    iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
    iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    
    // 設定 Vertex Attributes
    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
    // 建立輸入紋理
    glGenTextures(1, &inputTextureID);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &inputData[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // 建立5個控制點紋理
    for (int i = 0; i < 5; i++) {
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    // 設定清除顏色和深度測試
    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    
    printf("OpenGL 初始化完成\n");
    return true;
}

/**
 * 渲染更新函數
 */
void GraphicsUpdate() {
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    
    // 綁定輸入紋理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTextureID);
    glUniform1i(iLocInputTexture, 0);
    
    // 綁定5個控制點紋理
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glUniform1i(iLocControlPoint[i], 1 + i);
    }
    
    // 傳遞固定 X 座標
    glUniform2fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
    
    // 繪製全螢幕四邊形
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/**
 * 主程式
 */
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("使用方法: %s <輸入BMP> <點1> <點2> <點3> <點4> <點5>\n", argv[0]);
        return 1;
    }
    
    const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};
    
    // 初始化 EGL 和視窗
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
    // 初始化 OpenGL 和載入圖片
    if (!prepareGraphics(argv[1], controlFiles)) {
        printf("初始化失敗\n");
        return 1;
    }
    
    printf("開始顯示處理結果...\n按任意鍵關閉視窗\n");
    
    // 主渲染迴圈
    bool end = false;
    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
            end = true;
        }
        GraphicsUpdate();
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
    }
    
    printf("程式結束\n");
    
    // 清理資源
    glDeleteTextures(1, &inputTextureID);
    glDeleteTextures(5, controlPointTextureID);
    glDeleteProgram(programID);
    
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;
    
    return 0;
}