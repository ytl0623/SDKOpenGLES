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

// // 自定義標頭檔 (假設這些是封裝好的視窗與 EGL 工具)
// // XLinuxPodium: 負責 Linux 底層視窗系統 (如 X11 或 Wayland) 的管理
// // XGLSLCompile: 可能包含輔助 Shader 編譯的工具
// // XEGLIntf: 負責 EGL Context 的初始化與管理
// #include "XLinuxPodium.h"
// #include "XGLSLCompile.h"
// #include "XEGLIntf.h"

// // ============================================================================
// // 常數定義
// // ============================================================================
// // 定義場景視窗的解析度 (Full HD)
// #define SCENE_WIDTH 1920
// #define SCENE_HEIGHT 900

// using std::string;
// using std::vector;

// // ============================================================================
// // 工具類別：計時器 (Timer)
// // ============================================================================
// /**
//  * 用於效能分析的計時器類別。
//  * 採用 RAII (Resource Acquisition Is Initialization) 模式：
//  * - 建構子 (Constructor) 記錄開始時間。
//  * - 解構子 (Destructor) 計算並印出執行時間。
//  * 適合用來包在 { }區塊中，測量該區塊的耗時。
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
//         // 將時間差轉換為微秒 (microseconds) 後除以 1000 轉為毫秒
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//         printf("[%s] 耗時: %.3f ms\n", name, duration.count() / 1000.0);
//     }
    
//     // 手動獲取經過時間 (毫秒)，不等待解構
//     double getElapsedMs() {
//         auto end_time = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//         return duration.count() / 1000.0;
//     }
// };

// // ============================================================================
// // BMP 檔案格式結構定義
// // ============================================================================
// // #pragma pack(push, 1) 用於告訴編譯器：
// // 結構體內的成員必須緊密排列 (以 1 byte 對齊)，不要為了記憶體存取效能自動補零 (Padding)。
// // 因為 BMP 檔案標頭是連續的二進位資料，如果有 Padding 會導致讀取錯誤。
// #pragma pack(push, 1)

// // BMP 檔案標頭 (File Header) - 固定 14 bytes
// typedef struct {
//     uint16_t type;        // 識別碼，必須是 'BM' (0x4D42)
//     uint32_t size;        // 整個檔案的大小
//     uint16_t reserved1;   // 保留
//     uint16_t reserved2;   // 保留
//     uint32_t offset;      // 點陣圖資料 (Pixel Data) 開始的位元組偏移量
// } BMPFileHeader;

// // BMP 資訊標頭 (Info Header) - Windows V3 格式，40 bytes
// typedef struct {
//     uint32_t size;              // 結構體大小
//     int32_t width;              // 圖像寬度
//     int32_t height;             // 圖像高度 (正值: 倒立儲存, 負值: 正向儲存)
//     uint16_t planes;            // 平面數 (Must be 1)
//     uint16_t bits;              // 色深 (Bit Depth)，本程式只處理 24-bit
//     uint32_t compression;       // 壓縮方式
//     uint32_t imagesize;         // 影像資料大小
//     int32_t xresolution;        // 水平解析度
//     int32_t yresolution;        // 垂直解析度
//     uint32_t ncolours;          // 調色盤顏色數
//     uint32_t importantcolours;  // 重要顏色數
// } BMPInfoHeader;

// #pragma pack(pop)  // 恢復預設的記憶體對齊設定

// // ============================================================================
// // 全域變數
// // ============================================================================
// string resourceDirectory = "Supportingfiles/";

// // OpenGL Shader 程式相關 Handle
// GLuint programID;             // Shader Program 物件
// GLint iLocPosition = -1;      // 頂點屬性位置 (aPosition)
// GLint iLocTexCoord = -1;      // 紋理屬性位置 (aTexCoord)

// // Uniform Locations: 用於從 C++ 傳送數據到 Shader
// GLint iLocInputTexture = -1;                  // 原始影像紋理單元索引
// GLint iLocControlPoint[4] = {-1, -1, -1, -1}; // 4個控制點紋理單元索引
// GLint iLocFixedX = -1;                        // X 軸分割點座標陣列

// // OpenGL Texture Objects (紋理物件 ID)
// GLuint inputTextureID;           // 輸入影像
// GLuint controlPointTextureID[4]; // 4張控制圖 (Spatial Correction Maps)

// // 圖片尺寸 (全域記錄，假設所有圖片尺寸相同)
// int imageWidth = 0;
// int imageHeight = 0;

