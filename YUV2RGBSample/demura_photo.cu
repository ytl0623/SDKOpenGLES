// nvcc -O3 demura_photo.cu -o demura_photo
// ./demura_photo Supportingfiles/test/red_192.bmp Supportingfiles/1216/Correction-Red-32.bmp Supportingfiles/1216/Correction-Red-64.bmp Supportingfiles/1216/Correction-Red-128.bmp Supportingfiles/1216/Correction-Red-192.bmp Supportingfiles/1216/Correction-Red-224.bmp

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <chrono> // 用於計算硬碟讀取時間

// ... (BMP 結構與工具函數 loadBMP, saveBMP 保持不變) ...
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
    if(fread(raw.data(), 1, raw.size(), file) != raw.size()) {} // 簡單檢查
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

// ============================================================================
// Texture 建立工具函數
// ============================================================================
void createTextureForImage(const std::vector<unsigned char>& hostData, int width, int height, cudaTextureObject_t* texObj, cudaArray_t* cuArray) {
    cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned);
    checkCuda(cudaMallocArray(cuArray, &channelDesc, width * 3, height));
    checkCuda(cudaMemcpy2DToArray(*cuArray, 0, 0, hostData.data(), width * 3 * sizeof(unsigned char), width * 3 * sizeof(unsigned char), height, cudaMemcpyHostToDevice));

    struct cudaResourceDesc resDesc;
    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = *cuArray;

    struct cudaTextureDesc texDesc;
    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeNormalizedFloat;
    texDesc.normalizedCoords = 0;

    checkCuda(cudaCreateTextureObject(texObj, &resDesc, &texDesc, NULL));
}

// ============================================================================
// Device Function
// ============================================================================
__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    const float x0 = 32.0f/255.0f; const float x1 = 64.0f/255.0f; 
    const float x2 = 128.0f/255.0f; const float x3 = 192.0f/255.0f; const float x4 = 224.0f/255.0f;

    float x_low, x_high, y_low, y_high;

    if (x < x0) { x_low = 0.0f; x_high = x0; y_low = 0.0f; y_high = y0; } 
    else if (x < x1) { x_low = x0; x_high = x1; y_low = y0; y_high = y1; } 
    else if (x < x2) { x_low = x1; x_high = x2; y_low = y1; y_high = y2; } 
    else if (x < x3) { x_low = x2; x_high = x3; y_low = y2; y_high = y3; } 
    else if (x < x4) { x_low = x3; x_high = x4; y_low = y3; y_high = y4; } 
    else { x_low = x4; x_high = 1.0f; y_low = y4; y_high = 1.0f; }

    float denominator = x_high - x_low; 
    if (denominator == 0.0f) return y_high;
    float m = (y_high - y_low) / denominator; 
    return fminf(fmaxf(y_low + m * (x - x_low), 0.0f), 1.0f);
}

