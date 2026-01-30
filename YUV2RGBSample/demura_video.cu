// nvcc -O3 demura_video.cu -o demura_video -lavcodec -lavformat -lavutil -lswscale -lcudart
// ./demura_video Supportingfiles/test/cat.mp4 Supportingfiles/1216/Correction-Gray-32.bmp Supportingfiles/1216/Correction-Gray-64.bmp Supportingfiles/1216/Correction-Gray-128.bmp Supportingfiles/1216/Correction-Gray-192.bmp Supportingfiles/1216/Correction-Gray-224.bmp output.mp4

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// 輔助函式：將 FFmpeg 錯誤碼轉為字串
char av_error_buffer[AV_ERROR_MAX_STRING_SIZE];
#define checkAV(call) { \
    int ret = (call); \
    if (ret < 0) { \
        av_strerror(ret, av_error_buffer, sizeof(av_error_buffer)); \
        printf("FFmpeg Error: %s (code: %d) at %s:%d\n", av_error_buffer, ret, __FILE__, __LINE__); \
        exit(1); \
    } \
}

#pragma pack(push, 1)
typedef struct { uint16_t type; uint32_t size; uint16_t r1; uint16_t r2; uint32_t offset; } BMPFileHeader;
typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t comp; uint32_t imgSize; int32_t xres; int32_t yres; uint32_t ncol; uint32_t impcol; } BMPInfoHeader;
#pragma pack(pop)

#define checkCuda(call) { const cudaError_t error = call; if (error != cudaSuccess) { printf("Error: %s:%d, code:%d\n", __FILE__, __LINE__, error); exit(1); } }

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
        float w = (x - x_low) / (x_high - x_low);
        
        h_lut_index[i] = idx;
        h_lut_weight[i] = w;
    }
    checkCuda(cudaMemcpyToSymbol(c_lut_index, h_lut_index, 256 * sizeof(int)));
    checkCuda(cudaMemcpyToSymbol(c_lut_weight, h_lut_weight, 256 * sizeof(float)));
}

bool loadBMP(const char* filename, std::vector<unsigned char>& data, int& width, int& height) {
    FILE* file = fopen(filename, "rb");
    if (!file) { printf("無法開啟 BMP: %s\n", filename); return false; }
    BMPFileHeader fh; BMPInfoHeader ih;
    if (fread(&fh, sizeof(fh), 1, file) != 1 || fread(&ih, sizeof(ih), 1, file) != 1) { fclose(file); return false; }
    width = ih.width; height = abs(ih.height);
    int rowSize = ((width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> raw(rowSize * height);
    fseek(file, fh.offset, SEEK_SET);
    fread(raw.data(), 1, raw.size(), file);
    fclose(file);
    data.resize(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src = (height - 1 - y) * rowSize + x * 3;
            int dst = y * width * 3 + x * 3;
            data[dst] = raw[src + 2]; data[dst + 1] = raw[src + 1]; data[dst + 2] = raw[src];
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// VideoReader (解碼器)
// ----------------------------------------------------------------------------
class VideoReader {
public:
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* dec_ctx = nullptr;
    int stream_idx = -1;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int width = 0, height = 0;
    AVRational time_base;

    ~VideoReader() {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (dec_ctx) avcodec_free_context(&dec_ctx);
        if (fmt_ctx) avformat_close_input(&fmt_ctx);
    }

    bool open(const char* filename) {
        if (avformat_open_input(&fmt_ctx, filename, nullptr, nullptr) != 0) return false;
        if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) return false;
        const AVCodec* codec = nullptr;
        stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream_idx < 0) return false;
        dec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar);
        if (avcodec_open2(dec_ctx, codec, nullptr) < 0) return false;
        
        width = dec_ctx->width;
        height = dec_ctx->height;
        time_base = fmt_ctx->streams[stream_idx]->time_base;
        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        printf("Input Video: %dx%d (%s)\n", width, height, av_get_pix_fmt_name(dec_ctx->pix_fmt));
        return true;
    }

    bool readNextFrame() {
        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == stream_idx) {
                if (avcodec_send_packet(dec_ctx, pkt) == 0) {
                    if (avcodec_receive_frame(dec_ctx, frame) == 0) {
                        av_packet_unref(pkt);
                        return true;
                    }
                }
            }
            av_packet_unref(pkt);
        }
        return false;
    }
};

// ----------------------------------------------------------------------------
// VideoWriter (編碼器) - 新增
// ----------------------------------------------------------------------------
class VideoWriter {
public:
    AVRational input_framerate;
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame_yuv = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    int frame_pts = 0;

