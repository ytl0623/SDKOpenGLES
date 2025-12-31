#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>

// 自定義標頭檔 (維持您的環境設定)
#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

#define SCENE_WIDTH 1920
#define SCENE_HEIGHT 1080

using std::vector;

// ============================================================================
// 工具類別：計時器
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
// Shader 定義 (簡化版：僅顯示紋理)
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

const char* fragmentShaderSource = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// ============================================================================
// 全域變數
// ============================================================================
GLuint programID;
GLint iLocPosition = -1;
GLint iLocTexCoord = -1;
GLint iLocTexture = -1;
GLuint textureID;

const GLfloat vertexVertices[] = { -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f };
const GLfloat textureVertices[] = { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f };

// ============================================================================
// 核心演算法移植：Tiling Pattern 生成
// ============================================================================
void generateTilingPattern(vector<unsigned char>& outputData) {
    printf("正在執行 Tiling Pattern 生成演算法...\n");

    int width = SCENE_WIDTH;
    int height = SCENE_HEIGHT;
    
    // 1. 基礎參數 (對應 Python)
    float bg_gray = 94.0f;
    float gamma = 2.2f;
    float bg_linear = pow(bg_gray / 255.0f, gamma);
    
    // 使用 float buffer 進行線性空間運算 (對應 numpy float32 陣列)
    // 這裡使用一維陣列模擬二維，index = y * width + x
    vector<float> img_linear(width * height, bg_linear);

    // 2. 定義 7 個區域的中心點
    int num_zones = 7;
    vector<int> centers(num_zones);
    for (int i = 0; i < num_zones; i++) {
        centers[i] = 140 + 270 * i;
    }

    // 3. 定義能量基底
    float total_energy_block = 9.0f * bg_linear;

    // 4. 演算法邏輯迴圈
    // 對每個 Zone 執行對應的 Mode
    for (int i = 0; i < num_zones; i++) {
        int center_x = centers[i];
        int mode = i; // Mode 0 ~ 6

        // 定義左右縫隙座標
        int seam_l = center_x - 1;
        int seam_r = center_x;

        // Mode 0: Raw (不處理，直接挖空)
        // Python: img_linear[:, seam_indices] = 0.0 -> 對整條垂直線設為 0
        if (mode == 0) {
            for (int y = 0; y < height; y++) {
                if (seam_l >= 0 && seam_l < width) img_linear[y * width + seam_l] = 0.0f;
                if (seam_r >= 0 && seam_r < width) img_linear[y * width + seam_r] = 0.0f;
            }
            continue; // 跳過後續填充邏輯
        }

        // 定義操作座標
        int l_inner = seam_l - 1;
        int l_outer = seam_l - 2;
        int r_inner = seam_r + 1;
        int r_outer = seam_r + 2;

        // 垂直掃描 (Stride = 3)，從 y=1 到 height-2 (配合 Python range(1, height-1, 3))
        for (int y = 1; y < height - 1; y += 3) {
            
            // 先將縫隙處設為 0 (模擬物理遮擋)
            // 注意：Python 的 range(1, height-1, 3) 其實只處理了 stride 點
            // 但 Python 代碼有一行 `img_linear[:, seam_indices] = 0.0` 是在 loop 外全圖執行的
            // 為了效能，我們在這裡只處理 stride 附近的點，或者需要另外一個 loop 清空縫隙。
            // 這裡為了保持與 Python "模擬遮擋" 邏輯一致，我們在上方 Loop 外已經可以處理縫隙清空。
            // 這裡補強 stride 點的邏輯。
            int y_indices[3] = {y - 1, y, y + 1};

            // 清空目標區域 (Left)
            for (int r : y_indices) {
                if (r >= 0 && r < height) {
                    if (l_outer >= 0) img_linear[r * width + l_outer] = 0.0f;
                    if (l_inner >= 0) img_linear[r * width + l_inner] = 0.0f;
                    // 清空縫隙本身 (前面 Mode 0 處理過，但其他 Mode 也要清)
                    img_linear[r * width + seam_l] = 0.0f;
                }
            }

            // ================= Left Side Logic =================
            // 邊界檢查 Helper Lambda
            auto safe_add = [&](int y_pos, int x_pos, float val) {
                if (y_pos >= 0 && y_pos < height && x_pos >= 0 && x_pos < width) {
                    img_linear[y_pos * width + x_pos] += val;
                }
            };

            if (mode == 1) { // Point
                safe_add(y, l_inner, total_energy_block);
            }
            else if (mode == 2) { // Line
                safe_add(y, l_inner, total_energy_block * (2.0f/3.0f));
                safe_add(y, l_outer, total_energy_block * (1.0f/3.0f));
            }
            else if (mode == 3) { // Triangle
                safe_add(y, l_inner, total_energy_block * (2.0f/3.0f));
                safe_add(y-1, l_outer, total_energy_block * (1.0f/6.0f));
                safe_add(y+1, l_outer, total_energy_block * (1.0f/6.0f));
            }
            else if (mode == 4) { // Vertical
                safe_add(y, l_inner, total_energy_block * (2.0f/12.0f));
                safe_add(y-1, l_inner, total_energy_block * (5.0f/12.0f));
                safe_add(y+1, l_inner, total_energy_block * (5.0f/12.0f));
            }
            else if (mode == 5) { // Cross
                safe_add(y, l_inner, total_energy_block * 0.3f);
                safe_add(y-1, l_inner, total_energy_block * 0.2f);
                safe_add(y+1, l_inner, total_energy_block * 0.2f);
                safe_add(y, l_outer, total_energy_block * 0.2f);
                safe_add(y-1, l_outer, total_energy_block * 0.05f);
                safe_add(y+1, l_outer, total_energy_block * 0.05f);
            }
            else if (mode == 6) { // Anisotropic Gaussian
                // Inner
                safe_add(y, l_inner, total_energy_block * 0.40f);
                safe_add(y-1, l_inner, total_energy_block * 0.20f);
                safe_add(y+1, l_inner, total_energy_block * 0.20f);
                // Outer
                safe_add(y, l_outer, total_energy_block * 0.10f);
                safe_add(y-1, l_outer, total_energy_block * 0.05f);
                safe_add(y+1, l_outer, total_energy_block * 0.05f);
            }

            // ================= Right Side Logic (Symmetric) =================
            // 清空目標區域 (Right)
            for (int r : y_indices) {
                if (r >= 0 && r < height) {
                    if (r_outer < width) img_linear[r * width + r_outer] = 0.0f;
                    if (r_inner < width) img_linear[r * width + r_inner] = 0.0f;
                    // 清空縫隙本身
                    img_linear[r * width + seam_r] = 0.0f;
                }
            }

            if (mode == 1) { // Point
                safe_add(y, r_inner, total_energy_block);
            }
            else if (mode == 2) { // Line
                safe_add(y, r_inner, total_energy_block * (2.0f/3.0f));
                safe_add(y, r_outer, total_energy_block * (1.0f/3.0f));
            }
            else if (mode == 3) { // Triangle
                safe_add(y, r_inner, total_energy_block * (2.0f/3.0f));
                safe_add(y-1, r_outer, total_energy_block * (1.0f/6.0f));
                safe_add(y+1, r_outer, total_energy_block * (1.0f/6.0f));
            }
            else if (mode == 4) { // Vertical
                safe_add(y, r_inner, total_energy_block * (2.0f/12.0f));
                safe_add(y-1, r_inner, total_energy_block * (5.0f/12.0f));
                safe_add(y+1, r_inner, total_energy_block * (5.0f/12.0f));
            }
            else if (mode == 5) { // Cross
                safe_add(y, r_inner, total_energy_block * 0.3f);
                safe_add(y-1, r_inner, total_energy_block * 0.2f);
                safe_add(y+1, r_inner, total_energy_block * 0.2f);
                safe_add(y, r_outer, total_energy_block * 0.2f);
                safe_add(y-1, r_outer, total_energy_block * 0.05f);
                safe_add(y+1, r_outer, total_energy_block * 0.05f);
            }
            else if (mode == 6) { // Anisotropic Gaussian
                // Inner
                safe_add(y, r_inner, total_energy_block * 0.40f);
                safe_add(y-1, r_inner, total_energy_block * 0.20f);
                safe_add(y+1, r_inner, total_energy_block * 0.20f);
                // Outer
                safe_add(y, r_outer, total_energy_block * 0.10f);
                safe_add(y-1, r_outer, total_energy_block * 0.05f);
                safe_add(y+1, r_outer, total_energy_block * 0.05f);
            }
        }
    }

    // 5. Gamma 校正與轉檔 (Linear Float -> Gamma Corrected UInt8 RGB)
    outputData.resize(width * height * 3);
    float inv_gamma = 1.0f / gamma;

    for (int i = 0; i < width * height; i++) {
        float val = img_linear[i];
        
        // Gamma Correction: 255 * (val ^ (1/2.2))
        // 確保數值不小於 0 (雖然理論上不會，但安全起見)
        if (val < 0.0f) val = 0.0f;
        
        float corrected = 255.0f * pow(val, inv_gamma);
        
        // Clamp 0~255
        unsigned char pixel_val = (unsigned char)(std::min(std::max(corrected, 0.0f), 255.0f));
        
        // 填入 RGB (灰階所以三色相同)
        outputData[i * 3 + 0] = pixel_val; // R
        outputData[i * 3 + 1] = pixel_val; // G
        outputData[i * 3 + 2] = pixel_val; // B
    }
}

