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

// ... (BMP 讀寫函式 loadBMP, saveBMP 請保持原樣，此處省略以節省版面) ...
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

// ============================================================================
// 新增：Texture 建立工具函數
// 負責將 Host 資料搬移到 CUDA Array 並建立 Texture Object
// ============================================================================
void createTextureForImage(const std::vector<unsigned char>& hostData, int width, int height, cudaTextureObject_t* texObj, cudaArray_t* cuArray) {
    // 1. 分配 CUDA Array (針對紋理優化的記憶體區塊)
    cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned); // 單通道 8-bit
    // 註：因為我們要分開讀 RGB，這裡示範將整張圖視為單通道長條圖，或者需要對 Input 進行重排。
    // 為了最簡單的實現，並配合 demura 邏輯 (RGB 分開讀)，我們維持 RGB 3 byte 結構，但 Texture 讀取通常以 1, 2, 4 component 為主。
    // *最佳實務*：將 RGB 分離成 3 個 Plane (R圖, G圖, B圖) 或者使用 uchar4。
    // 但為了不大幅改動資料結構，我們這裡使用一個小技巧：
    // Texture 寬度設為 width * 3，視為單通道 (Grayscale) 讀取。
    
    checkCuda(cudaMallocArray(cuArray, &channelDesc, width * 3, height));

    // 2. 複製資料 Host -> CUDA Array
    checkCuda(cudaMemcpy2DToArray(*cuArray, 0, 0, hostData.data(), width * 3 * sizeof(unsigned char), width * 3 * sizeof(unsigned char), height, cudaMemcpyHostToDevice));

    // 3. 設定資源描述符
    struct cudaResourceDesc resDesc;
    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = *cuArray;

    // 4. 設定紋理描述符 (關鍵設定都在這)
    struct cudaTextureDesc texDesc;
    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0] = cudaAddressModeClamp; // 邊界：Clamp
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;      // 插值：點採樣 (我們需要精確的像素值，插值邏輯自己寫)
    texDesc.readMode = cudaReadModeNormalizedFloat; // <--- 關鍵！自動將 0~255 轉為 0.0~1.0
    texDesc.normalizedCoords = 0;                  // 使用像素座標 (0, 1, 2...) 而非 (0.0 ~ 1.0)

    // 5. 建立 Texture Object
    checkCuda(cudaCreateTextureObject(texObj, &resDesc, &texDesc, NULL));
}

// ============================================================================
// Device Function (邏輯不變，但輸入已是 normalized float)
// ============================================================================
__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    // 節點定義保持不變
    const float x0 = 32.0f/255.0f; const float x1 = 64.0f/255.0f; 
    const float x2 = 128.0f/255.0f; const float x3 = 192.0f/255.0f; const float x4 = 224.0f/255.0f;

    float x_low, x_high, y_low, y_high;

    // 分支邏輯 (Texture 只能做線性插值，無法做這種分段式的不均勻插值，所以這段數學保留)
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
// Kernel: 使用 Texture Object 讀取
// ============================================================================
__global__ void demuraKernelTex(
    const unsigned char* __restrict__ input, // Input 變動頻繁，維持 Global Memory
    cudaTextureObject_t texCP0,              // CP 圖改用 Texture Object
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

    // 1. 讀取 Input (Global Memory)
    int idx = (y * width + x) * 3;
    float inR = input[idx]     / 255.0f; // Input 仍需手動正規化
    float inG = input[idx + 1] / 255.0f;
    float inB = input[idx + 2] / 255.0f;

    // 2. 讀取 Control Points (Texture)
    // 技巧：因為我們建立 Texture 時寬度是 width*3，所以用 (x*3, y) 來存取 R, G, B
    // tex2D<float> 會自動回傳 0.0 ~ 1.0 的 float，省去除法！
    int texX = x * 3;
    
    float r[5], g[5], b[5];
    cudaTextureObject_t textures[5] = {texCP0, texCP1, texCP2, texCP3, texCP4};

    #pragma unroll
    for(int i=0; i<5; i++) {
        // Texture Cache 會在這裡發揮作用，且無須除以 255.0f
        r[i] = tex2D<float>(textures[i], texX,     y); 
        g[i] = tex2D<float>(textures[i], texX + 1, y);
        b[i] = tex2D<float>(textures[i], texX + 2, y);
    }

    // 3. 插值運算 (邏輯不變)
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

    printf("[Init] Loading Images...\n");
    if (!loadBMP(argv[1], h_input, width, height)) return 1;
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) return 1;
    }

    // 分配 Global Memory (Input/Output)
    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    unsigned char *d_input, *d_output;

    std::vector<unsigned char> h_output(imgSize);
    
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));

    // ==========================================
    // 建立 Texture Objects (只做一次)
    // ==========================================
    cudaArray_t cuArrayCP[5];
    cudaTextureObject_t texCP[5];
    
    printf("[Init] Creating Texture Objects for CPs...\n");
    for(int i=0; i<5; i++) {
        createTextureForImage(h_cp[i], width, height, &texCP[i], &cuArrayCP[i]);
    }

    // 準備計時器
    cudaEvent_t startFrame, stopFrame, startH2D, stopH2D;
    checkCuda(cudaEventCreate(&startFrame)); checkCuda(cudaEventCreate(&stopFrame));
    checkCuda(cudaEventCreate(&startH2D)); checkCuda(cudaEventCreate(&stopH2D));

    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    int N_FRAMES = 100;
    printf("[Loop] Starting Texture-Optimized simulation (%d frames)...\n", N_FRAMES);

    // Warm up
    demuraKernelTex<<<gridSize, blockSize>>>(d_input, texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], d_output, width, height);

    float totalTimeMs = 0.0f;

    for (int frame = 0; frame < N_FRAMES; frame++) {
        checkCuda(cudaEventRecord(startFrame));
        
        // H2D: 只有 Input 需要傳
        checkCuda(cudaMemcpy(d_input, h_input.data(), imgSize, cudaMemcpyHostToDevice));

        // Kernel: 傳入 Texture Objects
        demuraKernelTex<<<gridSize, blockSize>>>(
            d_input, 
            texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], 
            d_output, width, height
        );

        checkCuda(cudaMemcpy(h_output.data(), d_output, imgSize, cudaMemcpyDeviceToHost));
        
        checkCuda(cudaEventRecord(stopFrame));
        checkCuda(cudaEventSynchronize(stopFrame));

        float ms = 0;
        cudaEventElapsedTime(&ms, startFrame, stopFrame);
        totalTimeMs += ms;
    }

    printf("\n==================================================\n");
    printf(" [Texture Object 優化報告] (RTX 2070)\n");
    printf("==================================================\n");
    printf(" 平均每幀總時間:       %8.3f ms\n", totalTimeMs / N_FRAMES);
    printf(" 預估 FPS:             %8.1f FPS\n", 1000.0f / (totalTimeMs / N_FRAMES));
    printf("==================================================\n");

    saveBMP("output_cuda_tex.bmp", h_output.data(), width, height);

    // 釋放資源 (記得 Destroy Texture)
    cudaFree(d_input); cudaFree(d_output);
    for(int i=0; i<5; i++) {
        checkCuda(cudaDestroyTextureObject(texCP[i]));
        checkCuda(cudaFreeArray(cuArrayCP[i]));
    }
    cudaEventDestroy(startFrame); cudaEventDestroy(stopFrame); 
    cudaEventDestroy(startH2D); cudaEventDestroy(stopH2D);

    return 0;
}