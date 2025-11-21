// cd SDKOpenGLES/YUV2RGBSample/ 
// source ../run_exports.sh
// export DISPLAY=:0
// make clean
// make
// ./YUV2RGBSample  Supportingfiles/blue.bmp  gray_90.bmp  gray_150.bmp gray_180.bmp gray_200.bmp gray_250.bmp
// last edited: 20251119
// 20251119 調整解析度:1920*1080
// 20251119 增加註解
// 20251117 計算各階段時間
// 20251111 針對RGB三通道插值計算

// #include <GLES2/gl2.h>
// #include <GLES2/gl2ext.h>
// #include <EGL/egl.h>
// #include <cstdio>
// #include <cstdlib>
// #include <cstring>
// #include <vector>
// #include <string>
// #include <chrono>
// #include <stdio.h>
// #include <cmath> // 為了 abs()

// // 自定義標頭檔 (假設這些是封裝好的視窗與 EGL 工具)
// #include "XLinuxPodium.h"
// #include "XGLSLCompile.h"
// #include "XEGLIntf.h"

// // ============================================================================
// // 常數定義
// // ============================================================================
// // 場景視窗的寬度和高度 (解析度)
// #define SCENE_WIDTH 1920
// #define SCENE_HEIGHT 1080

// using std::string;
// using std::vector;

// // ============================================================================
// // 工具類別：計時器 (Timer)
// // ============================================================================
// /**
//  * 用於測量程式碼區塊執行時間的輔助類別 (RAII 風格)
//  * 建構時開始計時，解構時自動印出經過時間
//  */
// class Timer {
// private:
//     std::chrono::high_resolution_clock::time_point start_time;
//     const char* name;
    
// public:
//     Timer(const char* timer_name) : name(timer_name) {
//         start_time = std::chrono::high_resolution_clock::now();
//     }
    
//     ~Timer() {
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//         printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0);
//     }
    
//     // 手動獲取經過時間 (毫秒)
//     double getElapsedMs() {
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//         return duration.count() / 1000.0;
//     }
// };

// // ============================================================================
// // BMP 檔案格式結構定義
// // ============================================================================
// #pragma pack(push, 1)  // 強制結構體以 1 byte 對齊，避免編譯器自動填充 (Padding) 造成讀取錯誤

// // BMP 檔案標頭 (File Header) - 共 14 bytes
// typedef struct {
//     uint16_t type;        // 檔案類型標記，必須是 0x4D42 (ASCII 的 'BM')
//     uint32_t size;        // 整個檔案的大小 (bytes)
//     uint16_t reserved1;   // 保留欄位，必須為 0
//     uint16_t reserved2;   // 保留欄位，必須為 0
//     uint32_t offset;      // 像素資料在檔案中的起始偏移量 (Offset)
// } BMPFileHeader;

// // BMP 資訊標頭 (Info Header) - 共 40 bytes (Windows V3 Header)
// typedef struct {
//     uint32_t size;              // 此結構體的大小 (通常為 40)
//     int32_t width;              // 圖像寬度 (pixels)
//     int32_t height;             // 圖像高度 (pixels)
//     uint16_t planes;            // 色彩平面數，必須為 1
//     uint16_t bits;              // 每像素位元數 (如 24 代表 RGB 888)
//     uint32_t compression;       // 壓縮類型 (0 = BI_RGB 無壓縮)
//     uint32_t imagesize;         // 原始點陣圖資料大小 (bytes)
//     int32_t xresolution;        // 水平解析度 (像素/米)
//     int32_t yresolution;        // 垂直解析度 (像素/米)
//     uint32_t ncolours;          // 調色盤使用的顏色數 (0 代表全部)
//     uint32_t importantcolours;  // 重要顏色數
// } BMPInfoHeader;
// #pragma pack(pop)  // 恢復預設的記憶體對齊方式

// // ============================================================================
// // 全域變數
// // ============================================================================
// string resourceDirectory = "Supportingfiles/";  // 資源檔案目錄路徑

// // OpenGL Shader 程式相關 ID
// GLuint programID;             // Shader 程式 ID (Program Object)
// GLint iLocPosition = -1;      // Attribute: 頂點位置 (aPosition)
// GLint iLocTexCoord = -1;      // Attribute: 紋理座標 (aTexCoord)

// // Uniform Location: 傳遞給 Shader 的全域參數位置
// GLint iLocInputTexture = -1;  // 原始輸入圖片紋理
// GLint iLocControlPoint[5] = {-1, -1, -1, -1, -1};  // 5個控制點紋理 (用於曲線調整)
// GLint iLocFixedX = -1;        // 曲線的 X 軸固定座標點

// // OpenGL 紋理 ID (Handle)
// GLuint inputTextureID;           // 輸入圖片的紋理 ID
// GLuint controlPointTextureID[5]; // 5個控制點圖片的紋理 ID

// // 圖片尺寸 (假設所有圖片尺寸相同)
// int imageWidth = 0;
// int imageHeight = 0;

// // ============================================================================
// // 曲線調整參數
// // ============================================================================
// // 這些是色彩曲線上的固定 X 座標節點 (Input Level)
// // 數值已經正規化到 [0, 1] 範圍 (原值/255)
// // 用於 Fragment Shader 中的分段線性插值
// const float FIXED_X[5] = {
//     32.0f/255.0f,   // 節點 1: 暗部 (Shadows)
//     64.0f/255.0f,  // 節點 2: 中間調偏暗
//     128.0f/255.0f,  // 節點 3: 中間調 (Midtones)
//     192.0f/255.0f,  // 節點 4: 中間調偏亮
//     255.0f/255.0f   // 節點 5: 亮部 (Highlights)
// };