// // 定義 X 軸的 4 個固定節點 (標準化到 0.0 ~ 1.0)
// // 這些點將灰階值 (0~255) 分割成不同區間進行插值
// const float FIXED_X[4] = {
//     64.0f/255.0f,
//     95.0f/255.0f,
//     128.0f/255.0f,
//     156.0f/255.0f
// };

// // 全螢幕四邊形 (Full Screen Quad) 的頂點資料
// // 使用 Triangle Strip 繪製兩個三角形組成一個矩形
// // 座標系: Normalized Device Coordinates (NDC), 範圍 [-1, 1]
// const GLfloat vertexVertices[] = {
//     -1.0f, -1.0f,  // 左下
//      1.0f, -1.0f,  // 右下
//     -1.0f,  1.0f,  // 左上
//      1.0f,  1.0f   // 右上
// };

// // 對應的紋理座標 (UV)
// // 座標系: UV Space, 範圍 [0, 1], 原點通常在左下 (OpenGL 標準)
// const GLfloat textureVertices[] = {
//     0.0f, 0.0f,  // 左下
//     1.0f, 0.0f,  // 右下
//     0.0f, 1.0f,  // 左上
//     1.0f, 1.0f   // 右上
// };

// // ============================================================================
// // 函數：載入 BMP 圖片 (核心 I/O)
// // ============================================================================
// bool loadBMP(const char* filename, vector<unsigned char>& data, int& width, int& height) {
//     FILE* file = fopen(filename, "rb");
//     if (!file) {
//         printf("錯誤: 無法開啟檔案 %s\n", filename);
//         return false;
//     }
    
//     BMPFileHeader fileHeader;
//     BMPInfoHeader infoHeader;
    
//     // 讀取標頭
//     if (fread(&fileHeader, sizeof(BMPFileHeader), 1, file) != 1 ||
//         fread(&infoHeader, sizeof(BMPInfoHeader), 1, file) != 1) {
//         printf("錯誤: 讀取 BMP 標頭失敗 %s\n", filename);
//         fclose(file);
//         return false;
//     }
    
//     // 驗證 BMP 格式
//     if (fileHeader.type != 0x4D42) {
//         printf("錯誤: 不是有效的 BMP 檔案 %s\n", filename);
//         fclose(file);
//         return false;
//     }
     
//     // 檢查色深 (本範例不支援 8-bit 調色盤或 32-bit Alpha)
//     if (infoHeader.bits != 24) {
//         printf("錯誤: 僅支援 24-bit BMP %s\n", filename);
//         fclose(file);
//         return false;
//     }
    
//     width = infoHeader.width;
//     height = std::abs(infoHeader.height); // 取絕對值處理高度
    
//     // 計算 Row Size (Stride): BMP 規定每一行的 bytes 數必須是 4 的倍數
//     // 公式說明:
//     // (width * 3) 是實際像素佔用的 bytes (RGB各1 byte)
//     // +3 然後 /4 再 *4 是一種向上取整到 4 的倍數的技巧
//     int rowSize = ((width * 3 + 3) / 4) * 4;
//     int imageSize = rowSize * height;
    
//     vector<unsigned char> rawData(imageSize);
//     fseek(file, fileHeader.offset, SEEK_SET); // 移動檔案指標到像素資料開頭
//     fread(&rawData[0], 1, imageSize, file);   // 一次性讀取所有像素資料
//     fclose(file);
    
//     // 格式轉換: BMP (BGR + Padding) -> OpenGL (RGB + Packed)
//     data.resize(width * height * 3);
//     for (int y = 0; y < height; y++) {
//         for (int x = 0; x < width; x++) {
//             // 計算來源索引 (包含 Padding)
//             int srcIdx = y * rowSize + x * 3;
//             // 計算目標索引 (緊密排列，無 Padding)
//             int dstIdx = y * width * 3 + x * 3;
            
//             // BMP 像素順序是 BGR，OpenGL 需要 RGB，因此需要交換
//             data[dstIdx]     = rawData[srcIdx + 2]; // R from BGR's 3rd byte
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
// // 負責處理幾何頂點，這裡只做簡單的 Pass-through
// const char* vertexShaderSource = R"(
// attribute vec2 aPosition;  // 從 C++ 傳入的頂點座標
// attribute vec2 aTexCoord;  // 從 C++ 傳入的紋理座標
// varying vec2 vTexCoord;    // 輸出給 Fragment Shader 的紋理座標 (會自動插值)

// void main() {
//     // 設定裁剪空間座標 (Clip Space Coordinates)
//     gl_Position = vec4(aPosition, 0.0, 1.0);
    
//     // 將紋理座標原樣傳遞
//     vTexCoord = aTexCoord;
// }
// )";