    ~VideoWriter() {
        if (frame_yuv) av_frame_free(&frame_yuv);
        if (pkt) av_packet_free(&pkt);
        if (enc_ctx) avcodec_free_context(&enc_ctx);
        if (fmt_ctx) {
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
        }
        if (sws_ctx) sws_freeContext(sws_ctx);
    }

    bool open(const char* filename, int width, int height, AVRational fps) {
        input_framerate = fps;
        checkAV(avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename));
        
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) { printf("找不到 H.264 編碼器\n"); return false; }

        stream = avformat_new_stream(fmt_ctx, codec);
        enc_ctx = avcodec_alloc_context3(codec);
        enc_ctx->width = width;
        enc_ctx->height = height;
        enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        enc_ctx->time_base = av_inv_q(input_framerate);
        enc_ctx->framerate = input_framerate;
        enc_ctx->colorspace = AVCOL_SPC_BT709;
        enc_ctx->color_range = AVCOL_RANGE_MPEG;
        av_opt_set(enc_ctx->priv_data, "preset", "fast", 0);
        av_opt_set(enc_ctx->priv_data, "crf", "23", 0);

        if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        checkAV(avcodec_open2(enc_ctx, codec, nullptr));
        checkAV(avcodec_parameters_from_context(stream->codecpar, enc_ctx));
        stream->time_base = enc_ctx->time_base;

        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
            checkAV(avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE));
        checkAV(avformat_write_header(fmt_ctx, nullptr));

        // 重要：分配像素緩衝區
        frame_yuv = av_frame_alloc();
        frame_yuv->format = enc_ctx->pix_fmt;
        frame_yuv->width = width;
        frame_yuv->height = height;
        checkAV(av_frame_get_buffer(frame_yuv, 0)); 

        pkt = av_packet_alloc();

        sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24, width, height, 
                                 AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        int *inv_table, *table, srcRange, dstRange, brightness, contrast, saturation;
        sws_getColorspaceDetails(sws_ctx, &inv_table, &srcRange, &table, &dstRange, &brightness, &contrast, &saturation);
        const int *type_709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws_ctx, type_709, srcRange, type_709, dstRange, brightness, contrast, saturation);

        return true;
    }

    void writeFrame(const unsigned char* rgb_data, int width, int height) {
        if (!rgb_data) return;
        const uint8_t* src_slice[] = { (const uint8_t*)rgb_data };
        int src_stride[] = { 3 * width };

        sws_scale(sws_ctx, src_slice, src_stride, 0, height, frame_yuv->data, frame_yuv->linesize);
        
        frame_yuv->pts = frame_pts++;
        if (avcodec_send_frame(enc_ctx, frame_yuv) >= 0) {
            while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
        }
    }

    void close() {
        // Flush encoder
        avcodec_send_frame(enc_ctx, nullptr);
        while (avcodec_receive_packet(enc_ctx, pkt) >= 0) {
            av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
            av_interleaved_write_frame(fmt_ctx, pkt);
            av_packet_unref(pkt);
        }
        av_write_trailer(fmt_ctx);
    }
};

// --- Texture Helpers ---
void createTexture(unsigned char* hostData, int width, int height, cudaTextureObject_t* texObj, cudaArray_t* cuArray) {
    cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned);
    checkCuda(cudaMallocArray(cuArray, &channelDesc, width * 3, height));
    checkCuda(cudaMemcpy2DToArray(*cuArray, 0, 0, hostData, width * 3, width * 3, height, cudaMemcpyHostToDevice));

    struct cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = *cuArray;

    struct cudaTextureDesc texDesc = {};
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;
    texDesc.readMode = cudaReadModeNormalizedFloat;

    checkCuda(cudaCreateTextureObject(texObj, &resDesc, &texDesc, NULL));
}