// // ============================================================================
// // 頂點資料 (Quad Data)
// // ============================================================================
// // 全螢幕四邊形的頂點座標 (Normalized Device Coordinates, NDC)
// // 範圍從 -1.0 (左/下) 到 1.0 (右/上)
// const GLfloat vertexVertices[] = {
//     -1.0f, -1.0f,  // 左下
//      1.0f, -1.0f,  // 右下
//     -1.0f,  1.0f,  // 左上
//      1.0f,  1.0f   // 右上
// };

// // 對應的紋理座標 (UV Coordinates)
// // 範圍從 0.0 (左/下) 到 1.0 (右/上)，對應整個紋理圖像
// const GLfloat textureVertices[] = {
//     0.0f, 0.0f,  // 左下
//     1.0f, 0.0f,  // 右下
//     0.0f, 1.0f,  // 左上
//     1.0f, 1.0f   // 右上
// };

// // ============================================================================
// // 函數：載入 BMP 圖片
// // ============================================================================
// /**
//  * 讀取 24-bit BMP 檔案並轉換為 RGB 陣列
//  * * @param filename 檔案路徑
//  * @param data 輸出: 儲存像素資料的 vector (RGB順序)
//  * @param width 輸出: 圖片寬度
//  * @param height 輸出: 圖片高度
//  * @return 成功回傳 true，失敗回傳 false
//  */
// bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
//     FILE* file = fopen(filename, "rb");
//     if (!file) {
//         printf("錯誤: 無法開啟檔案 %s\n", filename);
//         return false;
//     }
    
//     BMPFileHeader fileHeader;
//     BMPInfoHeader infoHeader;
    
//     // 讀取標頭資訊
//     if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 ||
//         fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) {
//         printf("錯誤: 讀取 BMP 標頭失敗 %s\n", filename);
//         fclose(file);
//         return false;
//     }
    
//     // 檢查魔術數字 (Magic Number)
//     if (fileHeader.type != 0x4D42) {
//         printf("錯誤: 不是有效的 BMP 檔案 %s\n", filename);
//         fclose(file);
//         return false;
//     }
     
//     // 本程式僅支援 24-bit (RGB) 格式
//     if (infoHeader.bits != 24) {
//         printf("錯誤: 僅支援 24-bit BMP %s\n", filename);
//         fclose(file);
//         return false;
//     }
    
//     width = infoHeader.width;
//     height = std::abs(infoHeader.height); // 高度可能為負，表示由上而下儲存
    
//     // BMP 的每一列 (Row) 資料長度必須是 4 bytes 的倍數
//     // 計算每列包含 Padding 的實際位元組數
//     int rowSize = ((width * 3 + 3) / 4) * 4;
//     int imageSize = rowSize * height;
    
//     vector<unsigned char> rawData(imageSize);
//     fseek(file, fileHeader.offset, SEEK_SET); // 跳到像素資料開始處
//     fread(&rawData[0], 1, imageSize, file);
//     fclose(file);
    
//     // 將 BGR (BMP 標準) 轉換為 RGB (OpenGL 標準) 並移除 Padding
//     data.resize(width * height * 3);
//     for (int y = 0; y < height; y++) {
//         for (int x = 0; x < width; x++) {
//             int srcIdx = y * rowSize + x * 3;      // 來源索引 (含 Padding)
//             int dstIdx = y * width * 3 + x * 3;    // 目標索引 (緊密排列)
            
//             // BMP 儲存順序為 B, G, R，需交換為 R, G, B
//             data[dstIdx]     = rawData[srcIdx + 2]; // R
//             data[dstIdx + 1] = rawData[srcIdx + 1]; // G
//             data[dstIdx + 2] = rawData[srcIdx];     // B
//         }
//     }
    
//     printf("成功載入: %s (%dx%d)\n", filename, width, height);
//     return true;
// }

// // ============================================================================
// // Vertex Shader (頂點著色器)
// // ============================================================================
// /**
//  * 處理每個頂點的程式。
//  * 這裡主要負責傳遞位置和紋理座標。
//  */
// const char* vertexShaderSource = R"(
// attribute vec2 aPosition;  // 輸入: 頂點位置 (x, y)
// attribute vec2 aTexCoord;  // 輸入: 紋理座標 (u, v)
// varying vec2 vTexCoord;    // 輸出: 插值後的紋理座標 (傳給 Fragment Shader)

// void main() {
//     // 設定頂點位置 (z=0.0, w=1.0)
//     gl_Position = vec4(aPosition, 0.0, 1.0);
    
//     // 直接傳遞紋理座標
//     vTexCoord = aTexCoord;
// }
// )";

// // ============================================================================
// // Fragment Shader (片段著色器)
// // ============================================================================
// /**
//  * 處理每個像素顏色的程式。
//  * 核心邏輯：讀取原始顏色，根據 5 個控制點紋理提供的 Y 值，進行曲線映射。
//  */
// const char* fragmentShaderSource = R"(
// precision lowp float;      // 設定浮點數精度為低精度 (提升效能)
// varying vec2 vTexCoord;    // 從 Vertex Shader 接收的紋理座標

// // Uniforms: 外部傳入的紋理單元
// uniform sampler2D uInputTexture;    // 原始影像
// uniform sampler2D uControlPoint0;   // 控制點 0 的 Y 值圖 (對應 FIXED_X[0])
// uniform sampler2D uControlPoint1;   // 控制點 1 的 Y 值圖
// uniform sampler2D uControlPoint2;   // 控制點 2 的 Y 值圖
// uniform sampler2D uControlPoint3;   // 控制點 3 的 Y 值圖
// uniform sampler2D uControlPoint4;   // 控制點 4 的 Y 值圖
// uniform vec2 uFixedX[5];            // 5 個控制點的 X 座標 (固定值)

