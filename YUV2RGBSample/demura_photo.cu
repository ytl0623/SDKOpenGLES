// nvcc -O3 demura_photo.cu -o demura_photo
// ./demura_photo Supportingfiles/test/red_192.bmp Supportingfiles/1216/Correction-Red-32.bmp Supportingfiles/1216/Correction-Red-64.bmp Supportingfiles/1216/Correction-Red-128.bmp Supportingfiles/1216/Correction-Red-192.bmp Supportingfiles/1216/Correction-Red-224.bmp

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <chrono>

// ... (BMP 結構與工具函數 loadBMP, saveBMP 保持不變，為節省篇幅省略，請直接貼上原有的) ...
// ... (checkCuda 巨集保持不變) ...
// ... (interpolate Device 函數保持不變) ...
// ... (demuraKernel 函數保持不變) ...

// ****************************************************************************
// 為了完整性，這裡需要您將原本的 BMP/Kernel 相關程式碼貼在這個位置
// ****************************************************************************
// (以下假設您已經保留了上面的 helper functions)

#pragma pack(push, 1)
typedef struct { uint16_t type; uint32_t size; uint16_t reserved1; uint16_t reserved2; uint32_t offset; } BMPFileHeader;
typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t compression; uint32_t imagesize; int32_t xresolution; int32_t yresolution; uint32_t ncolours; uint32_t importantcolours; } BMPInfoHeader;
#pragma pack(pop)

bool loadBMP(const char* filename, std::vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) { printf("無法開啟: %s\n", filename); return false; }
    BMPFileHeader fh; BMPInfoHeader ih;
    if (fread(&fh, sizeof(BMPFileHeader), 1, file) != 1 || fread(&ih, sizeof(BMPInfoHeader), 1, file) != 1) { fclose(file); return false; }
    width = ih.width; height = abs(ih.height);
    int rowSize = ((width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> raw(rowSize * height);
    fseek(file, fh.offset, SEEK_SET);
    fread(raw.data(), 1, raw.size(), file);
    fclose(file);
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src = y * rowSize + x * 3;
            int dst = (height - 1 - y) * width * 3 + x * 3; 
            data[dst] = raw[src + 2]; data[dst + 1] = raw[src + 1]; data[dst + 2] = raw[src];
        }
    }
    return true;
}

void saveBMP(const char* filename, const unsigned char* data, int width, int height) {
    FILE* file = fopen(filename, "wb"); if (!file) return;
    BMPFileHeader fh = {0x4D42, 0, 0, 0, 54}; BMPInfoHeader ih = {40, width, height, 1, 24, 0, 0, 0, 0, 0, 0};
    int rowSize = ((width * 3 + 3) / 4) * 4; fh.size = 54 + rowSize * height;
    fwrite(&fh, sizeof(fh), 1, file); fwrite(&ih, sizeof(ih), 1, file);
    std::vector<unsigned char> line(rowSize, 0);
    for (int y = 0; y < height; y++) {
        int srcIndex = (height - 1 - y) * width * 3;
        for (int x = 0; x < width; x++) {
            line[x * 3 + 2] = data[srcIndex + x * 3]; line[x * 3 + 1] = data[srcIndex + x * 3 + 1]; line[x * 3] = data[srcIndex + x * 3 + 2];
        }
        fwrite(line.data(), 1, rowSize, file);
    }
    fclose(file);
}

#define checkCuda(call) { const cudaError_t error = call; if (error != cudaSuccess) { printf("Error: %s:%d, code:%d\n", __FILE__, __LINE__, error); exit(1); } }

__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    const float x0 = 32.0f/255.0f; const float x1 = 64.0f/255.0f; const float x2 = 128.0f/255.0f; const float x3 = 192.0f/255.0f; const float x4 = 224.0f/255.0f;
    float x_low, x_high, y_low, y_high;
    if (x < x0) { x_low = 0.0f; x_high = x0; y_low = 0.0f; y_high = y0; } 
    else if (x < x1) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; } 
    else if (x < x2) { x_low = x1; x_high = x2; y_low = y1; y_high = y2; } 
    else if (x < x3) { x_low = x2; x_high = x3; y_low = y2; y_high = y3; } 
    else if (x < x4) { x_low = x3; x_high = x4; y_low = y3; y_high = y4; } 
    else { x_low = x4; x_high = 1.0f; y_low = y4; y_high = 1.0f; }
    float denominator = x_high - x_low; if (denominator == 0.0f) return y_high;
    float m = (y_high - y_low) / denominator; return fminf(fmaxf(y_low + m * (x - x_low), 0.0f), 1.0f);
}

__global__ void demuraKernel(const unsigned char* __restrict__ input, const unsigned char* __restrict__ cp0, const unsigned char* __restrict__ cp1, const unsigned char* __restrict__ cp2, const unsigned char* __restrict__ cp3, const unsigned char* __restrict__ cp4, unsigned char* __restrict__ output, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x; int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    int idx = (y * width + x) * 3;
    float inR = input[idx] / 255.0f; float inG = input[idx + 1] / 255.0f; float inB = input[idx + 2] / 255.0f;
    float r[5], g[5], b[5];
    const unsigned char* cps[5] = {cp0, cp1, cp2, cp3, cp4};
    #pragma unroll
    for(int i=0; i<5; i++) { r[i] = cps[i][idx] / 255.0f; g[i] = cps[i][idx + 1] / 255.0f; b[i] = cps[i][idx + 2] / 255.0f; }
    output[idx] = (unsigned char)(interpolate(inR, r[0], r[1], r[2], r[3], r[4]) * 255.0f);
    output[idx + 1] = (unsigned char)(interpolate(inG, g[0], g[1], g[2], g[3], g[4]) * 255.0f);
    output[idx + 2] = (unsigned char)(interpolate(inB, b[0], b[1], b[2], b[3], b[4]) * 255.0f);
}