// --- 更新後的 Kernel：使用 BT.709 Limited Range 係數 ---
__global__ void demuraVideoKernelLUT(
    const unsigned char* __restrict__ srcY, int pitchY,
    const unsigned char* __restrict__ srcU, int pitchU,
    const unsigned char* __restrict__ srcV, int pitchV,
    cudaTextureObject_t texCP0, cudaTextureObject_t texCP1, 
    cudaTextureObject_t texCP2, cudaTextureObject_t texCP3, cudaTextureObject_t texCP4,
    unsigned char* __restrict__ dstRGB,
    int width, int height
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    // 1. 讀取原始 YUV 數值 (0-255)
    float y_raw = (float)srcY[y * pitchY + x];
    int uv_idx = (y / 2) * pitchU + (x / 2);
    float u_raw = (float)srcU[uv_idx];
    float v_raw = (float)srcV[uv_idx];

    // 2. BT.709 Limited Range (HDTV) 轉換公式
    // Y' 範圍 [16, 235], Cb/Cr 範圍 [16, 240]
    float r_f = 1.164383f * (y_raw - 16.0f) + 1.792741f * (v_raw - 128.0f);
    float g_f = 1.164383f * (y_raw - 16.0f) - 0.213249f * (u_raw - 128.0f) - 0.532909f * (v_raw - 128.0f);
    float b_f = 1.164383f * (y_raw - 16.0f) + 2.112402f * (u_raw - 128.0f);

    // 3. 歸一化至 0.0~1.0 並進行截斷處理，供 De-mura LUT 使用
    float r_norm = fminf(fmaxf(r_f / 255.0f, 0.0f), 1.0f);
    float g_norm = fminf(fmaxf(g_f / 255.0f, 0.0f), 1.0f);
    float b_norm = fminf(fmaxf(b_f / 255.0f, 0.0f), 1.0f);

    // 4. 準備 LUT 索引 (0-255)
    unsigned char raw[3] = { (unsigned char)(r_norm * 255.0f), 
                             (unsigned char)(g_norm * 255.0f), 
                             (unsigned char)(b_norm * 255.0f) };
    float out[3];
    cudaTextureObject_t textures[5] = {texCP0, texCP1, texCP2, texCP3, texCP4};

    // 5. De-mura LUT 內插 (R, G, B)
    #pragma unroll
    for(int c = 0; c < 3; c++) {
        int l_idx = c_lut_index[raw[c]];
        float l_w = c_lut_weight[raw[c]];
        
        float cp_vals[5];
        #pragma unroll
        for(int i=0; i<5; i++) cp_vals[i] = tex2D<float>(textures[i], x * 3 + c, y);

        float start, end;
        if(l_idx == 0) { start = 0.0f; end = cp_vals[0]; }
        else if (l_idx == 5) { start = cp_vals[4]; end = 1.0f; }
        else { start = cp_vals[l_idx-1]; end = cp_vals[l_idx]; }
        
        out[c] = start + (end - start) * l_w;
    }

    // 6. 寫回輸出 (RGB24 格式)
    int out_idx = (y * width + x) * 3;
    dstRGB[out_idx]     = (unsigned char)(fminf(fmaxf(out[0], 0.0f), 1.0f) * 255.0f);
    dstRGB[out_idx + 1] = (unsigned char)(fminf(fmaxf(out[1], 0.0f), 1.0f) * 255.0f);
    dstRGB[out_idx + 2] = (unsigned char)(fminf(fmaxf(out[2], 0.0f), 1.0f) * 255.0f);
}