// // ----------------------------------------------------------------------------
// // 函數: 分段線性插值 (Piecewise Linear Interpolation)
// // ----------------------------------------------------------------------------
// /**
//  * 給定輸入值 x 和 5 個控制點的輸出值 y0~y4，計算對應的輸出 y。
//  * 邏輯：找出 x 落在 X 軸的哪個區間，然後在該區間內做線性插值。
//  */
// float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
//     // 取出 X 軸節點值
//     float x0 = uFixedX[0].x;
//     float x1 = uFixedX[1].x;
//     float x2 = uFixedX[2].x;
//     float x3 = uFixedX[3].x;
//     float x4 = uFixedX[4].x;
    
//     float x_low, x_high, y_low, y_high;
    
//     // 判斷 x 所在的區間
//     if (x < x0) {
//         // 區間 0 左側 (暗部): 使用 (x0, y0) 和 (x1, y1) 向外插值 (Extrapolation)
//         x_low = x0; x_high = x1;
//         y_low = y0; y_high = y1;
//     } else if (x > x4) {
//         // 區間 4 右側 (亮部): 使用 (x3, y3) 和 (x4, y4) 向外插值
//         x_low = x3; x_high = x4;
//         y_low = y3; y_high = y4;
//     } else {
//         // 正常範圍內
//         if (x >= x0 && x <= x1) {
//             x_low = x0; x_high = x1;
//             y_low = y0; y_high = y1;
//         } else if (x >= x1 && x <= x2) {
//             x_low = x1; x_high = x2;
//             y_low = y1; y_high = y2;
//         } else if (x >= x2 && x <= x3) {
//             x_low = x2; x_high = x3;
//             y_low = y2; y_high = y3;
//         } else {
//             x_low = x3; x_high = x4;
//             y_low = y3; y_high = y4;
//         }
//     }
    
//     // 計算斜率 m
//     float m = (y_high - y_low) / (x_high - x_low);
    
//     // 線性方程: y = y_low + slope * (x - x_low)
//     float y = y_low + m * (x - x_low);
    
//     // 確保輸出值在 [0, 1] 範圍內
//     return clamp(y, 0.0, 1.0);
// }

// void main() {
//     // 1. 採樣原始圖片顏色
//     vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;
    
//     // 2. 採樣 5 個控制點紋理在當前位置的值 (這些紋理儲存了局部的曲線調整參數)
//     // 假設控制圖是灰階的，取 R 通道即可代表亮度/數值
//     float y0_r = texture2D(uControlPoint0, vTexCoord).r;
//     float y1_r = texture2D(uControlPoint1, vTexCoord).r;
//     float y2_r = texture2D(uControlPoint2, vTexCoord).r;
//     float y3_r = texture2D(uControlPoint3, vTexCoord).r;
//     float y4_r = texture2D(uControlPoint4, vTexCoord).r;

//     float y0_g = texture2D(uControlPoint0, vTexCoord).g;
//     float y1_g = texture2D(uControlPoint1, vTexCoord).g;
//     float y2_g = texture2D(uControlPoint2, vTexCoord).g;
//     float y3_g = texture2D(uControlPoint3, vTexCoord).g;
//     float y4_g = texture2D(uControlPoint4, vTexCoord).g;

//     float y0_b = texture2D(uControlPoint0, vTexCoord).b;
//     float y1_b = texture2D(uControlPoint1, vTexCoord).b;
//     float y2_b = texture2D(uControlPoint2, vTexCoord).b;
//     float y3_b = texture2D(uControlPoint3, vTexCoord).b;
//     float y4_b = texture2D(uControlPoint4, vTexCoord).b;
    
//     // 3. 對 RGB 三個通道分別進行曲線映射
//     float newR = interpolate(inputColor.r, y0_r, y1_r, y2_r, y3_r, y4_r);
//     float newG = interpolate(inputColor.g, y0_g, y1_g, y2_g, y3_g, y4_g);
//     float newB = interpolate(inputColor.b, y0_b, y1_b, y2_b, y3_b, y4_b);
    
//     // 4. 輸出最終顏色
//     gl_FragColor = vec4(newR, newG, newB, 1.0);
// }
// )";

// // ============================================================================
// // 函數：編譯 Shader
// // ============================================================================
// GLuint compileShader(GLenum type, const char* source) {
//     GLuint shader = glCreateShader(type);
//     glShaderSource(shader, 1, &source, NULL);
//     glCompileShader(shader);
    
//     GLint compiled;
//     glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
//     if (!compiled) {
//         GLint infoLen = 0;
//         glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
//         if (infoLen > 0) {
//             char* infoLog = (char*)malloc(infoLen);
//             glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
//             printf("Shader 編譯失敗:\n%s\n", infoLog);
//             free(infoLog);
//         }
//         glDeleteShader(shader);
//         return 0;
//     }
//     return shader;
// }

// // ============================================================================
// // 函數：初始化圖形系統
// // ============================================================================
// bool prepareGraphics(const char* inputFile, const char* controlFiles[5]) {
//     printf("正在初始化圖形資源 (解析度: %dx%d)...\n", SCENE_WIDTH, SCENE_HEIGHT);
    
//     // 1. 載入原始圖片
//     vector<unsigned char> inputData;
//     if (!loadBMP(inputFile, inputData, imageWidth, imageHeight)) {
//         return false;
//     }
    
//     // 2. 載入 5 張控制點圖片
//     vector<unsigned char> controlData[5];
//     for (int i = 0; i < 5; i++) {
//         int w, h;
//         if (!loadBMP(controlFiles[i], controlData[i], w, h)) {
//             return false;
//         }
//         // 檢查尺寸一致性：控制圖必須與原圖大小相同
//         if (w != imageWidth || h != imageHeight) {
//             printf("錯誤: 控制點圖片尺寸 (%dx%d) 與原圖不符\n", w, h);
//             return false;
//         }
//     }
    