// ============================================================================
// Kernel
// ============================================================================
__global__ void demuraKernelTex(
    const unsigned char* __restrict__ input,
    cudaTextureObject_t texCP0,
    cudaTextureObject_t texCP1,
    cudaTextureObject_t texCP2,
    cudaTextureObject_t texCP3,
    cudaTextureObject_t texCP4,
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

    int texX = x * 3;
    float r[5], g[5], b[5];
    cudaTextureObject_t textures[5] = {texCP0, texCP1, texCP2, texCP3, texCP4};

    #pragma unroll
    for(int i=0; i<5; i++) {
        r[i] = tex2D<float>(textures[i], texX,     y); 
        g[i] = tex2D<float>(textures[i], texX + 1, y);
        b[i] = tex2D<float>(textures[i], texX + 2, y);
    }

    output[idx]     = (unsigned char)(interpolate(inR, r[0], r[1], r[2], r[3], r[4]) * 255.0f);
    output[idx + 1] = (unsigned char)(interpolate(inG, g[0], g[1], g[2], g[3], g[4]) * 255.0f);
    output[idx + 2] = (unsigned char)(interpolate(inB, b[0], b[1], b[2], b[3], b[4]) * 255.0f);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <input> <cp1>...<cp5>\n", argv[0]);
        return 1;
    }

    int width, height;
    std::vector<unsigned char> h_input, h_cp[5];

    // ========================================================
    // C. 測量硬碟讀取時間 (Disk I/O)
    // ========================================================
    printf("[Init] Loading Images...\n");
    auto start_disk = std::chrono::high_resolution_clock::now(); // 開始 CPU 計時

    if (!loadBMP(argv[1], h_input, width, height)) return 1;
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) return 1;
    }

    auto end_disk = std::chrono::high_resolution_clock::now(); // 結束 CPU 計時
    std::chrono::duration<double, std::milli> disk_duration = end_disk - start_disk;

    // 分配 Global Memory
    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    unsigned char *d_input, *d_output;
    std::vector<unsigned char> h_output(imgSize); // 修正：定義 Output 容器
    
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));

    // 建立 Texture Objects
    cudaArray_t cuArrayCP[5];
    cudaTextureObject_t texCP[5];
    
    printf("[Init] Creating Texture Objects for CPs...\n");
    for(int i=0; i<5; i++) {
        createTextureForImage(h_cp[i], width, height, &texCP[i], &cuArrayCP[i]);
    }

    // ========================================================
    // 準備計時器 (細分階段)
    // ========================================================
    cudaEvent_t startH2D, stopH2D;       // 上傳
    cudaEvent_t startKernel, stopKernel; // 運算
    cudaEvent_t startD2H, stopD2H;       // 下載
    
    checkCuda(cudaEventCreate(&startH2D)); checkCuda(cudaEventCreate(&stopH2D));
    checkCuda(cudaEventCreate(&startKernel)); checkCuda(cudaEventCreate(&stopKernel));
    checkCuda(cudaEventCreate(&startD2H)); checkCuda(cudaEventCreate(&stopD2H));

    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    int N_FRAMES = 100;
    printf("[Loop] Starting Texture-Optimized simulation (%d frames)...\n", N_FRAMES);

    // Warm up
    demuraKernelTex<<<gridSize, blockSize>>>(d_input, texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], d_output, width, height);
    checkCuda(cudaDeviceSynchronize());

    float totalH2D = 0.0f;
    float totalKernel = 0.0f;
    float totalD2H = 0.0f;

    for (int frame = 0; frame < N_FRAMES; frame++) {
        // 1. H2D (Host -> Device)
        checkCuda(cudaEventRecord(startH2D));
        checkCuda(cudaMemcpy(d_input, h_input.data(), imgSize, cudaMemcpyHostToDevice));
        checkCuda(cudaEventRecord(stopH2D));

        // 2. Kernel (Computation)
        checkCuda(cudaEventRecord(startKernel));
        demuraKernelTex<<<gridSize, blockSize>>>(
            d_input, 
            texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], 
            d_output, width, height
        );
        checkCuda(cudaEventRecord(stopKernel));

        // 3. D2H (Device -> Host)
        checkCuda(cudaEventRecord(startD2H));
        checkCuda(cudaMemcpy(h_output.data(), d_output, imgSize, cudaMemcpyDeviceToHost));
        checkCuda(cudaEventRecord(stopD2H));
        
        // 等待該幀完成以統計時間
        checkCuda(cudaEventSynchronize(stopD2H));

        float t_h2d, t_kernel, t_d2h;
        cudaEventElapsedTime(&t_h2d, startH2D, stopH2D);
        cudaEventElapsedTime(&t_kernel, startKernel, stopKernel);
        cudaEventElapsedTime(&t_d2h, startD2H, stopD2H);

        totalH2D += t_h2d;
        totalKernel += t_kernel;
        totalD2H += t_d2h;
    }

    float avgH2D = totalH2D / N_FRAMES;
    float avgKernel = totalKernel / N_FRAMES;
    float avgD2H = totalD2H / N_FRAMES;
    float avgTotalPCIe = avgH2D + avgD2H;
    float avgFrameTotal = avgTotalPCIe + avgKernel;

    printf("\n==================================================\n");
    printf(" [最終效能分析 Breakdown] (RTX 2070 + Texture)\n");
    printf("==================================================\n");
    printf(" C. 硬碟讀取 (Disk I/O):       %8.3f ms\n", disk_duration.count());
    printf("--------------------------------------------------\n");
    printf(" B. PCIe 傳輸總計 (Avg):       %8.3f ms\n", avgTotalPCIe);
    printf("    - H2D (Input Only):         %8.3f ms\n", avgH2D);
    printf("    - D2H (Output):             %8.3f ms\n", avgD2H);
    printf("--------------------------------------------------\n");
    printf(" A. Kernel 運算 (Avg):         %8.3f ms\n", avgKernel);
    printf("--------------------------------------------------\n");
    printf(" 平均每幀 GPU 流程總耗時:      %8.3f ms\n", avgFrameTotal);
    printf(" 預估 FPS:                     %8.1f FPS\n", 1000.0f / avgFrameTotal);
    printf("==================================================\n");

    saveBMP("output_cuda_tex.bmp", h_output.data(), width, height);

    // 釋放資源
    cudaFree(d_input); cudaFree(d_output);
    for(int i=0; i<5; i++) {
        checkCuda(cudaDestroyTextureObject(texCP[i]));
        checkCuda(cudaFreeArray(cuArrayCP[i]));
    }
    cudaEventDestroy(startH2D); cudaEventDestroy(stopH2D);
    cudaEventDestroy(startKernel); cudaEventDestroy(stopKernel);
    cudaEventDestroy(startD2H); cudaEventDestroy(stopD2H);

    return 0;
}