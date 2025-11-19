// /*
// 主要功能說明：

// 開啟 framebuffer 設備 - 存取 /dev/fb0 以讀取螢幕內容
// 取得螢幕資訊 - 使用 ioctl 取得解析度、色彩深度等參數
// 記憶體映射 - 將 framebuffer 映射到使用者空間，方便直接存取像素資料
// 像素格式轉換 - 支援 16-bit (RGB565) 和 32-bit (ARGB/BGRA) 格式
// 輸出 PPM 檔案 - 將螢幕內容儲存為標準 PPM 圖片格式
// */

// #include <stdio.h>
// #include <stdlib.h>
// #include <fcntl.h>
// #include <sys/mman.h>
// #include <sys/ioctl.h>
// #include <linux/fb.h>
// #include <unistd.h>

// int main() {
//     // 開啟 framebuffer 設備檔案
//     int fd = open("/dev/fb0", O_RDWR);
//     if (fd < 0) {
//         perror("open /dev/fb0");
//         return 1;
//     }

//     // 取得 framebuffer 的固定螢幕資訊和可變螢幕資訊
//     struct fb_fix_screeninfo finfo;  // 固定資訊：記憶體位址、長度等
//     struct fb_var_screeninfo vinfo;  // 可變資訊：解析度、色彩深度等
    
//     // 取得固定螢幕資訊
//     if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
//         perror("FBIOGET_FSCREENINFO");
//         close(fd);
//         return 1;
//     }
    
//     // 取得可變螢幕資訊
//     if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
//         perror("FBIOGET_VSCREENINFO");
//         close(fd);
//         return 1;
//     }

//     // 除錯輸出：顯示螢幕參數
//     printf("xres=%d, yres=%d, bpp=%d, line_length=%d, smem_len=%d\n",
//            vinfo.xres,              // 水平解析度（寬度）
//            vinfo.yres,              // 垂直解析度（高度）
//            vinfo.bits_per_pixel,    // 每像素位元數
//            finfo.line_length,       // 每行的位元組數（包含 padding）
//            finfo.smem_len);         // 顯示記憶體總大小

//     // 計算每像素的位元組數
//     int bytespp = vinfo.bits_per_pixel / 8;
    
//     // 使用總記憶體大小（避免因 virtual 解析度造成的問題）
//     long screensize = finfo.smem_len;
    
//     // 將 framebuffer 記憶體映射到使用者空間
//     unsigned char *fbp = (unsigned char *)mmap(0, screensize, 
//                                                PROT_READ | PROT_WRITE,  // 讀寫權限
//                                                MAP_SHARED,              // 共享映射
//                                                fd, 0);
//     if (fbp == MAP_FAILED) {
//         perror("mmap");
//         close(fd);
//         return 1;
//     }

//     // 建立並開啟輸出檔案（PPM 格式）
//     FILE *f = fopen("screenshot.ppm", "w");
//     if (!f) {
//         perror("fopen");
//         munmap(fbp, screensize);
//         close(fd);
//         return 1;
//     }
    
//     // 寫入 PPM 檔案標頭（P6 表示二進位 RGB 格式）
//     fprintf(f, "P6\n%d %d\n255\n", vinfo.xres, vinfo.yres);

//     // 每行的實際位元組數（stride，包含對齊的 padding）
//     int stride = finfo.line_length;
    
//     // 逐像素讀取並轉換顏色
//     for (int y = 0; y < vinfo.yres; y++) {
//         for (int x = 0; x < vinfo.xres; x++) {
//             // 計算像素在 framebuffer 中的偏移量
//             long offset = (long)y * stride + (long)x * bytespp;
//             unsigned char r, g, b;

//             if (vinfo.bits_per_pixel == 32) {
//                 // 32-bit 色彩模式：通常是 ARGB 或 BGRA 格式
//                 unsigned char *pixel = fbp + offset;
                
//                 // 根據各顏色通道的 offset 和 length 提取並正規化到 0-255
//                 r = (pixel[vinfo.red.offset / 8] * 255) / ((1 << vinfo.red.length) - 1);
//                 g = (pixel[vinfo.green.offset / 8] * 255) / ((1 << vinfo.green.length) - 1);
//                 b = (pixel[vinfo.blue.offset / 8] * 255) / ((1 << vinfo.blue.length) - 1);
                
//             } else if (vinfo.bits_per_pixel == 16) {
//                 // 16-bit RGB565 格式：R(5位) G(6位) B(5位)
//                 unsigned short pixel = *(unsigned short *)(fbp + offset);
                
