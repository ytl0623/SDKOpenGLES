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

// ============================================================================
// BMP 工具函數
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
            int dst = (height - 1 - y) * width * 3 + x * 3; 
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
        int srcIndex = (height - 1 - y) * width * 3;
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
// CUDA 錯誤檢查
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
// Device Function
// ============================================================================
__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    const float x0 = 32.0f/255.0f;
    const float x1 = 64.0f/255.0f;
    const float x2 = 128.0f/255.0f;
    const float x3 = 192.0f/255.0f;
    const float x4 = 224.0f/255.0f;

    float x_low, x_high, y_low, y_high;

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

    return fminf(fmaxf(y, 0.0f), 1.0f);
}

// ============================================================================
// Kernel
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
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int idx = (y * width + x) * 3;

    float inR = input[idx]     / 255.0f;
    float inG = input[idx + 1] / 255.0f;
    float inB = input[idx + 2] / 255.0f;

    float r[5], g[5], b[5];
    const unsigned char* cps[5] = {cp0, cp1, cp2, cp3, cp4};
    
    #pragma unroll
    for(int i=0; i<5; i++) {
        r[i] = cps[i][idx]     / 255.0f;
        g[i] = cps[i][idx + 1] / 255.0f;
        b[i] = cps[i][idx + 2] / 255.0f;
    }

    float outR = interpolate(inR, r[0], r[1], r[2], r[3], r[4]);
    float outG = interpolate(inG, g[0], g[1], g[2], g[3], g[4]);
    float outB = interpolate(inB, b[0], b[1], b[2], b[3], b[4]);

    output[idx]     = (unsigned char)(outR * 255.0f);
    output[idx + 1] = (unsigned char)(outG * 255.0f);
    output[idx + 2] = (unsigned char)(outB * 255.0f);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <input.bmp> <cp1> <cp2> <cp3> <cp4> <cp5>\n", argv[0]);
        return 1;
    }

    int width, height;
    std::vector<unsigned char> h_input, h_cp[5];

    // ========================================================
    // C. 測量硬碟讀取時間 (Disk I/O)
    // ========================================================
    printf("Loading Images...\n");
    auto start_disk = std::chrono::high_resolution_clock::now();

    if (!loadBMP(argv[1], h_input, width, height)) return 1;
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) {
            printf("Error loading control point %d or size mismatch\n", i);
            return 1;
        }
    }

    auto end_disk = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> disk_duration = end_disk - start_disk;

    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    std::vector<unsigned char> h_output(imgSize);

    // 記憶體分配
    unsigned char *d_input, *d_output, *d_cp[5];
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));
    for(int i=0; i<5; i++) checkCuda(cudaMalloc(&d_cp[i], imgSize));

    // ========================================================
    // 準備 CUDA 計時器
    // ========================================================
    cudaEvent_t startH2D, stopH2D;       // B. PCIe Upload
    cudaEvent_t startKernel, stopKernel; // A. Kernel
    cudaEvent_t startD2H, stopD2H;       // B. PCIe Download
    
    checkCuda(cudaEventCreate(&startH2D)); checkCuda(cudaEventCreate(&stopH2D));
    checkCuda(cudaEventCreate(&startKernel)); checkCuda(cudaEventCreate(&stopKernel));
    checkCuda(cudaEventCreate(&startD2H)); checkCuda(cudaEventCreate(&stopD2H));

    printf("Processing on GPU (%dx%d)...\n", width, height);

    // ========================================================
    // B1. 測量 PCIe Host -> Device 時間 (H2D)
    // ========================================================
    checkCuda(cudaEventRecord(startH2D));

    // 複製 Input + 5 張 CP 圖
    checkCuda(cudaMemcpy(d_input, h_input.data(), imgSize, cudaMemcpyHostToDevice));
    for(int i=0; i<5; i++) {
        checkCuda(cudaMemcpy(d_cp[i], h_cp[i].data(), imgSize, cudaMemcpyHostToDevice));
    }
    
    checkCuda(cudaEventRecord(stopH2D));

    // 設定維度
    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, 
                  (height + blockSize.y - 1) / blockSize.y);

    // ========================================================
    // A. 測量 Kernel 運算時間
    // ========================================================
    checkCuda(cudaEventRecord(startKernel));

    demuraKernel<<<gridSize, blockSize>>>(
        d_input, d_cp[0], d_cp[1], d_cp[2], d_cp[3], d_cp[4], 
        d_output, width, height
    );
    
    checkCuda(cudaEventRecord(stopKernel));

    // ========================================================
    // B2. 測量 PCIe Device -> Host 時間 (D2H)
    // ========================================================
    checkCuda(cudaEventRecord(startD2H));

    checkCuda(cudaMemcpy(h_output.data(), d_output, imgSize, cudaMemcpyDeviceToHost));

    checkCuda(cudaEventRecord(stopD2H));
    
    // 同步等待全部完成
    checkCuda(cudaEventSynchronize(stopD2H));

    // ========================================================
    // 計算與輸出報告
    // ========================================================
    float msH2D = 0, msKernel = 0, msD2H = 0;
    cudaEventElapsedTime(&msH2D, startH2D, stopH2D);
    cudaEventElapsedTime(&msKernel, startKernel, stopKernel);
    cudaEventElapsedTime(&msD2H, startD2H, stopD2H);

    printf("\n==================================================\n");
    printf(" [效能瓶頸分析報告] (RTX 2070)\n");
    printf("==================================================\n");
    printf(" C. 硬碟讀取 (Disk I/O):       %8.3f ms  <-- [最慢] CPU\n", disk_duration.count());
    printf("--------------------------------------------------\n");
    printf(" B. PCIe 傳輸總計:             %8.3f ms  <-- [次慢] Bandwidth Bound\n", msH2D + msD2H);
    printf("    - H2D (上傳 6 張圖):        %8.3f ms\n", msH2D);
    printf("    - D2H (下載 1 張圖):        %8.3f ms\n", msD2H);
    printf("--------------------------------------------------\n");
    printf(" A. Kernel 運算 (GPU):         %8.3f ms  <-- [極快] Compute Bound\n", msKernel);
    printf("==================================================\n");
    printf(" GPU 總流程 (傳輸+運算):       %8.3f ms\n", msH2D + msKernel + msD2H);
    printf("==================================================\n");

    saveBMP("output_cuda.bmp", h_output.data(), width, height);

    // 釋放資源
    cudaFree(d_input); cudaFree(d_output);
    for(int i=0; i<5; i++) cudaFree(d_cp[i]);
    cudaEventDestroy(startH2D); cudaEventDestroy(stopH2D);
    cudaEventDestroy(startKernel); cudaEventDestroy(stopKernel);
    cudaEventDestroy(startD2H); cudaEventDestroy(stopD2H);
    
    return 0;
}