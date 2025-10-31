#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <fstream>

#include "XLinuxPodium.h"
#include "BMP2Grayscale.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"
#include "XMatrixAPI.h"
#include "string"

#define SCENE_WIDTH 512
#define SCENE_HEIGHT 512

using std::string;

string resourceDirectory = "Supportingfiles/";
string vertexShaderFilename = "BMP.vert";
string fragmentShaderFilename = "BMP.frag";
string bmpImageFilename = "blue.bmp";

GLuint programID;
GLint iLocPosition = -1;
GLint iLocTextureCoord = -1;
GLint iLocMVP = -1;
GLint iLocSampler = -1;

GLuint bmpTextureID;

// BMP 載入函數
bool loadBMP(const char* imagepath, std::vector<unsigned char>& out_data, int& out_width, int& out_height) {
    printf("Reading image %s\n", imagepath);

    unsigned char header[54];
    FILE * file = fopen(imagepath, "rb");
    if (!file) {
        printf("Image could not be opened\n");
        return false;
    }
    if (fread(header, 1, 54, file) != 54) {
        printf("Not a correct BMP file\n");
        fclose(file);
        return false;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        printf("Not a correct BMP file\n");
        fclose(file);
        return false;
    }
    if (*(int*)&(header[0x1E]) != 0) {
        printf("BMP file must be uncompressed\n");
        fclose(file);
        return false;
    }
    if (*(int*)&(header[0x1C]) != 24) {
        printf("BMP file must be 24-bit\n");
        fclose(file);
        return false;
    }

    out_width = *(int*)&(header[0x12]);
    out_height = *(int*)&(header[0x16]);
    int imageSize = *(int*)&(header[0x22]);
    if (imageSize == 0) imageSize = out_width * out_height * 3;

    out_data.resize(imageSize);
    fseek(file, *(int*)&(header[0x0A]), SEEK_SET);
    fread(&out_data[0], 1, imageSize, file);
    fclose(file);
    
    printf("Loaded BMP: %d x %d\n", out_width, out_height);
    return true;
}

// prepareGraphics 函數
bool prepareGraphics(int width, int height) {
    printf("setupGraphics(%d, %d)\n", width, height);
    string vertexShaderPath = resourceDirectory + vertexShaderFilename;
    string fragmentShaderPath = resourceDirectory + fragmentShaderFilename;
    string bmpPath = resourceDirectory + bmpImageFilename;

    std::vector<unsigned char> bmpData;
    int bmpWidth, bmpHeight;
    if (!loadBMP(bmpPath.c_str(), bmpData, bmpWidth, bmpHeight)) {
        return false;
    }
    
    GLuint vertexShaderID = 0;
    GLuint fragmentShaderID = 0;
    Shader::processShader(&vertexShaderID, vertexShaderPath.c_str(), GL_VERTEX_SHADER);
    printf("vertexShaderID = %d\n", vertexShaderID);
    Shader::processShader(&fragmentShaderID, fragmentShaderPath.c_str(), GL_FRAGMENT_SHADER);
    printf("fragmentShaderID = %d\n", fragmentShaderID);

    programID = glCreateProgram();
    if (programID == 0) {
        printf("Could not create program.\n");
        return false;
    }
    glAttachShader(programID, vertexShaderID);
    glAttachShader(programID, fragmentShaderID);
    glLinkProgram(programID);
    glUseProgram(programID);

    iLocPosition = glGetAttribLocation(programID, "av4position");
    iLocTextureCoord = glGetAttribLocation(programID, "av2texCoord");
    iLocMVP = glGetUniformLocation(programID, "mvp");
    iLocSampler = glGetUniformLocation(programID, "textureSampler");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glEnableVertexAttribArray(iLocTextureCoord);
    glVertexAttribPointer(iLocTextureCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
    glGenTextures(1, &bmpTextureID);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bmpTextureID);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, bmpWidth, bmpHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &bmpData[0]);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

void GraphicsUpdate()
{
    // 定義一個 4x4 單位矩陣。
    // OpenGL 需要 column-major (行主序) 陣列，這正是下面的排列方式。
    const GLfloat identityMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,  // 第一行
        0.0f, 1.0f, 0.0f, 0.0f,  // 第二行
        0.0f, 0.0f, 1.0f, 0.0f,  // 第三行
        0.0f, 0.0f, 0.0f, 1.0f   // 第四行
    };

    // 將單位矩陣傳遞給著色器
    glUniformMatrix4fv(iLocMVP, 1, GL_FALSE, identityMatrix);

    // --- 渲染邏輯 (與之前相同) ---
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 綁定紋理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bmpTextureID);

    // 告知採樣器使用 0 號紋理單元
    glUniform1i(iLocSampler, 0);

    // 繪製填滿螢幕的四邊形
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char* argv[]) {
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

    if (!prepareGraphics(SCENE_WIDTH, SCENE_HEIGHT)) {
        printf("Error preparing graphics.\n");
        return -1;
    }

    // --- MODIFIED: 主迴圈 ---
    // 移除了時間變數和增量，因為不再需要動畫
    bool end = false;
    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
            end = true;
        }
        GraphicsUpdate(); // 不再傳遞 time 參數
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
    }

    // --- 清理 ---
    glDeleteTextures(1, &bmpTextureID);
    glDeleteProgram(programID);

    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;
    return 0;
}