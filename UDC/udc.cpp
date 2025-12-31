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
#include <iostream>

// 自定義標頭檔 (維持您的環境設定)
#include "XLinuxPodium.h"
#include "XGLSLCompile.h"
#include "XEGLIntf.h"

#define SCENE_WIDTH 1920
#define SCENE_HEIGHT 1080

using std::vector;
using std::min;
using std::max;

// ============================================================================
// 工具：計時器
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
// Shader (Pass-through 用於顯示生成的紋理)
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
// 核心演算法：UDC 補償圖生成
// ============================================================================

// 輔助函數：計算單點的 Alpha (8x8 Super-sampling)
float compute_pixel_alpha(int px, int py, float cx, float cy, float radius_sq, int N) {
    // 快速篩選：如果像素中心距離圓心很遠，直接回傳 0 或 1
    // 使用像素中心點 (px + 0.5)
    float dist_sq_center = pow(px - cx + 0.5f, 2) + pow(py - cy + 0.5f, 2);
    float safe_dist = 2.0f; // 寬鬆邊界
    float r = sqrt(radius_sq);
    
    // 完全在圓內
    if (dist_sq_center <= pow(r - safe_dist, 2)) return 1.0f;
    // 完全在圓外
    if (dist_sq_center > pow(r + safe_dist, 2)) return 0.0f;

    // 邊緣處：執行 8x8 SS
    int hit_count = 0;
    for (int m = 0; m < N; m++) {
        for (int n = 0; n < N; n++) {
            // 子像素偏移
            float off_x = (m + 0.5f) / N;
            float off_y = (n + 0.5f) / N;
            
            // 子像素絕對座標
            float cur_x = px + off_x;
            float cur_y = py + off_y;
            
            float d2 = pow(cur_x - cx, 2) + pow(cur_y - cy, 2);
            if (d2 <= radius_sq) {
                hit_count++;
            }
        }
    }
    return (float)hit_count / (float)(N * N);
}