// ============================================================================
// Main (已優化邏輯)
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <input> <cp1> <cp2> <cp3> <cp4> <cp5>\n", argv[0]);
        return 1;
    }

    int width, height;
    std::vector<unsigned char> h_input, h_cp[5];

    // 1. 初始化階段：硬碟讀取 (只做一次)
    printf("[Init] Loading Images from Disk...\n");
    if (!loadBMP(argv[1], h_input, width, height)) return 1;
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) return 1;
    }
    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    std::vector<unsigned char> h_output(imgSize);

    // 2. 初始化階段：GPU 記憶體配置與 CP 上傳 (只做一次！)
    unsigned char *d_input, *d_output, *d_cp[5];
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));
    for(int i=0; i<5; i++) {
        checkCuda(cudaMalloc(&d_cp[i], imgSize));
        // <--- 優化重點：在這裡就傳送 CP 圖，之後不再傳送 --->
        checkCuda(cudaMemcpy(d_cp[i], h_cp[i].data(), imgSize, cudaMemcpyHostToDevice));
    }
    printf("[Init] CP Images uploaded to GPU (Static Assets).\n");

    // 準備計時器
    cudaEvent_t startFrame, stopFrame;
    cudaEvent_t startH2D, stopH2D;
    checkCuda(cudaEventCreate(&startFrame)); checkCuda(cudaEventCreate(&stopFrame));
    checkCuda(cudaEventCreate(&startH2D));   checkCuda(cudaEventCreate(&stopH2D));

    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    // ========================================================
    // 3. 模擬即時處理迴圈 (Loop)
    // ========================================================
    int N_FRAMES = 100; // 模擬跑 100 幀
    printf("[Loop] Starting simulation for %d frames...\n", N_FRAMES);

    // 暖身 (Warm-up) - 讓 GPU 進入工作狀態
    demuraKernel<<<gridSize, blockSize>>>(d_input, d_cp[0], d_cp[1], d_cp[2], d_cp[3], d_cp[4], d_output, width, height);
    checkCuda(cudaDeviceSynchronize());

    // 總時間累計
    float totalTimeMs = 0.0f;
    float totalH2DMs = 0.0f;

    for (int frame = 0; frame < N_FRAMES; frame++) {
        // --- 每一幀開始 ---
        checkCuda(cudaEventRecord(startFrame));
        
        // A. 只有 Input 需要傳送 (CP 圖已經在 GPU 裡了)
        checkCuda(cudaEventRecord(startH2D));
        checkCuda(cudaMemcpy(d_input, h_input.data(), imgSize, cudaMemcpyHostToDevice));
        checkCuda(cudaEventRecord(stopH2D));

        // B. 執行 Kernel
        demuraKernel<<<gridSize, blockSize>>>(
            d_input, d_cp[0], d_cp[1], d_cp[2], d_cp[3], d_cp[4], 
            d_output, width, height
        );

        // C. 取回 Output
        checkCuda(cudaMemcpy(h_output.data(), d_output, imgSize, cudaMemcpyDeviceToHost));
        
        // --- 每一幀結束 ---
        checkCuda(cudaEventRecord(stopFrame));
        checkCuda(cudaEventSynchronize(stopFrame));

        float msFrame = 0, msH2D = 0;
        cudaEventElapsedTime(&msFrame, startFrame, stopFrame);
        cudaEventElapsedTime(&msH2D, startH2D, stopH2D);
        
        totalTimeMs += msFrame;
        totalH2DMs += msH2D;
    }

    // ========================================================
    // 輸出優化後報告
    // ========================================================
    float avgFrameTime = totalTimeMs / N_FRAMES;
    float avgH2DTime = totalH2DMs / N_FRAMES;
    
    // 理論推算 H2D (只傳 1 張圖: 5.93MB / 12GB/s ≈ 0.5ms)
    // 理論推算 Kernel (不變 ≈ 0.3ms)
    // 理論推算 D2H (傳回 1 張圖 ≈ 0.5ms)
    // 預期總時間 ≈ 1.3 ~ 1.5 ms

    printf("\n==================================================\n");
    printf(" [優化後效能報告] (RTX 2070 - Loop Avg)\n");
    printf("==================================================\n");
    printf(" 平均每幀總時間:               %8.3f ms\n", avgFrameTime);
    printf(" --------------------------------------------------\n");
    printf(" 效能細節拆解:\n");
    printf("  1. H2D (Input Only):         %8.3f ms  <-- 顯著降低\n", avgH2DTime);
    printf("  2. Kernel + D2H + Overhead:  %8.3f ms\n", avgFrameTime - avgH2DTime);
    printf("==================================================\n");
    printf(" 預估 FPS (不含硬碟讀取):      %8.1f FPS\n", 1000.0f / avgFrameTime);
    printf("==================================================\n");

    saveBMP("output_cuda_opt.bmp", h_output.data(), width, height);

    // 釋放資源
    cudaFree(d_input); cudaFree(d_output);
    for(int i=0; i<5; i++) cudaFree(d_cp[i]);
    cudaEventDestroy(startFrame); cudaEventDestroy(stopFrame);
    
    return 0;
}