// ============================================================================
// OpenGL 初始化與資源管理
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

bool prepareGraphics() {
    printf("初始化圖形資源...\n");

    // 1. 生成 Tiling Pattern 影像資料 (CPU 計算)
    vector<unsigned char> imageBuffer;
    {
        Timer t("影像生成演算法");
        generateTilingPattern(imageBuffer);
    }

    // 2. 編譯 Shader
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    if (vertShader == 0 || fragShader == 0) return false;

    programID = glCreateProgram();
    glAttachShader(programID, vertShader);
    glAttachShader(programID, fragShader);
    glLinkProgram(programID);
    glUseProgram(programID);

    // 3. 獲取變數位置
    iLocPosition = glGetAttribLocation(programID, "aPosition");
    iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    iLocTexture = glGetUniformLocation(programID, "uTexture");

    // 4. 設定頂點資料
    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    // 5. 上傳紋理到 GPU
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // 設定紋理參數
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // 使用 Nearest 觀察像素細節
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCENE_WIDTH, SCENE_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, &imageBuffer[0]);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

void GraphicsUpdate() {
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(iLocTexture, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ============================================================================
// 主程式
// ============================================================================
int main(int argc, char* argv[]) {
    // 初始化系統
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

    if (!prepareGraphics()) {
        printf("圖形初始化失敗\n");
        return 1;
    }

    // 渲染迴圈
    bool end = false;
    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
            end = true;
        }
        
        GraphicsUpdate();
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
    }

    // 資源釋放
    glDeleteTextures(1, &textureID);
    glDeleteProgram(programID);
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}