//     // 3. 編譯 Shader
//     GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
//     GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
//     if (vertShader == 0 || fragShader == 0) return false;
    
//     // 4. 連結 Shader Program
//     programID = glCreateProgram();
//     glAttachShader(programID, vertShader);
//     glAttachShader(programID, fragShader);
//     glLinkProgram(programID);
//     glUseProgram(programID);
    
//     // 5. 獲取變數位置 (Locations)
//     iLocPosition = glGetAttribLocation(programID, "aPosition");
//     iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    
//     iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
//     iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
//     iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
//     iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
//     iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
//     iLocControlPoint[4] = glGetUniformLocation(programID, "uControlPoint4");
//     iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    
//     // 6. 設定 VBO (Vertex Attributes)
//     glEnableVertexAttribArray(iLocPosition);
//     glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    
//     glEnableVertexAttribArray(iLocTexCoord);
//     glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
//     // 7. 建立並上傳輸入紋理
//     glGenTextures(1, &inputTextureID);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &inputData[0]);
    
//     // 設定紋理過濾與 Wrap 模式
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
//     // 8. 建立並上傳 5 個控制點紋理
//     for (int i = 0; i < 5; i++) {
//         glGenTextures(1, &controlPointTextureID[i]);
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//     }
    
//     // 9. 基本 OpenGL 設定
//     glClearColor(0.0f, 0.0f, 0.2f, 1.0f); // 背景設為深藍色
//     glDisable(GL_DEPTH_TEST); // 2D 圖像處理不需要深度測試
    
//     printf("OpenGL 初始化完成。\n");
//     return true;
// }

// // ============================================================================
// // 函數：圖形渲染迴圈
// // ============================================================================
// void GraphicsUpdate() {
//     // 清除畫面
//     glClear(GL_COLOR_BUFFER_BIT);
    
//     // 設定視埠 (Viewport) 大小
//     glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    
//     // 1. 綁定紋理到對應的 Texture Unit
//     // Unit 0: 原始影像
//     glActiveTexture(GL_TEXTURE0);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     glUniform1i(iLocInputTexture, 0);
    
//     // Unit 1~5: 控制點影像
//     for (int i = 0; i < 5; i++) {
//         glActiveTexture(GL_TEXTURE1 + i);
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glUniform1i(iLocControlPoint[i], 1 + i);
//     }
    
//     // 2. 更新 Uniform 變數
//     glUniform2fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
    
//     // 3. 繪製四邊形 (觸發 Fragment Shader)
//     // GL_TRIANGLE_STRIP: 使用 4 個頂點繪製 2 個三角形組成的矩形
//     glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
// }

// // ============================================================================
// // 主程式 (Entry Point)
// // ============================================================================
// int main(int argc, char* argv[]) {
//     auto program_start = std::chrono::high_resolution_clock::now();
    
//     // 檢查參數數量
//     if (argc != 7) {
//         printf("使用方法: %s <輸入BMP> <點1> <點2> <點3> <點4> <點5>\n", argv[0]);
//         return 1;
//     }
    
//     const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};
    
//     // 1. 初始化視窗與 EGL 環境
//     Timer timer("1. 系統初始化");
    
//     XPodium *podium = XPodium::getHandler();
//     podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT); // 建立視窗
//     CoreEGL::initializeEGL(CoreEGL::OPENGLES2);       // 初始化 EGL context
//     eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
//     // 2. 初始化 OpenGL 資源 (Shader, Textures)
//     {
//         Timer timer("2. 圖形資源載入");
//         if (!prepareGraphics(argv[1], controlFiles)) {
//             printf("圖形初始化失敗！\n");
//             return 1;
//         }
//     }
    
//     printf("\n--- 系統就緒，開始渲染迴圈 ---\n");
//     printf("按任意鍵或關閉視窗以退出...\n\n");
    
//     // 3. 主渲染迴圈
//     bool end = false;
//     int frame_count = 0;
//     double total_render_time = 0.0;
    
//     auto render_loop_start = std::chrono::high_resolution_clock::now();
    
//     while (!end) {
//         // 檢查視窗事件 (如關閉按鈕)
//         if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
//             end = true;
//         }
        
//         auto frame_start = std::chrono::high_resolution_clock::now();
        
//         // 執行渲染
//         GraphicsUpdate();
        
//         // 交換前後緩衝區 (顯示畫面)
//         eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
        
//         auto frame_end = std::chrono::high_resolution_clock::now();
//         double frame_ms = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start).count() / 1000.0;
        
//         total_render_time += frame_ms;
//         frame_count++;
//         printf("第 %d 幀 FPS: %.1f (Frame Time: %.3f ms)\n", frame_count, 1000.0 / frame_ms, frame_ms);
//     }
    
//     // 4. 清理資源
//     {
//         Timer timer("4. 資源釋放");
//         glDeleteTextures(1, &inputTextureID);
//         glDeleteTextures(5, controlPointTextureID);
//         glDeleteProgram(programID);
//         CoreEGL::terminateEGL();
//         podium->destroyWindow();
//         delete podium;
//     }
    
//     auto program_end = std::chrono::high_resolution_clock::now();
//     printf("總執行時間: %.3f 秒\n", std::chrono::duration_cast<std::chrono::milliseconds>(program_end - program_start).count() / 1000.0);
    
//     return 0;
// }

// MP4
// sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
// LIBS = -lGLESv2 -lEGL -lX11 -lavcodec -lavformat -lavutil -lswscale

// #include <GLES2/gl2.h>
// #include <GLES2/gl2ext.h>
// #include <EGL/egl.h>
// #include <cstdio>
// #include <cstdlib>
// #include <cstring>
// #include <vector>
// #include <string>
// #include <chrono>
// #include <stdio.h>
// #include <cmath>
// #include <thread> // 用於控制播放速度