//                 // 提取各顏色通道並擴展到 8-bit
//                 r = ((pixel >> 11) & 0x1F) << 3;  // 取最高 5 位，左移 3 位擴展到 8 位
//                 g = ((pixel >> 5) & 0x3F) << 2;   // 取中間 6 位，左移 2 位擴展到 8 位
//                 b = (pixel & 0x1F) << 3;          // 取最低 5 位，左移 3 位擴展到 8 位
                
//             } else {
//                 // 不支援的色彩深度
//                 printf("Unsupported bpp: %d\n", vinfo.bits_per_pixel);
//                 fclose(f);
//                 munmap(fbp, screensize);
//                 close(fd);
//                 return 1;
//             }

//             // 寫入 RGB 值到檔案
//             fprintf(f, "%c%c%c", r, g, b);
//         }
//     }
//     fclose(f);

//     // 清理資源
//     munmap(fbp, screensize);  // 取消記憶體映射
//     close(fd);                // 關閉檔案描述符
    
//     printf("Framebuffer 已儲存為 screenshot.ppm (解析度: %dx%d, bpp: %d)\n",
//            vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
//     return 0;
// }


// #include <stdio.h>
// #include <stdlib.h>
// #include <fcntl.h>
// #include <unistd.h>
// #include <sys/mman.h>
// #include <sys/ioctl.h>
// #include <linux/fb.h>
// #include <string.h>
// #include <stdint.h>

// int main(int argc, char *argv[]) {
//     const char *fb_device = "/dev/fb0";
//     const char *output_file = "framebuffer_dump.raw";
//     int fb_fd;
//     struct fb_var_screeninfo vinfo;
//     struct fb_fix_screeninfo finfo;
//     uint8_t *fb_mem;
//     FILE *out_fp;
//     long screensize;

//     // 如果有命令列參數,使用指定的輸出檔名
//     if (argc > 1) {
//         output_file = argv[1];
//     }
//     if (argc > 2) {
//         fb_device = argv[2];
//     }

//     // 開啟 framebuffer 裝置
//     fb_fd = open(fb_device, O_RDONLY);
//     if (fb_fd == -1) {
//         perror("Error opening framebuffer device");
//         return 1;
//     }

//     // 取得可變螢幕資訊
//     if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
//         perror("Error reading variable screen info");
//         close(fb_fd);
//         return 1;
//     }

//     // 取得固定螢幕資訊
//     if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
//         perror("Error reading fixed screen info");
//         close(fb_fd);
//         return 1;
//     }

//     // 顯示 framebuffer 資訊
//     printf("Framebuffer Information:\n");
//     printf("========================\n");
//     printf("Resolution: %dx%d\n", vinfo.xres, vinfo.yres);
//     printf("Virtual Resolution: %dx%d\n", vinfo.xres_virtual, vinfo.yres_virtual);
//     printf("Bits per pixel: %d\n", vinfo.bits_per_pixel);
//     printf("Line length: %d bytes\n", finfo.line_length);
//     printf("Red: offset=%d, length=%d\n", vinfo.red.offset, vinfo.red.length);
//     printf("Green: offset=%d, length=%d\n", vinfo.green.offset, vinfo.green.length);
//     printf("Blue: offset=%d, length=%d\n", vinfo.blue.offset, vinfo.blue.length);
//     printf("Alpha: offset=%d, length=%d\n", vinfo.transp.offset, vinfo.transp.length);

//     // 計算螢幕大小
//     screensize = vinfo.yres_virtual * finfo.line_length;
//     printf("Screensize: %ld bytes\n", screensize);
//     printf("========================\n\n");

//     // 將 framebuffer 映射到記憶體
//     fb_mem = (uint8_t *)mmap(0, screensize, PROT_READ, MAP_SHARED, fb_fd, 0);
//     if (fb_mem == MAP_FAILED) {
//         perror("Error mapping framebuffer to memory");
//         close(fb_fd);
//         return 1;
//     }

//     // 開啟輸出檔案
//     out_fp = fopen(output_file, "wb");
//     if (out_fp == NULL) {
//         perror("Error opening output file");
//         munmap(fb_mem, screensize);
//         close(fb_fd);
//         return 1;
//     }

//     // 寫入 framebuffer 內容到檔案
//     printf("Dumping framebuffer to %s...\n", output_file);
//     size_t written = fwrite(fb_mem, 1, screensize, out_fp);
//     if (written != screensize) {
//         fprintf(stderr, "Error: only wrote %zu of %ld bytes\n", written, screensize);
//     } else {
//         printf("Successfully dumped %ld bytes\n", screensize);
//     }

