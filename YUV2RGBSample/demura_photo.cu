// nvcc -O3 demura_photo.cu -o demura_photo
// ./demura_photo Supportingfiles/test/red_192.bmp Supportingfiles/1216/Correction-Red-32.bmp Supportingfiles/1216/Correction-Red-64.bmp Supportingfiles/1216/Correction-Red-128.bmp Supportingfiles/1216/Correction-Red-192.bmp Supportingfiles/1216/Correction-Red-224.bmp

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <cmath>
#include <iostream>
#include <chrono>

#pragma pack(push, 1)
typedef struct { uint16_t type; uint32_t size; uint16_t reserved1; uint16_t reserved2; uint32_t offset; } BMPFileHeader;
typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t compression; uint32_t imagesize; int32_t xresolution; int32_t yresolution; uint32_t ncolours; uint32_t importantcolours; } BMPInfoHeader;
#pragma pack(pop)

// [FIXED] Pinned Memory 版本的 loadBMP
bool loadBMPToPtr(const char* filename, unsigned char* buffer, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) { printf("無法開啟: %s\n", filename); return false; }
    
    BMPFileHeader fh; BMPInfoHeader ih;
    if (fread(&fh, sizeof(BMPFileHeader), 1, file) != 1 || fread(&ih, sizeof(BMPInfoHeader), 1, file) != 1) { 
        fclose(file); return false; 
    }
    
    width = ih.width; height = abs(ih.height);
    
    // [修正點]：如果 buffer 是 NULL，代表只是要讀取 header 取得寬高，直接返回
    if (buffer == NULL) {
        fclose(file);
        return true;
    }

    int rowSize = ((width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> raw(rowSize * height);
    
    fseek(file, fh.offset, SEEK_SET);
    if(fread(raw.data(), 1, raw.size(), file) != raw.size()) {}
    fclose(file);
    
    // BGR -> RGB
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src = y * rowSize + x * 3;
            int dst = (height - 1 - y) * width * 3 + x * 3; 
            buffer[dst] = raw[src + 2]; buffer[dst + 1] = raw[src + 1]; buffer[dst + 2] = raw[src];
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

// LUT Global Memory
__constant__ int c_lut_index[256];
__constant__ float c_lut_weight[256];

void initLUT() {
    int h_lut_index[256];
    float h_lut_weight[256];
    const float nodes[] = {0.0f, 32.0f, 64.0f, 128.0f, 192.0f, 224.0f, 255.0f}; 
    
    for (int i = 0; i < 256; i++) {
        float x = (float)i;
        int idx = 0;
        if (x < nodes[1]) idx = 0;
        else if (x < nodes[2]) idx = 1;
        else if (x < nodes[3]) idx = 2;
        else if (x < nodes[4]) idx = 3;
        else if (x < nodes[5]) idx = 4;
        else idx = 5; 

        float x_low = nodes[idx];
        float x_high = (idx == 5) ? 255.0f : nodes[idx+1];
        float w = 0.0f;
        if (idx < 5) w = (x - x_low) / (x_high - x_low);
        else w = (x - 224.0f) / (255.0f - 224.0f);
        
        h_lut_index[i] = idx;
        h_lut_weight[i] = w;
    }
    checkCuda(cudaMemcpyToSymbol(c_lut_index, h_lut_index, 256 * sizeof(int)));
    checkCuda(cudaMemcpyToSymbol(c_lut_weight, h_lut_weight, 256 * sizeof(float)));
}

void createTextureForImage(unsigned char* hostData, int width, int height, cudaTextureObject_t* texObj, cudaArray_t* cuArray) {
    cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned);
    checkCuda(cudaMallocArray(cuArray, &channelDesc, width * 3, height));
    checkCuda(cudaMemcpy2DToArray(*cuArray, 0, 0, hostData, width * 3 * sizeof(unsigned char), width * 3 * sizeof(unsigned char), height, cudaMemcpyHostToDevice));

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

// Kernel (LUT + Texture)
__global__ void demuraKernelLUT(
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
    
    unsigned char rawR = input[idx];
    unsigned char rawG = input[idx + 1];
    unsigned char rawB = input[idx + 2];

    int idxR = c_lut_index[rawR]; float wR = c_lut_weight[rawR];
    int idxG = c_lut_index[rawG]; float wG = c_lut_weight[rawG];
    int idxB = c_lut_index[rawB]; float wB = c_lut_weight[rawB];

    int texX = x * 3;
    float cp_vals[5]; 
    cudaTextureObject_t textures[5] = {texCP0, texCP1, texCP2, texCP3, texCP4};

    // R
    #pragma unroll
    for(int i=0; i<5; i++) cp_vals[i] = tex2D<float>(textures[i], texX, y);
    
    float startR, endR;
    if(idxR == 0) { startR = 0.0f; endR = cp_vals[0]; }
    else if (idxR == 5) { startR = cp_vals[4]; endR = 1.0f; }
    else { startR = cp_vals[idxR-1]; endR = cp_vals[idxR]; }
    float outR = startR + (endR - startR) * wR;

    // G
    #pragma unroll
    for(int i=0; i<5; i++) cp_vals[i] = tex2D<float>(textures[i], texX + 1, y);
    
    float startG, endG;
    if(idxG == 0) { startG = 0.0f; endG = cp_vals[0]; }
    else if (idxG == 5) { startG = cp_vals[4]; endG = 1.0f; }
    else { startG = cp_vals[idxG-1]; endG = cp_vals[idxG]; }
    float outG = startG + (endG - startG) * wG;

    // B
    #pragma unroll
    for(int i=0; i<5; i++) cp_vals[i] = tex2D<float>(textures[i], texX + 2, y);

    float startB, endB;
    if(idxB == 0) { startB = 0.0f; endB = cp_vals[0]; }
    else if (idxB == 5) { startB = cp_vals[4]; endB = 1.0f; }
    else { startB = cp_vals[idxB-1]; endB = cp_vals[idxB]; }
    float outB = startB + (endB - startB) * wB;

    output[idx]     = (unsigned char)(fminf(fmaxf(outR, 0.0f), 1.0f) * 255.0f);
    output[idx + 1] = (unsigned char)(fminf(fmaxf(outG, 0.0f), 1.0f) * 255.0f);
    output[idx + 2] = (unsigned char)(fminf(fmaxf(outB, 0.0f), 1.0f) * 255.0f);
}

int main(int argc, char* argv[]) {
    if (argc != 7) {
        printf("Usage: %s <input> <cp1>...<cp5>\n", argv[0]);
        return 1;
    }

    checkCuda(cudaSetDevice(0));
    initLUT();

    int width = 0; 
    int height = 0;
    
    // [FIXED] 預讀取第一次以確認寬高
    // 傳入 NULL 作為 buffer，讓 loadBMPToPtr 只讀取寬高並安全返回
    if (!loadBMPToPtr(argv[1], NULL, width, height)) {
        printf("Error reading image header.\n");
        return 1;
    }
    printf("Image detected: %dx%d\n", width, height);

    size_t imgSize = width * height * 3 * sizeof(unsigned char);
    
    // ==========================================
    // 1. Pinned Memory Allocation
    // ==========================================
    unsigned char *h_pinned_input, *h_pinned_output;
    unsigned char *h_pinned_cp[5];
    
    checkCuda(cudaMallocHost(&h_pinned_input, imgSize));
    checkCuda(cudaMallocHost(&h_pinned_output, imgSize));
    for(int i=0; i<5; i++) checkCuda(cudaMallocHost(&h_pinned_cp[i], imgSize));

    // ==========================================
    // 2. Disk I/O Measurement
    // ==========================================
    printf("[Init] Loading Images to Pinned Memory...\n");
    auto start_disk = std::chrono::high_resolution_clock::now();
    
    int w, h;
    loadBMPToPtr(argv[1], h_pinned_input, w, h); // 實際讀入
    for(int i=0; i<5; i++) loadBMPToPtr(argv[2+i], h_pinned_cp[i], w, h);
    
    auto end_disk = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> disk_duration = end_disk - start_disk;

    // GPU Memory
    unsigned char *d_input, *d_output;
    checkCuda(cudaMalloc(&d_input, imgSize));
    checkCuda(cudaMalloc(&d_output, imgSize));

    // Texture Setup
    cudaArray_t cuArrayCP[5];
    cudaTextureObject_t texCP[5];
    printf("[Init] Creating Texture Objects...\n");
    for(int i=0; i<5; i++) createTextureForImage(h_pinned_cp[i], width, height, &texCP[i], &cuArrayCP[i]);

    // Events
    cudaEvent_t startH2D, stopH2D, startKernel, stopKernel, startD2H, stopD2H;
    checkCuda(cudaEventCreate(&startH2D)); checkCuda(cudaEventCreate(&stopH2D));
    checkCuda(cudaEventCreate(&startKernel)); checkCuda(cudaEventCreate(&stopKernel));
    checkCuda(cudaEventCreate(&startD2H)); checkCuda(cudaEventCreate(&stopD2H));

    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    int N_FRAMES = 100;
    printf("[Loop] Starting Pinned+LUT simulation (%d frames)...\n", N_FRAMES);
    
    // Warm up
    demuraKernelLUT<<<gridSize, blockSize>>>(d_input, texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], d_output, width, height);
    checkCuda(cudaDeviceSynchronize());

    float totalH2D = 0, totalKernel = 0, totalD2H = 0;

    for (int frame = 0; frame < N_FRAMES; frame++) {
        // H2D
        checkCuda(cudaEventRecord(startH2D));
        checkCuda(cudaMemcpyAsync(d_input, h_pinned_input, imgSize, cudaMemcpyHostToDevice, 0));
        checkCuda(cudaEventRecord(stopH2D));

        // Kernel
        checkCuda(cudaEventRecord(startKernel));
        demuraKernelLUT<<<gridSize, blockSize, 0, 0>>>(d_input, texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], d_output, width, height);
        checkCuda(cudaEventRecord(stopKernel));

        // D2H
        checkCuda(cudaEventRecord(startD2H));
        checkCuda(cudaMemcpyAsync(h_pinned_output, d_output, imgSize, cudaMemcpyDeviceToHost, 0));
        checkCuda(cudaEventRecord(stopD2H));
        
        checkCuda(cudaEventSynchronize(stopD2H));

        float t1, t2, t3;
        cudaEventElapsedTime(&t1, startH2D, stopH2D);
        cudaEventElapsedTime(&t2, startKernel, stopKernel);
        cudaEventElapsedTime(&t3, startD2H, stopD2H);
        totalH2D+=t1; totalKernel+=t2; totalD2H+=t3;
    }

    // 計算平均值
    float avgH2D = totalH2D / N_FRAMES;
    float avgKernel = totalKernel / N_FRAMES;
    float avgD2H = totalD2H / N_FRAMES;
    float avgTotalPCIe = avgH2D + avgD2H;
    float avgFrameTotal = avgTotalPCIe + avgKernel;
    float fps = 1000.0f / avgFrameTotal;

    // 輸出報告
    printf("\n==========================================================\n");
    printf(" C. Disk -> CPU DRAM:           %8.3f ms\n", disk_duration.count());
    printf("----------------------------------------------------------\n");    
    printf(" B. PCIe Total:                 %8.3f ms\n", avgTotalPCIe);
    printf("    - CPU DRAM -> GPU VRAM:     %8.3f ms\n", avgH2D);
    printf("    - GPU VRAM -> CPU DRAM:     %8.3f ms\n", avgD2H);    
    printf("----------------------------------------------------------\n");    
    printf(" A. Kernel:                     %8.3f ms\n", avgKernel);    
    printf("----------------------------------------------------------\n");
    printf(" 平均每幀 GPU 耗時 (A+B):       %8.3f ms\n", avgFrameTotal);
    printf(" 預估 FPS:                      %8.1f FPS\n", fps);    
    printf("==========================================================\n");

    saveBMP("output.bmp", h_pinned_output, width, height);

    // Free Pinned Memory
    checkCuda(cudaFreeHost(h_pinned_input));
    checkCuda(cudaFreeHost(h_pinned_output));
    for(int i=0; i<5; i++) checkCuda(cudaFreeHost(h_pinned_cp[i]));
    
    // Free Device Memory
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