// // ============================================================================
// // FFmpeg Headers (必須包在 extern "C" 中)
// // ============================================================================
// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libswscale/swscale.h>
// #include <libavutil/imgutils.h>
// }

// // 自定義標頭檔
// #include "XLinuxPodium.h"
// #include "XGLSLCompile.h"
// #include "XEGLIntf.h"

// #define SCENE_WIDTH 1920
// #define SCENE_HEIGHT 1080

// using std::string;
// using std::vector;

// // ... (Timer 類別與 BMP 結構定義保持不變，為節省篇幅省略，請保留原有的) ...

// class Timer {
// private:
//     std::chrono::high_resolution_clock::time_point start_time;
//     const char* name;
// public:
//     Timer(const char* timer_name) : name(timer_name) {
//         start_time = std::chrono::high_resolution_clock::now();
//     }
//     ~Timer() {
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//         // printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0); // 註解掉避免影片播放時洗版
//     }
// };

// // 保留原有的 loadBMP 函數 (用於載入控制點)
// // ... (請在此處保留 loadBMP 實作) ...
// // 為了完整性，這裡假設 loadBMP 已定義 (同你原本的程式碼)
// #pragma pack(push, 1)
// typedef struct { uint16_t type; uint32_t size; uint16_t reserved1; uint16_t reserved2; uint32_t offset; } BMPFileHeader;
// typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t compression; uint32_t imagesize; int32_t xresolution; int32_t yresolution; uint32_t ncolours; uint32_t importantcolours; } BMPInfoHeader;
// #pragma pack(pop)

// bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
//     FILE* file = fopen(filename, "rb");
//     if (!file) return false;
//     BMPFileHeader fileHeader; BMPInfoHeader infoHeader;
//     if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 || fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) { fclose(file); return false; }
//     width = infoHeader.width; height = std::abs(infoHeader.height);
//     int rowSize = ((width * 3 + 3) / 4) * 4;
//     vector<unsigned char> rawData(rowSize * height);
//     fseek(file, fileHeader.offset, SEEK_SET);
//     fread(&rawData[0], 1, rawData.size(), file);
//     fclose(file);
//     data.resize(width * height * 3);
//     for (int y = 0; y < height; y++) {
//         for (int x = 0; x < width; x++) {
//             int srcIdx = y * rowSize + x * 3;
//             int dstIdx = (height - 1 - y) * width * 3 + x * 3; // BMP 上下顛倒，這裡修正翻轉
//              if (infoHeader.height < 0) dstIdx = y * width * 3 + x * 3; // 如果原本就是正向
            
//             data[dstIdx]     = rawData[srcIdx + 2]; // R
//             data[dstIdx + 1] = rawData[srcIdx + 1]; // G
//             data[dstIdx + 2] = rawData[srcIdx];     // B
//         }
//     }
//     return true;
// }

// // ============================================================================
// // 新增：影片讀取類別 (封裝 FFmpeg)
// // ============================================================================
// class VideoReader {
// public:
//     AVFormatContext* format_ctx = nullptr;
//     AVCodecContext* codec_ctx = nullptr;
//     int video_stream_index = -1;
//     AVFrame* frame = nullptr;
//     AVFrame* frame_rgb = nullptr;
//     AVPacket* packet = nullptr;
//     struct SwsContext* sws_ctx = nullptr;
//     uint8_t* buffer = nullptr;
//     int width = 0;
//     int height = 0;

//     VideoReader() {}

//     ~VideoReader() {
//         if (buffer) av_free(buffer);
//         if (frame) av_frame_free(&frame);
//         if (frame_rgb) av_frame_free(&frame_rgb);
//         if (packet) av_packet_free(&packet);
//         if (codec_ctx) avcodec_free_context(&codec_ctx);
//         if (format_ctx) avformat_close_input(&format_ctx);
//     }

//     bool open(const char* filename) {
//         // 1. 開啟檔案
//         if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) != 0) {
//             printf("FFmpeg: 無法開啟影片檔案 %s\n", filename);
//             return false;
//         }

//         // 2. 尋找串流資訊
//         if (avformat_find_stream_info(format_ctx, nullptr) < 0) return false;

//         // 3. 尋找視訊串流與解碼器
//         const AVCodec *codec = NULL;
//         video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
//         if (video_stream_index < 0) {
//             printf("FFmpeg: 找不到視訊串流\n");
//             return false;
//         }

//         codec_ctx = avcodec_alloc_context3(codec);
//         avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar);

//         if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
//             printf("FFmpeg: 無法開啟解碼器\n");
//             return false;
//         }

//         width = codec_ctx->width;
//         height = codec_ctx->height;
//         printf("影片資訊: %dx%d, Codec: %s\n", width, height, codec->name);

//         // 4. 配置 Frame 與 Packet
//         frame = av_frame_alloc();
//         frame_rgb = av_frame_alloc();
//         packet = av_packet_alloc();

//         // 5. 設定影像轉換 context (YUV -> RGB)
//         // 注意: OpenGL ES 需要 RGB 格式 (GL_RGB)
//         sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
//                                  width, height, AV_PIX_FMT_RGB24,
//                                  SWS_BILINEAR, nullptr, nullptr, nullptr);

//         // 配置 RGB buffer
//         int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
//         buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
//         av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, buffer, AV_PIX_FMT_RGB24, width, height, 1);

//         return true;
//     }