int main(int argc, char* argv[]) {
    if (argc < 8) {
        printf("用法: %s <input.mp4> <cp1~5.bmp> <output.mp4>\n", argv[0]);
        return 1;
    }

    checkCuda(cudaSetDevice(0));
    initLUT();

    // ==========================================
    // 1. 測量磁碟讀取與初始化 (Disk -> CPU DRAM)
    // ==========================================
    auto start_disk = std::chrono::high_resolution_clock::now();

    VideoReader vr;
    if (!vr.open(argv[1])) return 1;
    int width = vr.dec_ctx->width;
    int height = vr.dec_ctx->height;
    AVRational fps_ratio = av_guess_frame_rate(vr.fmt_ctx, vr.fmt_ctx->streams[vr.stream_idx], nullptr);

    std::vector<unsigned char> h_cp[5];
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[2 + i], h_cp[i], w, h) || w != width || h != height) {
            printf("[Error] BMP 圖片載入失敗或尺寸不符.\n");
            return 1;
        }
    }

    auto end_disk = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> disk_duration = end_disk - start_disk;

    // ==========================================
    // 2. GPU 資源準備
    // ==========================================
    VideoWriter vw;
    if (!vw.open(argv[7], width, height, fps_ratio)) return 1;

    unsigned char *d_y, *d_u, *d_v, *d_output;
    checkCuda(cudaMalloc(&d_y, width * height));
    checkCuda(cudaMalloc(&d_u, width * height / 4));
    checkCuda(cudaMalloc(&d_v, width * height / 4));
    checkCuda(cudaMalloc(&d_output, width * height * 3));

    cudaArray_t cuArrayCP[5];
    cudaTextureObject_t texCP[5];
    for(int i=0; i<5; i++) createTexture(h_cp[i].data(), width, height, &texCP[i], &cuArrayCP[i]);

    std::vector<unsigned char> h_output(width * height * 3);
    dim3 block(16, 16), grid((width + 15) / 16, (height + 15) / 16);

    // 建立效能測量事件
    cudaEvent_t startH2D, stopH2D, startKernel, stopKernel, startD2H, stopD2H;
    checkCuda(cudaEventCreate(&startH2D)); checkCuda(cudaEventCreate(&stopH2D));
    checkCuda(cudaEventCreate(&startKernel)); checkCuda(cudaEventCreate(&stopKernel));
    checkCuda(cudaEventCreate(&startD2H)); checkCuda(cudaEventCreate(&stopD2H));

    float totalH2D = 0, totalKernel = 0, totalD2H = 0;
    int frameCount = 0;

    printf("[Start] 開始處理影片 (FPS: %.2f)...\n", av_q2d(fps_ratio));

    // ==========================================
    // 3. 處理迴圈 (累加 GPU 耗時)
    // ==========================================
    while (vr.readNextFrame()) {
        // H2D: YUV 資料傳輸
        checkCuda(cudaEventRecord(startH2D));
        checkCuda(cudaMemcpy2D(d_y, width, vr.frame->data[0], vr.frame->linesize[0], width, height, cudaMemcpyHostToDevice));
        checkCuda(cudaMemcpy2D(d_u, width/2, vr.frame->data[1], vr.frame->linesize[1], width/2, height/2, cudaMemcpyHostToDevice));
        checkCuda(cudaMemcpy2D(d_v, width/2, vr.frame->data[2], vr.frame->linesize[2], width/2, height/2, cudaMemcpyHostToDevice));
        checkCuda(cudaEventRecord(stopH2D));

        // Kernel: De-mura 運算
        checkCuda(cudaEventRecord(startKernel));
        demuraVideoKernelLUT<<<grid, block>>>(d_y, width, d_u, width/2, d_v, width/2, texCP[0], texCP[1], texCP[2], texCP[3], texCP[4], d_output, width, height);
        checkCuda(cudaEventRecord(stopKernel));

        // D2H: 結果回傳
        checkCuda(cudaEventRecord(startD2H));
        checkCuda(cudaMemcpy(h_output.data(), d_output, width * height * 3, cudaMemcpyDeviceToHost));
        checkCuda(cudaEventRecord(stopD2H));

        // 等待當前影格 GPU 任務完成並累加時間
        checkCuda(cudaEventSynchronize(stopD2H));
        float t1, t2, t3;
        cudaEventElapsedTime(&t1, startH2D, stopH2D);
        cudaEventElapsedTime(&t2, startKernel, stopKernel);
        cudaEventElapsedTime(&t3, startD2H, stopD2H);
        totalH2D += t1; totalKernel += t2; totalD2H += t3;

        // 寫入影片 (此階段包含 CPU 編碼耗時，通常不計入 GPU 核心耗時)
        vw.writeFrame(h_output.data(), width, height);
        
        if (++frameCount % 30 == 0) {
            printf("\r已處理影格: %d", frameCount);
            fflush(stdout);
        }
    }

    // ==========================================
    // 4. 計算平均值並輸出 Log
    // ==========================================
    float avgH2D = totalH2D / frameCount;
    float avgKernel = totalKernel / frameCount;
    float avgD2H = totalD2H / frameCount;
    float avgPCIe = avgH2D + avgD2H;
    float avgFrameTotal = avgPCIe + avgKernel;
    float estFps = 1000.0f / avgFrameTotal;

    printf("\n\n==========================================================\n");
    printf(" C. Disk -> CPU DRAM (Init):    %8.3f ms\n", disk_duration.count());
    printf("----------------------------------------------------------\n");    
    printf(" B. PCIe Total (Avg/Frame):     %8.3f ms\n", avgPCIe);
    printf("    - CPU DRAM -> GPU VRAM:     %8.3f ms\n", avgH2D);
    printf("    - GPU VRAM -> CPU DRAM:     %8.3f ms\n", avgD2H);    
    printf("----------------------------------------------------------\n");    
    printf(" A. Kernel (Avg/Frame):         %8.3f ms\n", avgKernel);    
    printf("----------------------------------------------------------\n");
    printf(" 平均每幀 GPU 耗時 (A+B):       %8.3f ms\n", avgFrameTotal);
    printf(" 預估 GPU 極限 FPS:             %8.1f FPS\n", estFps);    
    printf("==========================================================\n");

    vw.close();
    // ... (資源清理保持不變) ...
    return 0;
}