// // ============================================================================
// // Fragment Shader (片段著色器) - 核心演算法
// // ============================================================================
// // 負責計算每個像素的最終顏色。
// // 演算法邏輯：空間變異的色彩校正 (Spatially Varying Color Correction)
// // 1. 讀取原始影像的顏色 (inputColor)
// // 2. 在相同位置讀取 4 張控制圖 (Control Points)，代表在不同亮度等級下的校正目標值。
// // 3. 根據 inputColor 的亮度，在這些控制點之間進行線性插值，算出最終顏色。
// const char* fragmentShaderSource = R"(
// precision highp float;      // 宣告浮點數精度，避免手機 GPU 上精度不足
// varying vec2 vTexCoord;     // 接收 Vertex Shader 插值後的座標

// uniform sampler2D uInputTexture;    // 原始影像

// // 5張控制點紋理 (Lookup Tables / Correction Maps)
// // 每張圖代表當輸入亮度為 uFixedX[i] 時，應該輸出的亮度值 (或顏色)
// uniform sampler2D uControlPoint0;   
// uniform sampler2D uControlPoint1;   
// uniform sampler2D uControlPoint2;   
// uniform sampler2D uControlPoint3; 

// // X軸的分段點 (Input Level)
// uniform float uFixedX[4]; 

// // ----------------------------------------------------------------------------
// // 函數: 分段線性插值 (Piecewise Linear Interpolation)
// // 輸入: x (原始亮度), y0~y4 (該像素在不同亮度級距下的校正目標值)
// // 輸出: 校正後的亮度
// // ----------------------------------------------------------------------------
// float interpolate(float x, float y0, float y1, float y2, float y3) {
//     float x0 = uFixedX[0]; // 64
//     float x1 = uFixedX[1]; // 95
//     float x2 = uFixedX[2]; // 128
//     float x3 = uFixedX[3]; // 156

//     float x_low, x_high, y_low, y_high;

//     // Logic for 4 Points (5 Intervals)
//     if (x < x0) {
//         // [Interval 1] 0 to P0 (0 ~ 64)
//         x_low = 0.0;  y_low = 0.0; 
//         x_high = x0;  y_high = y0;
//     } else if (x < x1) {
//         // [Interval 2] P0 to P1 (64 ~ 95)
//         x_low = x0;   y_low = y0;
//         x_high = x1;  y_high = y1;
//     } else if (x < x2) {
//         // [Interval 3] P1 to P2 (95 ~ 128)
//         x_low = x1;   y_low = y1;
//         x_high = x2;  y_high = y2;
//     } else if (x < x3) {
//         // [Interval 4] P2 to P3 (128 ~ 156)
//         x_low = x2;   y_low = y2;
//         x_high = x3;  y_high = y3;
//     } else {
//         // [Interval 5] P3 to 255 (156 ~ 255)
//         x_low = x3;   y_low = y3;
//         x_high = 1.0; y_high = 1.0;
//     }

//     float denominator = x_high - x_low;
//     if (denominator == 0.0) return y_high;

//     float m = (y_high - y_low) / denominator;
//     float y = y_low + m * (x - x_low);

//     return (y < 0.0) ? 0.0 : ((y > 1.0) ? 1.0 : y);
// }

// void main() {
//     vec3 inputColor = texture2D(uInputTexture, vTexCoord).rgb;

//     // Sample only 4 control maps
//     float r0 = texture2D(uControlPoint0, vTexCoord).r;
//     float r1 = texture2D(uControlPoint1, vTexCoord).r;
//     float r2 = texture2D(uControlPoint2, vTexCoord).r;
//     float r3 = texture2D(uControlPoint3, vTexCoord).r;

//     float g0 = texture2D(uControlPoint0, vTexCoord).g;
//     float g1 = texture2D(uControlPoint1, vTexCoord).g;
//     float g2 = texture2D(uControlPoint2, vTexCoord).g;
//     float g3 = texture2D(uControlPoint3, vTexCoord).g;

//     float b0 = texture2D(uControlPoint0, vTexCoord).b;
//     float b1 = texture2D(uControlPoint1, vTexCoord).b;
//     float b2 = texture2D(uControlPoint2, vTexCoord).b;
//     float b3 = texture2D(uControlPoint3, vTexCoord).b;

//     // Call interpolate with 4 points
//     float newR = interpolate(inputColor.r, r0, r1, r2, r3);
//     float newG = interpolate(inputColor.g, g0, g1, g2, g3);
//     float newB = interpolate(inputColor.b, b0, b1, b2, b3);

//     gl_FragColor = vec4(newR, newG, newB, 1.0);
// }
// )";