//     // 額外產生一個資訊檔
//     char info_file[256];
//     snprintf(info_file, sizeof(info_file), "%s.info", output_file);
//     FILE *info_fp = fopen(info_file, "w");
//     if (info_fp) {
//         fprintf(info_fp, "width=%d\n", vinfo.xres);
//         fprintf(info_fp, "height=%d\n", vinfo.yres);
//         fprintf(info_fp, "virtual_width=%d\n", vinfo.xres_virtual);
//         fprintf(info_fp, "virtual_height=%d\n", vinfo.yres_virtual);
//         fprintf(info_fp, "bpp=%d\n", vinfo.bits_per_pixel);
//         fprintf(info_fp, "line_length=%d\n", finfo.line_length);
//         fprintf(info_fp, "red_offset=%d\n", vinfo.red.offset);
//         fprintf(info_fp, "red_length=%d\n", vinfo.red.length);
//         fprintf(info_fp, "green_offset=%d\n", vinfo.green.offset);
//         fprintf(info_fp, "green_length=%d\n", vinfo.green.length);
//         fprintf(info_fp, "blue_offset=%d\n", vinfo.blue.offset);
//         fprintf(info_fp, "blue_length=%d\n", vinfo.blue.length);
//         fprintf(info_fp, "alpha_offset=%d\n", vinfo.transp.offset);
//         fprintf(info_fp, "alpha_length=%d\n", vinfo.transp.length);
//         fclose(info_fp);
//         printf("Info file saved to %s\n", info_file);
//     }

//     // 清理資源
//     fclose(out_fp);
//     munmap(fb_mem, screensize);
//     close(fb_fd);

//     printf("\nDone!\n");
//     printf("To convert to PNG, you can use:\n");
//     printf("ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt rgb32 -s %dx%d -i %s -f image2 -vcodec png output.png\n",
//            vinfo.xres, vinfo.yres, output_file);

//     return 0;
// }

// #include <stdio.h>
// #include <stdlib.h>
// #include <stdint.h>
// #include <fcntl.h>
// #include <unistd.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <sys/ioctl.h>
// #include <errno.h>
// #include <drm/drm.h>
// #include <drm/drm_mode.h>
// #include <xf86drm.h>
// #include <xf86drmMode.h>

// int main(int argc, char *argv[]) {
//     const char *drm_device = "/dev/dri/card0";
//     const char *output_file = "drm_capture.raw";
//     int drm_fd;
//     drmModeRes *resources;
//     drmModeCrtc *crtc = NULL;
//     drmModeFB *fb_info = NULL;
//     uint32_t fb_id = 0;
//     void *fb_data = NULL;
//     size_t fb_size;
//     FILE *out_fp;

//     if (argc > 1) {
//         output_file = argv[1];
//     }
//     if (argc > 2) {
//         drm_device = argv[2];
//     }

//     // 開啟 DRM 裝置
//     drm_fd = open(drm_device, O_RDWR);
//     if (drm_fd < 0) {
//         perror("Failed to open DRM device");
//         printf("Try: sudo ./drm_dump [output_file] [/dev/dri/card0]\n");
//         return 1;
//     }

//     // 取得 DRM 資源
//     resources = drmModeGetResources(drm_fd);
//     if (!resources) {
//         perror("Failed to get DRM resources");
//         close(drm_fd);
//         return 1;
//     }

//     printf("Found %d connectors, %d encoders, %d crtcs\n",
//            resources->count_connectors, resources->count_encoders, resources->count_crtcs);

//     // 尋找活躍的 CRTC (顯示控制器)
//     for (int i = 0; i < resources->count_crtcs; i++) {
//         crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
//         if (crtc && crtc->buffer_id) {
//             fb_id = crtc->buffer_id;
//             printf("\nFound active CRTC %d with framebuffer ID: %u\n", i, fb_id);
//             printf("Mode: %dx%d\n", crtc->width, crtc->height);
//             break;
//         }
//         if (crtc) {
//             drmModeFreeCrtc(crtc);
//             crtc = NULL;
//         }
//     }

//     if (!crtc || !fb_id) {
//         fprintf(stderr, "No active display found\n");
//         drmModeFreeResources(resources);
//         close(drm_fd);
//         return 1;
//     }

