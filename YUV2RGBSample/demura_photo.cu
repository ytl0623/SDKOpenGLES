// nvcc -O3 demura.cu -o demura_cuda
// ./demura_photo Supportingfiles/test/red_192.bmp Supportingfiles/1216/Correction-Red-32.bmp Supportingfiles/1216/Correction-Red-64.bmp Supportingfiles/1216/Correction-Red-128.bmp Supportingfiles/1216/Correction-Red-192.bmp Supportingfiles/1216/Correction-Red-224.bmp

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <cmath>
#include <iostream>

// ============================================================================
// BMP 工具函數 (保留原本邏輯，增加寫入功能)
// ============================================================================
#pragma pack(push, 1)
typedef struct {
    uint16_t type; uint32_t size; uint16_t reserved1; uint16_t reserved2; uint32_t offset;
} BMPFileHeader;

typedef struct {
    uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits;
    uint32_t compression; uint32_t imagesize; int32_t xresolution; int32_t yresolution;
    uint32_t ncolours; uint32_t importantcolours;
} BMPInfoHeader;
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
    // BGR -> RGB & Remove Padding
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src = y * rowSize + x * 3;
            int dst = (height - 1 - y) * width * 3 + x * 3; // 修正: BMP通常是倒立的，這裡翻轉以便處理
            data[dst] = raw[src + 2];     // R
            data[dst + 1] = raw[src + 1]; // G
            data[dst + 2] = raw[src];     // B
        }
    }
    return true;
}

void saveBMP(const char* filename, const unsigned char* data, int width, int height) {
    FILE* file = fopen(filename, "wb");
    if (!file) return;
    BMPFileHeader fh = {0x4D42, 0, 0, 0, 54};
    BMPInfoHeader ih = {40, width, height, 1, 24, 0, 0, 0, 0, 0, 0};
    int rowSize = ((width * 3 + 3) / 4) * 4;
    fh.size = 54 + rowSize * height;
    fwrite(&fh, sizeof(fh), 1, file);
    fwrite(&ih, sizeof(ih), 1, file);
    std::vector<unsigned char> line(rowSize, 0);
    for (int y = 0; y < height; y++) {
        int srcIndex = (height - 1 - y) * width * 3; // 寫入時翻轉回來
        for (int x = 0; x < width; x++) {
            line[x * 3 + 2] = data[srcIndex + x * 3];     // R
            line[x * 3 + 1] = data[srcIndex + x * 3 + 1]; // G
            line[x * 3]     = data[srcIndex + x * 3 + 2]; // B
        }
        fwrite(line.data(), 1, rowSize, file);
    }
    fclose(file);
    printf("已儲存結果: %s\n", filename);
}

// ============================================================================
// CUDA 錯誤檢查巨集
// ============================================================================
#define checkCuda(call) { \
    const cudaError_t error = call; \
    if (error != cudaSuccess) { \
        printf("Error: %s:%d, ", __FILE__, __LINE__); \
        printf("code:%d, reason: %s\n", error, cudaGetErrorString(error)); \
        exit(1); \
    } \
}

// ============================================================================
// CUDA Device 函數: 負責插值邏輯 (對應 Shader 中的 interpolate)
// ============================================================================
__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    // 定義固定節點 (0~1.0)
    const float x0 = 32.0f/255.0f;
    const float x1 = 64.0f/255.0f;
    const float x2 = 128.0f/255.0f;
    const float x3 = 192.0f/255.0f;
    const float x4 = 224.0f/255.0f;

    float x_low, x_high, y_low, y_high;

    // 判斷區間
    if (x < x0) {
        x_low = 0.0f; x_high = x0; y_low = 0.0f; y_high = y0;
    } else if (x < x1) {
        x_low = x0; x_high = x1; y_low = y0; y_high = y1;
    } else if (x < x2) {
        x_low = x1; x_high = x2; y_low = y1; y_high = y2;
    } else if (x < x3) {
        x_low = x2; x_high = x3; y_low = y2; y_high = y3;
    } else if (x < x4) {
        x_low = x3; x_high = x4; y_low = y3; y_high = y4;
    } else {
        x_low = x4; x_high = 1.0f; y_low = y4; y_high = 1.0f;
    }

    float denominator = x_high - x_low;
    if (denominator == 0.0f) return y_high;

    float m = (y_high - y_low) / denominator;
    float y = y_low + m * (x - x_low);

    // Clamp
    return fminf(fmaxf(y, 0.0f), 1.0f);
}

// ============================================================================
// CUDA Kernel: 核心並行運算 (對應 Fragment Shader main)
// ============================================================================
__global__ void demuraKernel(
    const unsigned char* __restrict__ input,
    const unsigned char* __restrict__ cp0,
    const unsigned char* __restrict__ cp1,
    const unsigned char* __restrict__ cp2,
    const unsigned char* __restrict__ cp3,
    const unsigned char* __restrict__ cp4,
    unsigned char* __restrict__ output,
    int width, int height
) {
    // 計算當前 Thread 對應的像素索引
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = (y * width + x) * 3; // RGB 3 channels

    // 1. 讀取並標準化 (0~255 -> 0.0~1.0)
    float inR = input[idx]     / 255.0f;
    float inG = input[idx + 1] / 255.0f;
    float inB = input[idx + 2] / 255.0f;

    // 2. 讀取控制點 (每個控制點對應的像素)
    float r[5], g[5], b[5];
    const unsigned char* cps[5] = {cp0, cp1, cp2, cp3, cp4};
    
    #pragma unroll
    for(int i=0; i<5; i++) {
        r[i] = cps[i][idx]     / 255.0f;
        g[i] = cps[i][idx + 1] / 255.0f;
        b[i] = cps[i][idx + 2] / 255.0f;
    }

    // 3. 計算插值
    float outR = interpolate(inR, r[0], r[1], r[2], r[3], r[4]);
    float outG = interpolate(inG, g[0], g[1], g[2], g[3], g[4]);
    float outB = interpolate(inB, b[0], b[1], b[2], b[3], b[4]);

    // 4. 寫回結果 (0.0~1.0 -> 0~255)
    output[idx]     = (unsigned char)(outR * 255.0f);
    output[idx + 1] = (unsigned char)(outG * 255.0f);
    output[idx + 2] = (unsigned char)(outB * 255.0f);
}

