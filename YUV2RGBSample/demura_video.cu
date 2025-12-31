// nvcc -O3 demura_video.cu -o demura_video -lavcodec -lavformat -lavutil -lcudart
// ./demura_video input.mp4 cp1.bmp cp2.bmp cp3.bmp cp4.bmp cp5.bmp

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

// ----------------------------------------------------------------------------
// 錯誤檢查巨集
// ----------------------------------------------------------------------------
#define checkCuda(call) { \
    const cudaError_t error = call; \
    if (error != cudaSuccess) { \
        printf("CUDA Error: %s:%d, code:%d, reason: %s\n", __FILE__, __LINE__, error, cudaGetErrorString(error)); \
        exit(1); \
    } \
}

// ----------------------------------------------------------------------------
// BMP 工具
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct { uint16_t type; uint32_t size; uint16_t r1; uint16_t r2; uint32_t offset; } BMPFileHeader;
typedef struct { uint32_t size; int32_t width; int32_t height; uint16_t planes; uint16_t bits; uint32_t comp; uint32_t imgSize; int32_t xres; int32_t yres; uint32_t ncol; uint32_t impcol; } BMPInfoHeader;
#pragma pack(pop)

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
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* enc_ctx = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame_yuv = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr; // 用於 RGB -> YUV 轉換
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

    bool open(const char* filename, int width, int height) {
        avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename);
        if (!fmt_ctx) return false;

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) { printf("找不到 H.264 Encoder\n"); return false; }

        stream = avformat_new_stream(fmt_ctx, codec);
        enc_ctx = avcodec_alloc_context3(codec);
        
        enc_ctx->width = width;
        enc_ctx->height = height;
        enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // H.264 最通用的格式
        enc_ctx->time_base = {1, 30}; // 假設 30 FPS
        enc_ctx->framerate = {30, 1};
        
        // 設定 H.264 參數 (品質 vs 速度)
        av_opt_set(enc_ctx->priv_data, "preset", "fast", 0);
        av_opt_set(enc_ctx->priv_data, "crf", "23", 0); // 固定品質係數

        if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(enc_ctx, codec, nullptr) < 0) return false;
        avcodec_parameters_from_context(stream->codecpar, enc_ctx);

        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE) < 0) return false;
        }

        if (avformat_write_header(fmt_ctx, nullptr) < 0) return false;

        // 準備 YUV Frame 容器
        frame_yuv = av_frame_alloc();
        frame_yuv->format = enc_ctx->pix_fmt;
        frame_yuv->width = width;
        frame_yuv->height = height;
        av_frame_get_buffer(frame_yuv, 32);

        pkt = av_packet_alloc();

        // 初始化 SWS Context: RGB24 (from GPU output) -> YUV420P (for Encoder)
        sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                 width, height, AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        printf("Output Video: %s (H.264)\n", filename);
        return true;
    }

    // 輸入 RGB 資料，自動轉 YUV 並編碼寫入
    void writeFrame(const unsigned char* rgb_data, int width, int height) {
        // 1. RGB -> YUV 轉換 (使用 FFmpeg SWScale，CPU 運算)
        // 註: 追求極致效能可在 CUDA 內直接轉 YUV，但這會增加 Kernel 複雜度，
        // 這裡為了可靠性使用 CPU 轉換。
        const int in_linesize[1] = { 3 * width };
        sws_scale(sws_ctx, &rgb_data, in_linesize, 0, height,
                  frame_yuv->data, frame_yuv->linesize);

        frame_yuv->pts = frame_pts++;

        // 2. 送入編碼器
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

// ----------------------------------------------------------------------------
// CUDA Kernel (保持不變)
// ----------------------------------------------------------------------------
__device__ float interpolate(float x, float y0, float y1, float y2, float y3, float y4) {
    const float x0 = 32.0f/255.0f; const float x1 = 64.0f/255.0f; const float x2 = 128.0f/255.0f; const float x3 = 192.0f/255.0f; const float x4 = 224.0f/255.0f;
    float x_low, x_high, y_low, y_high;
    if (x < x0) { x_low=0.0f; y_low=0.0f; x_high=x0; y_high=y0; }
    else if (x < x1) { x_low=x0; y_low=y0; x_high=x1; y_high=y1; }
    else if (x < x2) { x_low=x1; y_low=y1; x_high=x2; y_high=y2; }
    else if (x < x3) { x_low=x2; y_low=y2; x_high=x3; y_high=y3; }
    else if (x < x4) { x_low=x3; y_low=y3; x_high=x4; y_high=y4; }
    else { x_low=x4; y_low=y4; x_high=1.0f; y_high=1.0f; }
    float den = x_high - x_low; if (den == 0.0f) return y_high;
    float m = (y_high - y_low) / den; return fminf(fmaxf(y_low + m * (x - x_low), 0.0f), 1.0f);
}

__global__ void demuraVideoKernel(
    const unsigned char* __restrict__ srcY, int pitchY,
    const unsigned char* __restrict__ srcU, int pitchU,
    const unsigned char* __restrict__ srcV, int pitchV,
    const unsigned char* __restrict__ cp0, const unsigned char* __restrict__ cp1,
    const unsigned char* __restrict__ cp2, const unsigned char* __restrict__ cp3,
    const unsigned char* __restrict__ cp4,
    unsigned char* __restrict__ dstRGB,
    int width, int height
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    float y_val = srcY[y * pitchY + x] / 255.0f;
    int uv_idx = (y / 2) * pitchU + (x / 2);
    float u_val = (srcU[uv_idx] / 255.0f) - 0.5f;
    float v_val = (srcV[uv_idx] / 255.0f) - 0.5f;

    float r = y_val + 1.5748f * v_val;
    float g = y_val - 0.1873f * u_val - 0.4681f * v_val;
    float b = y_val + 1.8556f * u_val;
    
    r = fminf(fmaxf(r, 0.0f), 1.0f); g = fminf(fmaxf(g, 0.0f), 1.0f); b = fminf(fmaxf(b, 0.0f), 1.0f);

    int cp_idx = (y * width + x) * 3;
    const unsigned char* cps[] = {cp0, cp1, cp2, cp3, cp4};
    float c_r[5], c_g[5], c_b[5];
    #pragma unroll
    for(int i=0; i<5; i++) {
        c_r[i] = cps[i][cp_idx] / 255.0f; c_g[i] = cps[i][cp_idx + 1] / 255.0f; c_b[i] = cps[i][cp_idx + 2] / 255.0f;
    }

    float outR = interpolate(r, c_r[0], c_r[1], c_r[2], c_r[3], c_r[4]);
    float outG = interpolate(g, c_g[0], c_g[1], c_g[2], c_g[3], c_g[4]);
    float outB = interpolate(b, c_b[0], c_b[1], c_b[2], c_b[3], c_b[4]);

    int out_idx = (y * width + x) * 3;
    dstRGB[out_idx] = (unsigned char)(outR * 255.0f);
    dstRGB[out_idx + 1] = (unsigned char)(outG * 255.0f);
    dstRGB[out_idx + 2] = (unsigned char)(outB * 255.0f);
}

// ----------------------------------------------------------------------------
// 主程式
// ----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc != 8) {
        printf("Usage: %s <input.mp4> <output.mp4> <cp1> <cp2> <cp3> <cp4> <cp5>\n", argv[0]);
        return 1;
    }

    // 1. 初始化
    VideoReader vr;
    if (!vr.open(argv[1])) { printf("Open input failed.\n"); return 1; }

    VideoWriter vw;
    if (!vw.open(argv[2], vr.width, vr.height)) { printf("Open output failed.\n"); return 1; }

    std::vector<unsigned char> h_cp[5];
    for (int i = 0; i < 5; i++) {
        int w, h;
        if (!loadBMP(argv[3+i], h_cp[i], w, h) || w != vr.width || h != vr.height) {
            printf("Control Point Error: %d\n", i); return 1;
        }
    }

    // 2. GPU 記憶體分配
    unsigned char *d_y, *d_u, *d_v, *d_output;
    unsigned char *d_cp[5];
    int width = vr.width; int height = vr.height;

    checkCuda(cudaMalloc(&d_y, width * height));
    checkCuda(cudaMalloc(&d_u, width * height / 4));
    checkCuda(cudaMalloc(&d_v, width * height / 4));
    checkCuda(cudaMalloc(&d_output, width * height * 3));
    for(int i=0; i<5; i++) {
        checkCuda(cudaMalloc(&d_cp[i], width * height * 3));
        checkCuda(cudaMemcpy(d_cp[i], h_cp[i].data(), width * height * 3, cudaMemcpyHostToDevice));
    }

    std::vector<unsigned char> h_output(width * height * 3);
    dim3 blockSize(16, 16);
    dim3 gridSize((width + 15) / 16, (height + 15) / 16);

    printf("Processing: %s -> %s\n", argv[1], argv[2]);
    int frameCount = 0;
    auto total_start = std::chrono::high_resolution_clock::now();

    // 3. 處理迴圈
    while (vr.readNextFrame()) {
        // [GPU] Copy YUV
        checkCuda(cudaMemcpy2D(d_y, width, vr.frame->data[0], vr.frame->linesize[0], width, height, cudaMemcpyHostToDevice));
        checkCuda(cudaMemcpy2D(d_u, width/2, vr.frame->data[1], vr.frame->linesize[1], width/2, height/2, cudaMemcpyHostToDevice));
        checkCuda(cudaMemcpy2D(d_v, width/2, vr.frame->data[2], vr.frame->linesize[2], width/2, height/2, cudaMemcpyHostToDevice));

        // [GPU] Kernel
        demuraVideoKernel<<<gridSize, blockSize>>>(d_y, width, d_u, width/2, d_v, width/2, 
            d_cp[0], d_cp[1], d_cp[2], d_cp[3], d_cp[4], d_output, width, height);
        cudaDeviceSynchronize();

        // [CPU] Copy back & Write Video
        checkCuda(cudaMemcpy(h_output.data(), d_output, width * height * 3, cudaMemcpyDeviceToHost));
        vw.writeFrame(h_output.data(), width, height);

        if (++frameCount % 30 == 0) printf("\rProcessed %d frames...", frameCount);
    }

    vw.close();

    // 4. 清理
    cudaFree(d_y); cudaFree(d_u); cudaFree(d_v); cudaFree(d_output);
    for(int i=0; i<5; i++) cudaFree(d_cp[i]);
    
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_s = std::chrono::duration_cast<std::chrono::seconds>(total_end - total_start).count();
    printf("\nDone! Total time: %.0f sec\n", total_s);

    return 0;
}