// // ============================================================================
// // 函數：編譯 Shader
// // ============================================================================
// GLuint compileShader(GLenum type, const char* source) {
//     GLuint shader = glCreateShader(type); // 建立 Shader 物件
//     glShaderSource(shader, 1, &source, NULL); // 載入原始碼
//     glCompileShader(shader); // 編譯
    
//     // 檢查編譯狀態
//     GLint compiled;
//     glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
//     if (!compiled) {
//         // 若失敗，取出錯誤訊息 Log
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
// bool prepareGraphics(const char* inputFile, const char* controlFiles[4]) {
//     printf("正在初始化圖形資源 (解析度: %dx%d)...\n", SCENE_WIDTH, SCENE_HEIGHT);
    
//     // 1. 載入原始 BMP 圖片到記憶體
//     vector<unsigned char> inputData;
//     if (!loadBMP(inputFile, inputData, imageWidth, imageHeight)) {
//         return false;
//     }
    
//     // 2. 載入 4 張控制點 BMP 圖片
//     vector<unsigned char> controlData[4];
//     for (int i = 0; i < 4; i++) {
//         int w, h;
//         if (!loadBMP(controlFiles[i], controlData[i], w, h)) {
//             return false;
//         }

//         // 確保控制圖尺寸與原圖一致 (這是 Pixel-to-Pixel 校正的前提)
//         if (w != imageWidth || h != imageHeight) {
//             printf("錯誤: 控制點圖片尺寸 (%dx%d) 與原圖不符\n", w, h);
//             return false;
//         }
//     }
    
//     // 3. 編譯 Vertex 和 Fragment Shaders
//     GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
//     GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
//     if (vertShader == 0 || fragShader == 0) return false;
    
//     // 4. 建立 Program 並連結 Shaders
//     programID = glCreateProgram();
//     glAttachShader(programID, vertShader);
//     glAttachShader(programID, fragShader);
//     glLinkProgram(programID);
//     glUseProgram(programID); // 啟動此 Program
    
//     // 5. 獲取 Shader 變數的位置 (Location)
//     // Attribute: 頂點資料
//     iLocPosition = glGetAttribLocation(programID, "aPosition");
//     iLocTexCoord = glGetAttribLocation(programID, "aTexCoord");
    
//     // Uniform: 全域參數
//     iLocInputTexture = glGetUniformLocation(programID, "uInputTexture");
//     iLocControlPoint[0] = glGetUniformLocation(programID, "uControlPoint0");
//     iLocControlPoint[1] = glGetUniformLocation(programID, "uControlPoint1");
//     iLocControlPoint[2] = glGetUniformLocation(programID, "uControlPoint2");
//     iLocControlPoint[3] = glGetUniformLocation(programID, "uControlPoint3");
//     iLocFixedX = glGetUniformLocation(programID, "uFixedX");
    
//     // 6. 設定 VBO (啟用頂點屬性陣列)
//     // 傳送頂點位置
//     glEnableVertexAttribArray(iLocPosition);
//     glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    
//     // 傳送紋理座標
//     glEnableVertexAttribArray(iLocTexCoord);
//     glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);
    
//     // 7. 建立並上傳輸入紋理到 GPU
//     glGenTextures(1, &inputTextureID);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     // 上傳像素資料
//     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &inputData[0]);
    
//     // 設定紋理採樣參數: Nearest Neighbor (最鄰近插值)，因為我們要做精確的像素對應
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
//     // 8. 建立並上傳 4 個控制點紋理到 GPU
//     for (int i = 0; i < 4; i++) {
//         glGenTextures(1, &controlPointTextureID[i]);
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imageWidth, imageHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, &controlData[i][0]);
        
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//     }
    
//     // 9. 基本 OpenGL 狀態設定
//     glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // 設定背景清除色為黑色
//     glDisable(GL_DEPTH_TEST);             // 2D 繪圖通常不需要深度測試
    
//     printf("OpenGL 初始化完成。\n");
//     return true;
// }

// // ============================================================================
// // 函數：圖形渲染迴圈 (每幀呼叫)
// // ============================================================================
// void GraphicsUpdate() {
//     // 清除畫面緩衝區
//     glClear(GL_COLOR_BUFFER_BIT);
    
//     // 設定視埠 (Viewport) 大小，填滿整個視窗
//     glViewport(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    
//     // 1. 綁定紋理到對應的 Texture Unit (多重紋理)
//     // OpenGL 是一個狀態機，我們需要先「啟動」一個插槽 (Unit)，然後「綁定」紋理
    