//     // 讀取並解碼下一幀，存入 internal RGB buffer
//     // 回傳: true 表示讀取成功，false 表示影片結束或錯誤
//     bool readNextFrame(uint8_t** outData) {
//         while (av_read_frame(format_ctx, packet) >= 0) {
//             if (packet->stream_index == video_stream_index) {
//                 // 發送封包給解碼器
//                 if (avcodec_send_packet(codec_ctx, packet) == 0) {
//                     // 從解碼器接收 Frame
//                     int ret = avcodec_receive_frame(codec_ctx, frame);
//                     if (ret == 0) {
//                         // 轉換 YUV 到 RGB
//                         sws_scale(sws_ctx, (uint8_t const* const*)frame->data,
//                                   frame->linesize, 0, height,
//                                   frame_rgb->data, frame_rgb->linesize);
                        
//                         *outData = frame_rgb->data[0];
//                         av_packet_unref(packet);
//                         return true; // 成功取得一幀
//                     }
//                 }
//             }
//             av_packet_unref(packet);
//         }
        
//         // 影片讀取完畢，可以選擇 seek 到開頭 (Loop)
//         av_seek_frame(format_ctx, video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
//         return false; 
//     }
// };

// // ============================================================================
// // 全域變數 (新增 videoReader)
// // ============================================================================
// VideoReader videoReader;
// string resourceDirectory = "Supportingfiles/";

// GLuint programID;
// GLint iLocPosition = -1;
// GLint iLocTexCoord = -1;
// GLint iLocInputTexture = -1;
// GLint iLocControlPoint[5];
// GLint iLocFixedX = -1;

// GLuint inputTextureID;
// GLuint controlPointTextureID[5];

// const float FIXED_X[5] = {
//     32.0f/255.0f, 64.0f/255.0f, 128.0f/255.0f, 192.0f/255.0f, 255.0f/255.0f
// };

// // Quad 頂點資料 (不變)
// const GLfloat vertexVertices[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
// const GLfloat textureVertices[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f }; // 修正 UV 方向適配 FFmpeg

// // ============================================================================
// // Shader Source (保持不變)
// // ============================================================================
// // 為了節省篇幅，這裡直接使用原變數名，內容請保持你原本的 vertexShaderSource 和 fragmentShaderSource
// const char* vertexShaderSource = R"(
// attribute vec2 aPosition;
// attribute vec2 aTexCoord;
// varying vec2 vTexCoord;
// void main() {
//     gl_Position = vec4(aPosition, 0.0, 1.0);
//     vTexCoord = aTexCoord;
// }
// )";

// const char* fragmentShaderSource = R"(
// precision mediump float;
// varying vec2 vTexCoord;
// uniform sampler2D uInputTexture;
// uniform sampler2D uControlPoint0;
// uniform sampler2D uControlPoint1;
// uniform sampler2D uControlPoint2;
// uniform sampler2D uControlPoint3;
// uniform sampler2D uControlPoint4;
// uniform vec2 uFixedX[5];

// float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
//     float x0 = uFixedX[0].x; float x1 = uFixedX[1].x; float x2 = uFixedX[2].x; float x3 = uFixedX[3].x; float x4 = uFixedX[4].x;
//     float x_low, x_high, y_low, y_high;
//     if (x < x0) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; }
//     else if (x > x4) { x_low = x3; x_high = x4; y_low = y3; y_high = y4; }
//     else {
//         if (x >= x0 && x <= x1) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; }
//         else if (x >= x1 && x <= x2) { x_low = x1; x_high = x2; y_low = y1; y_high = y2; }
//         else if (x >= x2 && x <= x3) { x_low = x2; x_high = x3; y_low = y2; y_high = y3; }
//         else { x_low = x3; x_high = x4; y_low = y3; y_high = y4; }
//     }
//     float m = (y_high - y_low) / (x_high - x_low);
//     return clamp(y_low + m * (x - x_low), 0.0, 1.0);
// }

// void main() {
//     vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;
//     float y0_r = texture2D(uControlPoint0, vTexCoord).r;
//     float y1_r = texture2D(uControlPoint1, vTexCoord).r;
//     float y2_r = texture2D(uControlPoint2, vTexCoord).r;
//     float y3_r = texture2D(uControlPoint3, vTexCoord).r;
//     float y4_r = texture2D(uControlPoint4, vTexCoord).r;

//     float y0_g = texture2D(uControlPoint0, vTexCoord).g;
//     float y1_g = texture2D(uControlPoint1, vTexCoord).g;
//     float y2_g = texture2D(uControlPoint2, vTexCoord).g;
//     float y3_g = texture2D(uControlPoint3, vTexCoord).g;
//     float y4_g = texture2D(uControlPoint4, vTexCoord).g;

//     float y0_b = texture2D(uControlPoint0, vTexCoord).b;
//     float y1_b = texture2D(uControlPoint1, vTexCoord).b;
//     float y2_b = texture2D(uControlPoint2, vTexCoord).b;
//     float y3_b = texture2D(uControlPoint3, vTexCoord).b;
//     float y4_b = texture2D(uControlPoint4, vTexCoord).b;

//     gl_FragColor = vec4(
//         interpolate(inputColor.r, y0_r, y1_r, y2_r, y3_r, y4_r),
//         interpolate(inputColor.g, y0_g, y1_g, y2_g, y3_g, y4_g),
//         interpolate(inputColor.b, y0_b, y1_b, y2_b, y3_b, y4_b),
//         1.0
//     );
// }
// )";

// // ... (compileShader 函數保持不變) ...
// GLuint compileShader(GLenum type, const char* source) {
//     GLuint shader = glCreateShader(type);
//     glShaderSource(shader, 1, &source, NULL);
//     glCompileShader(shader);
//     GLint compiled;
//     glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
//     if (!compiled) {
//         GLint infoLen = 0;
//         glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
//         if (infoLen > 0) {
//             char* infoLog = (char*)malloc(infoLen);
//             glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
//             printf("Shader 編譯失敗:\n%s\n", infoLog);
//             free(infoLog);
//         }
//         glDeleteShader(shader);
//         return 0;
//     }
//     return shader;
// }