void generateUDCPattern(vector<unsigned char>& outputData) {
    printf("正在計算 UDC 補償圖案 (Type 0, 1, 2)...\n");

    int width = SCENE_WIDTH;
    int height = SCENE_HEIGHT;
    outputData.resize(width * height * 3); // RGB

    // --- 1. 基礎參數 ---
    int bg_gray_value = 94;
    float gamma = 2.2f;
    float radius = 20.0f;
    float radius_sq = radius * radius;
    int N = 8; // SS level

    // 區域分割
    int x_limit_0 = width / 3;       // 640
    int x_limit_1 = (width / 3) * 2; // 1280
    
    // 中心點
    float cx0 = 320.0f, cy0 = 540.0f;
    float cx1 = 960.0f, cy1 = 540.0f;
    float cx2 = 1600.0f, cy2 = 540.0f;

    // 亮度計算
    float L_out_val = pow(bg_gray_value / 255.0f, gamma);
    float L_in_val = L_out_val * 4.0f;
    if (L_in_val > 1.0f) L_in_val = 1.0f;
    
    // 高亮度的灰階值 (用於 Type 0)
    int high_gray_val = (int)(255.0f * pow(L_in_val, 1.0f / gamma));

    // 預先計算 Gamma 的倒數，加速迴圈
    float inv_gamma = 1.0f / gamma;

    // 為了 Type 2 的 Blur，我們需要先計算出 Type 2 區域的 Alpha Map
    // 範圍：x [1280, 1920), y [0, 1080)
    // 為了節省記憶體，我們只計算圓附近的 ROI (Region of Interest)
    // 但為了代碼簡單對應 Python，這裡開一個局部 buffer 對應 Type 2 區域
    int t2_w = width - x_limit_1; // 640
    int t2_h = height;
    vector<float> alpha_map_2(t2_w * t2_h, 0.0f);

    // 計算 Type 2 的原始 Alpha (包含 Super-sampling)
    // 優化：只計算圓附近的 y
    int y_start_roi = (int)(cy2 - radius - 5);
    int y_end_roi = (int)(cy2 + radius + 5);
    y_start_roi = max(0, y_start_roi);
    y_end_roi = min(height, y_end_roi);

    for (int y = y_start_roi; y < y_end_roi; y++) {
        for (int x_local = 0; x_local < t2_w; x_local++) {
            int x_global = x_limit_1 + x_local;
            alpha_map_2[y * t2_w + x_local] = compute_pixel_alpha(x_global, y, cx2, cy2, radius_sq, N);
        }
    }

    // 計算 Type 2 的 Blur Alpha (3x3 Box Blur)
    // 對應 cv2.blur(alpha, (3,3)) -> Normalized box filter
    vector<float> alpha_prime_2(t2_w * t2_h, 0.0f);
    
    // Blur 迴圈 (邊界不處理或簡單處理，這裡略過極邊界，因為圓在中間)
    for (int y = y_start_roi; y < y_end_roi; y++) {
        for (int x_local = 1; x_local < t2_w - 1; x_local++) {
            float sum = 0.0f;
            // 3x3 卷積
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    // 邊界檢查 safe check
                    int ny = y + dy;
                    if (ny >= 0 && ny < height) {
                        sum += alpha_map_2[ny * t2_w + (x_local + dx)];
                    }
                }
            }
            alpha_prime_2[y * t2_w + x_local] = sum / 9.0f;
        }
    }

    // --- 主生成迴圈 ---
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char final_pixel_val = (unsigned char)bg_gray_value;
            
            // ================= Type 0: Left Zone =================
            if (x < x_limit_0) {
                // 邏輯：Aliased Circle
                float dist_sq = pow(x - cx0 + 0.5f, 2) + pow(y - cy0 + 0.5f, 2);
                bool in_circle = dist_sq <= radius_sq;
                
                if (in_circle) {
                    bool active = (x % 2 == 0) && (y % 2 == 0);
                    final_pixel_val = active ? (unsigned char)high_gray_val : 0;
                }
            }
            // ================= Type 1: Middle Zone =================
            else if (x < x_limit_1) {
                // 1. 計算 Alpha
                float alpha = compute_pixel_alpha(x, y, cx1, cy1, radius_sq, N);
                
                // 2. 混合亮度 (Mix)
                float l_mix = alpha * L_in_val + (1.0f - alpha) * L_out_val;
                float gray_mix_f = 255.0f * pow(l_mix, inv_gamma);
                
                // Clamp
                if (gray_mix_f > 255.0f) gray_mix_f = 255.0f;
                if (gray_mix_f < 0.0f) gray_mix_f = 0.0f;
                unsigned char gray_mix = (unsigned char)gray_mix_f;

                // 3. 挖空邏輯 (Mask Gaps)
                // 條件：像素完全在圓內 (alpha >= 0.99) 且是 inactive 位置 (奇數座標)
                // 邊緣處 (alpha < 0.99) 保留漸層，不挖空
                bool is_gap = (x % 2 != 0) || (y % 2 != 0);
                bool fully_inside = alpha >= 0.99f;
                
                if (fully_inside && is_gap) {
                    final_pixel_val = 0;
                } else {
                    final_pixel_val = gray_mix;
                }
            }
            // ================= Type 2: Right Zone =================
            else {
                int x_local = x - x_limit_1;
                
                // 取得預先計算的 Alpha 和 Blurred Alpha
                float alpha_orig = alpha_map_2[y * t2_w + x_local];
                float alpha_blur = alpha_prime_2[y * t2_w + x_local];

                // 1. 使用 Blurred Alpha 進行亮度混合
                float l_mix = alpha_blur * L_in_val + (1.0f - alpha_blur) * L_out_val;
                float gray_mix_f = 255.0f * pow(l_mix, inv_gamma);
                
                if (gray_mix_f > 255.0f) gray_mix_f = 255.0f;
                if (gray_mix_f < 0.0f) gray_mix_f = 0.0f;
                unsigned char gray_mix = (unsigned char)gray_mix_f;

                // 2. 挖空邏輯
                // [重要] 這裡要用 "原始 Alpha" 來判斷幾何邊界，避免 Blur 擴大挖空範圍
                bool is_gap = (x % 2 != 0) || (y % 2 != 0);
                bool fully_inside = alpha_orig >= 0.99f;

                if (fully_inside && is_gap) {
                    final_pixel_val = 0;
                } else {
                    final_pixel_val = gray_mix;
                }
            }

            // 寫入 RGB Buffer
            int idx = (y * width + x) * 3;
            outputData[idx + 0] = final_pixel_val;
            outputData[idx + 1] = final_pixel_val;
            outputData[idx + 2] = final_pixel_val;
        }
    }
}

// ============================================================================
// OpenGL 全域變數與初始化
// ============================================================================
GLuint programID;
GLuint textureID;
GLint iLocPosition = -1;
GLint iLocTexCoord = -1;
GLint iLocTexture = -1;

const GLfloat vertexVertices[] = { -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f };
const GLfloat textureVertices[] = { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f };

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) return 0;
    return shader;
}

bool prepareGraphics() {
    printf("初始化圖形資源...\n");

    // 1. 生成 UDC 影像
    vector<unsigned char> imageBuffer;
    {
        Timer t("UDC Pattern 生成");
        generateUDCPattern(imageBuffer);
    }

    // 2. OpenGL 設定
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
    iLocTexture = glGetUniformLocation(programID, "uTexture");

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

    if (!prepareGraphics()) return 1;

    bool end = false;
    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;
        GraphicsUpdate();
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
    }

    glDeleteTextures(1, &textureID);
    glDeleteProgram(programID);
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}