// ============================================================================
// Host Main
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <input.bmp> <cp1> <cp2> <cp3> <cp4> <cp5>\n", argv[0]);
        return 1;
    }

    int width, height;
    std::vector<unsigned char> h_input, h_cp[5];

    // 1. 載入圖片 (Disk I/O 不算在 GPU 效能內)
    printf("Loading Images...\n");
    if (!loadBMP(argv[1], h_input, width, height)) return 1;
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) {
            printf("Error loading control point %d or size mismatch\n", i);
            return 1;
        }
    }

    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    std::vector<unsigned char> h_output(imgSize);

    // 2. 分配 Device Memory (通常在程式初始化做一次，不計入單幀時間)
    unsigned char *d_input, *d_output, *d_cp[5];
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));
    for(int i=0; i<5; i++) checkCuda(cudaMalloc(&d_cp[i], imgSize));

    // ==========================================
    //  準備計時器
    // ==========================================
    cudaEvent_t startTotal, stopTotal;  // 計算總流程 (Copy + Kernel)
    cudaEvent_t startKernel, stopKernel; // 計算純核心 (Kernel Only)
    
    checkCuda(cudaEventCreate(&startTotal));
    checkCuda(cudaEventCreate(&stopTotal));
    checkCuda(cudaEventCreate(&startKernel));
    checkCuda(cudaEventCreate(&stopKernel));

    printf("Processing on GPU (%dx%d)...\n", width, height);

    // ==========================================
    //  開始測量：總流程時間 (Start Total)
    // ==========================================
    checkCuda(cudaEventRecord(startTotal));

    // A. 複製資料 Host -> Device (H2D)
    checkCuda(cudaMemcpy(d_input, h_input.data(), imgSize, cudaMemcpyHostToDevice));
    for(int i=0; i<5; i++) {
        checkCuda(cudaMemcpy(d_cp[i], h_cp[i].data(), imgSize, cudaMemcpyHostToDevice));
    }

    // 設定 Grid/Block
    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, 
                  (height + blockSize.y - 1) / blockSize.y);

    // ==========================================
    //  開始測量：Kernel 時間 (Start Kernel)
    // ==========================================
    checkCuda(cudaEventRecord(startKernel));

    // B. 啟動 Kernel
    demuraKernel<<<gridSize, blockSize>>>(
        d_input, d_cp[0], d_cp[1], d_cp[2], d_cp[3], d_cp[4], 
        d_output, width, height
    );
    
    // ==========================================
    //  停止測量：Kernel 時間 (Stop Kernel)
    // ==========================================
    checkCuda(cudaEventRecord(stopKernel));

    // C. 複製結果 Device -> Host (D2H)
    // 注意：cudaMemcpy 會隱式同步，但為了 event 計時準確，我們把它包在裡面
    checkCuda(cudaMemcpy(h_output.data(), d_output, imgSize, cudaMemcpyDeviceToHost));

    // ==========================================
    //  停止測量：總流程時間 (Stop Total)
    // ==========================================
    checkCuda(cudaEventRecord(stopTotal));
    
    // 等待 GPU 完成所有工作以便計算時間
    checkCuda(cudaEventSynchronize(stopTotal));

    // ==========================================
    //  計算並顯示結果
    // ==========================================
    float msKernel = 0, msTotal = 0;
    cudaEventElapsedTime(&msKernel, startKernel, stopKernel);
    cudaEventElapsedTime(&msTotal, startTotal, stopTotal);

    printf("--------------------------------------------------\n");
    printf(" [效能分析]\n");
    printf(" 1. 純運算時間 (Kernel Time):  %8.3f ms\n", msKernel);
    printf(" 2. 總執行時間 (Total Time):   %8.3f ms (含 PCIe 傳輸)\n", msTotal);
    printf(" --------------------------------------------------\n");
    printf(" 資料傳輸耗時 (Overhead):      %8.3f ms (佔比 %.1f%%)\n", 
           msTotal - msKernel, 
           ((msTotal - msKernel) / msTotal) * 100.0f);
    printf("--------------------------------------------------\n");

    // 6. 儲存結果
    saveBMP("output_cuda.bmp", h_output.data(), width, height);

    // 7. 釋放資源
    cudaFree(d_input);
    cudaFree(d_output);
    for(int i=0; i<5; i++) cudaFree(d_cp[i]);
    cudaEventDestroy(startTotal); cudaEventDestroy(stopTotal);
    cudaEventDestroy(startKernel); cudaEventDestroy(stopKernel);
    
    return 0;
}