//     // 取得 framebuffer 資訊
//     fb_info = drmModeGetFB(drm_fd, fb_id);
//     if (!fb_info) {
//         perror("Failed to get framebuffer info");
//         drmModeFreeCrtc(crtc);
//         drmModeFreeResources(resources);
//         close(drm_fd);
//         return 1;
//     }

//     printf("\nFramebuffer Info:\n");
//     printf("=================\n");
//     printf("Size: %dx%d\n", fb_info->width, fb_info->height);
//     printf("Pitch: %u bytes\n", fb_info->pitch);
//     printf("BPP: %u\n", fb_info->bpp);
//     printf("Depth: %u\n", fb_info->depth);
//     printf("Handle: %u\n", fb_info->handle);

//     // 映射 framebuffer 到記憶體
//     struct drm_mode_map_dumb map_req = {0};
//     map_req.handle = fb_info->handle;

//     if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req)) {
//         perror("Failed to map dumb buffer");
//         drmModeFreeFB(fb_info);
//         drmModeFreeCrtc(crtc);
//         drmModeFreeResources(resources);
//         close(drm_fd);
//         return 1;
//     }

//     fb_size = fb_info->pitch * fb_info->height;
//     fb_data = mmap(0, fb_size, PROT_READ, MAP_SHARED, drm_fd, map_req.offset);
    
//     if (fb_data == MAP_FAILED) {
//         perror("Failed to mmap framebuffer");
//         drmModeFreeFB(fb_info);
//         drmModeFreeCrtc(crtc);
//         drmModeFreeResources(resources);
//         close(drm_fd);
//         return 1;
//     }

//     printf("\nMapped %zu bytes at offset 0x%llx\n", fb_size, map_req.offset);

//     // 寫入檔案
//     out_fp = fopen(output_file, "wb");
//     if (!out_fp) {
//         perror("Failed to open output file");
//         munmap(fb_data, fb_size);
//         drmModeFreeFB(fb_info);
//         drmModeFreeCrtc(crtc);
//         drmModeFreeResources(resources);
//         close(drm_fd);
//         return 1;
//     }

//     printf("\nWriting to %s...\n", output_file);
//     size_t written = fwrite(fb_data, 1, fb_size, out_fp);
//     if (written != fb_size) {
//         fprintf(stderr, "Warning: wrote %zu of %zu bytes\n", written, fb_size);
//     } else {
//         printf("Successfully wrote %zu bytes\n", written);
//     }

//     // 寫入資訊檔
//     char info_file[256];
//     snprintf(info_file, sizeof(info_file), "%s.info", output_file);
//     FILE *info_fp = fopen(info_file, "w");
//     if (info_fp) {
//         fprintf(info_fp, "width=%u\n", fb_info->width);
//         fprintf(info_fp, "height=%u\n", fb_info->height);
//         fprintf(info_fp, "pitch=%u\n", fb_info->pitch);
//         fprintf(info_fp, "bpp=%u\n", fb_info->bpp);
//         fprintf(info_fp, "depth=%u\n", fb_info->depth);
//         fclose(info_fp);
//         printf("Info saved to %s\n", info_file);
//     }

//     // 清理
//     fclose(out_fp);
//     munmap(fb_data, fb_size);
//     drmModeFreeFB(fb_info);
//     drmModeFreeCrtc(crtc);
//     drmModeFreeResources(resources);
//     close(drm_fd);

//     printf("\nDone! Convert with:\n");
//     printf("ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt bgra -s %ux%u -i %s -f image2 output.png\n",
//            fb_info->width, fb_info->height, output_file);

//     return 0;
// }

#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC   _IOW('F', 0x20, __u32)
#endif

int main(int argc, char **argv)
{
 int fd;
 drmModeConnector *conn;
 drmModeRes *res;
 uint32_t conn_id;
 uint32_t crtc_id;

    // 1. 打开设备
 fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

    // 2. 获得 crtc 和 connector 的 id
 res = drmModeGetResources(fd);
 crtc_id = res->crtcs[0];
 conn_id = res->connectors[0];

    // 3. 获得 connector
 conn = drmModeGetConnector(fd, conn_id);
 buf.width = conn->modes[0].hdisplay;
 buf.height = conn->modes[0].vdisplay;

    // 4. 创建 framebuffer
 modeset_create_fb(fd, &buf);

    // 5. Sets a CRTC configuration，这之后就会开始在 crtc0 + connector0 pipeline 上进行以 mode0 输出显示
 drmModeSetCrtc(fd, crtc_id, buf.fb_id, 0, 0, &conn_id, 1, &conn->modes[0]);

 getchar();

 // 6. cleanup
 ...

 return 0;
}