// // ============================================================================
// // 初始化圖形與影片
// // ============================================================================
// bool prepareGraphics(const char* videoFile, const char* controlFiles[5]) {
//     printf("正在初始化資源...\n");

//     // 1. 初始化 FFmpeg 讀取器
//     if (!videoReader.open(videoFile)) {
//         return false;
//     }
    
//     // 2. 載入 5 張控制點圖片 (這部分不變，因為 Demura Mask 是靜態的)
//     // 注意：為了簡化，我們假設控制點圖大小與影片大小相同，或者控制圖會被 Stretch
//     vector<unsigned char> controlData[5];
//     int cW, cH;
//     for (int i = 0; i < 5; i++) {
//         if (!loadBMP(controlFiles[i], controlData[i], cW, cH)) return false;
//         // 實務上若影片與控制圖尺寸不同，Shader 會自動插值 (Stretch)，所以這裡僅做警告
//         if (cW != videoReader.width || cH != videoReader.height) {
//             printf("警告: 控制點圖片尺寸 (%dx%d) 與影片 (%dx%d) 不符\n", cW, cH, videoReader.width, videoReader.height);
//         }
//     }

//     // 3. 編譯 Shader
//     GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
//     GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
//     if (vertShader == 0 || fragShader == 0) return false;

//     programID = glCreateProgram();
//     glAttachShader(programID, vertShader);
//     glAttachShader(programID, fragShader);
//     glLinkProgram(programID);
//     glUseProgram(programID);

//     // 4. 獲取 Locations
//     iLocPosition = glGetAttribLocation(programID, "aPosition");
//     iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
//     iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
//     for(int i=0; i<5; i++) {
//         char name[32]; sprintf(name, "uControlPoint%d", i);
//         iLocControlPoint[i] = glGetUniformLocation(programID, name);
//     }
//     iLocFixedX = glGetUniformLocation(programID, "uFixedX");

//     // 5. 設定 VBO
//     glEnableVertexAttribArray(iLocPosition);
//     glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
//     glEnableVertexAttribArray(iLocTexCoord);
//     glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

//     // 6. 建立影片紋理 (初始為空)
//     glGenTextures(1, &inputTextureID);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     // 先分配記憶體，之後用 glTexSubImage2D 更新
//     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, videoReader.width, videoReader.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

//     // 7. 建立控制點紋理
//     for (int i = 0; i < 5; i++) {
//         glGenTextures(1, &controlPointTextureID[i]);
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cW, cH, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//     }

//     glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
//     return true;
// }

// // ============================================================================
// // 渲染與更新
// // ============================================================================
// void GraphicsUpdate() {
//     // 1. 從 FFmpeg 讀取下一幀
//     uint8_t* frameData = nullptr;
    
//     // 計時解碼時間 (可選)
//     // Timer t("Decode"); 
//     if (videoReader.readNextFrame(&frameData)) {
//         // 2. 更新紋理
//         glActiveTexture(GL_TEXTURE0);
//         glBindTexture(GL_TEXTURE_2D, inputTextureID);
//         // 使用 glTexSubImage2D 替換紋理內容 (比重新建立紋理快)
//         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoReader.width, videoReader.height, GL_RGB, GL_UNSIGNED_BYTE, frameData);
//     }

//     // 3. 繪製 (應用 Shader)
//     glClear(GL_COLOR_BUFFER_BIT);
//     glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

//     glActiveTexture(GL_TEXTURE0);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     glUniform1i(iLocInputTexture, 0);

//     for (int i = 0; i < 5; i++) {
//         glActiveTexture(GL_TEXTURE1 + i);
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glUniform1i(iLocControlPoint[i], 1 + i);
//     }

//     glUniform2fv(iLocFixedX, 5, (GLfloat*)FIXED_X);
//     glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
// }

// // ============================================================================
// // 主程式
// // ============================================================================
// int main(int argc, char* argv[]) {
//     // 初始化 FFmpeg 網絡模組
//     // av_register_all(); 

//     if (argc != 7) {
//         printf("使用方法: %s <影片.mp4> <點1.bmp> <點2.bmp> <點3.bmp> <點4.bmp> <點5.bmp>\n", argv[0]);
//         return 1;
//     }

//     const char* controlFiles[5] = {argv[2], argv[3], argv[4], argv[5], argv[6]};

//     XPodium *podium = XPodium::getHandler();
//     podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
//     CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
//     eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

//     if (!prepareGraphics(argv[1], controlFiles)) {
//         printf("初始化失敗\n");
//         return 1;
//     }

//     printf("\n--- 開始播放與補償 ---\n");
    
//     bool end = false;
//     int frame_count = 0;

//     while (!end) {
//         if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

//         auto start_time = std::chrono::high_resolution_clock::now();

//         GraphicsUpdate();
//         eglSwapBuffers(CoreEGL::display, CoreEGL::surface);

//         frame_count++;
//         auto end_time = std::chrono::high_resolution_clock::now();
//         double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count() / 1000.0;

//         printf("Frame: %d | Cost: %.3f ms\n", frame_count, elapsed);
//     }

//     // 清理
//     glDeleteTextures(1, &inputTextureID);
//     glDeleteTextures(5, controlPointTextureID);
//     glDeleteProgram(programID);
//     CoreEGL::terminateEGL();
//     podium->destroyWindow();
//     delete podium;

//     return 0;
// }