//     // Unit 0: 原始影像
//     glActiveTexture(GL_TEXTURE0);
//     glBindTexture(GL_TEXTURE_2D, inputTextureID);
//     glUniform1i(iLocInputTexture, 0); // 告訴 Shader uInputTexture 對應 Unit 0
    
//     // Unit 1~4: 控制點影像
//     for (int i = 0; i < 4; i++) {
//         glActiveTexture(GL_TEXTURE1 + i); // 依序啟動 Texture Unit 1, 2, 3...
//         glBindTexture(GL_TEXTURE_2D, controlPointTextureID[i]);
//         glUniform1i(iLocControlPoint[i], 1 + i); // 告訴 Shader 對應 Unit 1~4
//     }
    
//     // 2. 更新 Uniform 變數 (X軸分段點)
//     glUniform1fv(iLocFixedX, 4, (GLfloat*)FIXED_X);
    
//     // 3. 發出繪圖指令
//     // GL_TRIANGLE_STRIP: 使用 4 個頂點繪製矩形 (兩個三角形)
//     // 這會觸發 GPU 的 Rendering Pipeline
//     glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); 
// }

// // ============================================================================
// // 主程式 (Entry Point)
// // ============================================================================
// int main(int argc, char* argv[]) {
//     // ... (保留原本的參數檢查與初始化程式碼) ...
//     if (argc != 6) {
//         printf("使用方法: %s <輸入BMP> <點1> <點2> <點3> <點4>\n", argv[0]);
//         return 1;
//     }
//     const char* controlFiles[4] = {argv[2], argv[3], argv[4], argv[5]};

//     // 1. 系統與 OpenGL 初始化
//     XPodium *podium = XPodium::getHandler();
//     podium->prepareWindow(SCENE_WIDTH, SCENE_HEIGHT);
//     CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
//     eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);

//     // *** 關鍵設定：控制 VSync ***
//     // 設為 1: 開啟 VSync (鎖定 60FPS)，總時間會包含等待時間
//     // 設為 0: 關閉 VSync，總時間即為真實運算極限
//     eglSwapInterval(CoreEGL::display, 1);

//     if (!prepareGraphics(argv[1], controlFiles)) return 1;

//     printf("\n--- 開始效能測量 ---\n");
    
//     bool end = false;
//     int frame_count = 0;

//     while (!end) {
//         if (podium->checkWindow() != XPodium::WINDOW_IDLE) end = true;

//         // ============================================================
//         // 效能測量開始
//         // ============================================================
        
//         // 1. 紀錄 [總幀時間] 的起點
//         auto start_total = std::chrono::high_resolution_clock::now();

//         // 為了測量純 GPU 運算，我們先確保 GPU 把上一幀的事情做完，清空管線
//         // (在正式產品中不要這樣寫，這會降低 throughput，但為了測量 latency 必須這樣做)
//         glFinish(); 

//         // 2. 紀錄 [GPU 純運算] 的起點
//         auto start_gpu = std::chrono::high_resolution_clock::now();

//         // --- 執行繪圖指令 ---
//         GraphicsUpdate(); 

//         // 強制 CPU 等待 GPU 畫完所有像素
//         // 這樣 end_gpu 才會是真正畫完的時間點
//         glFinish(); 

//         // 3. 紀錄 [GPU 純運算] 的終點
//         auto end_gpu = std::chrono::high_resolution_clock::now();

//         // --- 交換緩衝區 (顯示到螢幕) ---
//         // 如果 VSync 開啟，CPU 會在這裡停下來等待螢幕刷新 (16ms 的主要來源)
//         eglSwapBuffers(CoreEGL::display, CoreEGL::surface);

//         // 4. 紀錄 [總幀時間] 的終點
//         auto end_total = std::chrono::high_resolution_clock::now();

//         // ============================================================
//         // 計算與輸出
//         // ============================================================
//         double gpu_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_gpu - start_gpu).count() / 1000.0;
//         double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_total - start_total).count() / 1000.0;
        
//         // 計算 "其他等待時間" (包含 VSync 等待、驅動程式 Overhead、Context 切換)
//         double wait_ms = total_ms - gpu_ms;

//         frame_count++;
        
//         // 為了避免洗版，每 60 幀印出一次平均值，或單幀印出
//         // 這裡示範單幀印出
//         printf("Frame %d | GPU: %6.3f ms | Total: %6.3f ms | Idle: %6.3f ms\n", 
//                frame_count, gpu_ms, total_ms, wait_ms);
//     }

//     // ... (保留原本的資源釋放程式碼) ...
//     glDeleteTextures(1, &inputTextureID);
//     // ...
//     return 0;
// }

// 5 points
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