// 原本 (CPU 慢): MP4 (YUV) -> CPU sws_scale (重!) -> RGB Buffer -> glTexImage2D -> GPU
// 優化後 (GPU 快): MP4 (YUV) -> glTexImage2D (Y), (U), (V) -> GPU Fragment Shader 轉 RGB (快!) -> Demura 處理

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
typedef struct { uint16_t type; uint32_t size; uint16_t reserved1; uint16_t reserved2; uint32_t offset; } BMPFileHeader;
typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t compression; uint32_t imagesize; int32_t xresolution; int32_t yresolution; uint32_t ncolours; uint32_t importantcolours; } BMPInfoHeader;
#pragma pack(pop)

bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) return false;
    BMPFileHeader fileHeader; BMPInfoHeader infoHeader;
    if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 || fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) { fclose(file); return false; }
    width = infoHeader.width; height = std::abs(infoHeader.height);
    int rowSize = ((width * 3 + 3) / 4) * 4;
    vector<unsigned char> rawData(rowSize * height);
    fseek(file, fileHeader.offset, SEEK_SET);
    fread(&rawData[0], 1, rawData.size(), file);
    fclose(file);
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = y * rowSize + x * 3;
            int dstIdx = (height - 1 - y) * width * 3 + x * 3; 
             if (infoHeader.height < 0) dstIdx = y * width * 3 + x * 3;
            
            data[dstIdx]     = rawData[srcIdx + 2]; // R
            data[dstIdx + 1] = rawData[srcIdx + 1]; // G
            data[dstIdx + 2] = rawData[srcIdx];     // B
        }
    }
    return true;
}

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
    32.0f/255.0f, 64.0f/255.0f, 128.0f/255.0f, 192.0f/255.0f, 255.0f/255.0f
};

const GLfloat vertexVertices[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
const GLfloat textureVertices[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

// ============================================================================
// Shader Source (Vertex 保持不變，Fragment 大改)
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
precision mediump float;
varying vec2 vTexCoord;

// 輸入改為 3 個紋理
uniform sampler2D uTextureY;
uniform sampler2D uTextureU;
uniform sampler2D uTextureV;

uniform sampler2D uControlPoint0;
uniform sampler2D uControlPoint1;
uniform sampler2D uControlPoint2;
uniform sampler2D uControlPoint3;
uniform sampler2D uControlPoint4;
uniform vec2 uFixedX[5];

// YUV 轉 RGB 函數 (使用 BT.601 標準)
vec3 yuv2rgb(vec2 uv) {
    float y = texture2D(uTextureY, uv).r;
    float u = texture2D(uTextureU, uv).r - 0.5;
    float v = texture2D(uTextureV, uv).r - 0.5;
    
    float r = y + 1.402 * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772 * u;
    
    return vec3(r, g, b);
}

float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    float x0 = uFixedX[0].x; float x1 = uFixedX[1].x; float x2 = uFixedX[2].x; float x3 = uFixedX[3].x; float x4 = uFixedX[4].x;
    float x_low, x_high, y_low, y_high;
    // ... (保持原有的插值邏輯，為節省空間此處省略，請填入原本的 if-else 邏輯) ...
    if (x < x0) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; }
    else if (x > x4) { x_low = x3; x_high = x4; y_low = y3; y_high = y4; }
    else {
        if (x >= x0 && x <= x1) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; }
        else if (x >= x1 && x <= x2) { x_low = x1; x_high = x2; y_low = y1; y_high = y2; }
        else if (x >= x2 && x <= x3) { x_low = x2; x_high = x3; y_low = y2; y_high = y3; }
        else { x_low = x3; x_high = x4; y_low = y3; y_high = y4; }
    }
    float m = (y_high - y_low) / (x_high - x_low);
    return clamp(y_low + m * (x - x_low), 0.0, 1.0);
}

void main() {
    // 1. 先在 Shader 中做 YUV 轉 RGB
    vec3 inputColor = yuv2rgb(vTexCoord);

    // 2. 接著做原本的 Demura 補償
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

    gl_FragColor = vec4(
        interpolate(inputColor.r, y0_r, y1_r, y2_r, y3_r, y4_r),
        interpolate(inputColor.g, y0_g, y1_g, y2_g, y3_g, y4_g),
        interpolate(inputColor.b, y0_b, y1_b, y2_b, y3_b, y4_b),
        1.0
    );
}
)";

// ... (compileShader 保持不變) ...
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
    if (!videoReader.open(videoFile)) return false;
    
    vector<unsigned char> controlData[5];
    int cW, cH;
    for (int i = 0; i < 5; i++) {
        if (!loadBMP(controlFiles[i], controlData[i], cW, cH)) return false;
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
    
    for(int i=0; i<5; i++) {
        char name[32]; sprintf(name, "uControlPoint%d", i);
        iLocControlPoint[i] = glGetUniformLocation(programID, name);
    }
    iLocFixedX = glGetUniformLocation(programID, "uFixedX");

    glEnableVertexAttribArray(iLocPosition);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glEnableVertexAttribArray(iLocTexCoord);
    glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    // 設定 Pixel Store 以確保 1 byte 對齊 (因為 U/V 寬度可能是奇數或非4倍數)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 6. 建立 3 個紋理 (Y, U, V)
    // Texture Y: 寬 x 高
    glGenTextures(1, &textureIdY);
    glBindTexture(GL_TEXTURE_2D, textureIdY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width, videoReader.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture U: 寬/2 x 高/2 (YUV420p)
    glGenTextures(1, &textureIdU);
    glBindTexture(GL_TEXTURE_2D, textureIdU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Texture V: 寬/2 x 高/2
    glGenTextures(1, &textureIdV);
    glBindTexture(GL_TEXTURE_2D, textureIdV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, videoReader.width / 2, videoReader.height / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 7. 建立控制點紋理 (保持不變)
    for (int i = 0; i < 5; i++) {
        glGenTextures(1, &controlPointTextureID[i]);
        glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cW, cH, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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