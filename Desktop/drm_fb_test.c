#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <stdint.h>

typedef struct {
    int fd;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder;
    drmModeModeInfo mode;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint32_t size;
    void *map;
} drm_dev_t;

// 初始化 DRM 設備
int drm_init(drm_dev_t *dev, const char *device) {
    int i;
    
    // 打開 DRM 設備
    dev->fd = open(device, O_RDWR | O_CLOEXEC);
    if (dev->fd < 0) {
        perror("Failed to open DRM device");
        return -1;
    }
    
    // 獲取 DRM 資源
    dev->resources = drmModeGetResources(dev->fd);
    if (!dev->resources) {
        fprintf(stderr, "Failed to get DRM resources\n");
        close(dev->fd);
        return -1;
    }
    
    // 尋找有效的 connector
    dev->connector = NULL;
    for (i = 0; i < dev->resources->count_connectors; i++) {
        dev->connector = drmModeGetConnector(dev->fd, 
            dev->resources->connectors[i]);
        if (dev->connector->connection == DRM_MODE_CONNECTED) {
            break;
        }
        drmModeFreeConnector(dev->connector);
        dev->connector = NULL;
    }
    
    if (!dev->connector) {
        fprintf(stderr, "No connected connector found\n");
        drmModeFreeResources(dev->resources);
        close(dev->fd);
        return -1;
    }
    
    // 使用首選模式
    dev->mode = dev->connector->modes[0];
    printf("Display mode: %dx%d @ %d Hz\n", 
        dev->mode.hdisplay, dev->mode.vdisplay, dev->mode.vrefresh);
    
    // 獲取 encoder
    dev->encoder = drmModeGetEncoder(dev->fd, dev->connector->encoder_id);
    if (!dev->encoder) {
        fprintf(stderr, "Failed to get encoder\n");
        drmModeFreeConnector(dev->connector);
        drmModeFreeResources(dev->resources);
        close(dev->fd);
        return -1;
    }
    
    dev->crtc_id = dev->encoder->crtc_id;
    
    return 0;
}

// 創建 framebuffer
int drm_create_fb(drm_dev_t *dev) {
    struct drm_mode_create_dumb create_req = {0};
    struct drm_mode_map_dumb map_req = {0};
    
    // 創建 dumb buffer
    create_req.width = dev->mode.hdisplay;
    create_req.height = dev->mode.vdisplay;
    create_req.bpp = 32;  // 32 bits per pixel (XRGB8888)
    
    if (drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req) < 0) {
        perror("Failed to create dumb buffer");
        return -1;
    }
    
    dev->handle = create_req.handle;
    dev->pitch = create_req.pitch;
    dev->size = create_req.size;
    
    // 添加 framebuffer
    if (drmModeAddFB(dev->fd, dev->mode.hdisplay, dev->mode.vdisplay,
                     24, 32, dev->pitch, dev->handle, &dev->fb_id)) {
        perror("Failed to add framebuffer");
        return -1;
    }
    
    // 映射 buffer 到用戶空間
    map_req.handle = dev->handle;
    if (drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req)) {
        perror("Failed to map dumb buffer");
        return -1;
    }
    
    dev->map = mmap(0, dev->size, PROT_READ | PROT_WRITE, 
                    MAP_SHARED, dev->fd, map_req.offset);
    if (dev->map == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        return -1;
    }
    
    // 清空 framebuffer (黑色)
    memset(dev->map, 0, dev->size);
    
    return 0;
}

// 設置顯示模式
int drm_set_mode(drm_dev_t *dev) {
    if (drmModeSetCrtc(dev->fd, dev->crtc_id, dev->fb_id, 0, 0,
                       &dev->connector->connector_id, 1, &dev->mode)) {
        perror("Failed to set CRTC");
        return -1;
    }
    return 0;
}

// 繪製測試圖案
void draw_test_pattern(drm_dev_t *dev) {
    uint32_t *fb = (uint32_t *)dev->map;
    int width = dev->mode.hdisplay;
    int height = dev->mode.vdisplay;
    
    // 繪製彩色條紋
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t color;
            int section = (x * 8) / width;
            
            switch(section) {
                case 0: color = 0xFFFFFFFF; break; // 白色
                case 1: color = 0xFFFFFF00; break; // 黃色
                case 2: color = 0xFF00FFFF; break; // 青色
                case 3: color = 0xFF00FF00; break; // 綠色
                case 4: color = 0xFFFF00FF; break; // 洋紅
                case 5: color = 0xFFFF0000; break; // 紅色
                case 6: color = 0xFF0000FF; break; // 藍色
                case 7: color = 0xFF000000; break; // 黑色
                default: color = 0xFF808080; break;
            }
            
            fb[y * width + x] = color;
        }
    }
    
    // 繪製一個白色方框
    int box_size = 200;
    int box_x = (width - box_size) / 2;
    int box_y = (height - box_size) / 2;
    
    for (int y = box_y; y < box_y + box_size; y++) {
        for (int x = box_x; x < box_x + box_size; x++) {
            if (x == box_x || x == box_x + box_size - 1 ||
                y == box_y || y == box_y + box_size - 1) {
                fb[y * width + x] = 0xFFFFFFFF;
            }
        }
    }
}

// 清理資源
void drm_cleanup(drm_dev_t *dev) {
    struct drm_mode_destroy_dumb destroy_req = {0};
    
    if (dev->map) {
        munmap(dev->map, dev->size);
    }
    
    if (dev->fb_id) {
        drmModeRmFB(dev->fd, dev->fb_id);
    }
    
    if (dev->handle) {
        destroy_req.handle = dev->handle;
        drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);
    }
    
    if (dev->encoder) {
        drmModeFreeEncoder(dev->encoder);
    }
    
    if (dev->connector) {
        drmModeFreeConnector(dev->connector);
    }
    
    if (dev->resources) {
        drmModeFreeResources(dev->resources);
    }
    
    if (dev->fd >= 0) {
        close(dev->fd);
    }
}

int main(int argc, char *argv[]) {
    drm_dev_t dev = {0};
    const char *device = "/dev/dri/card0";
    
    if (argc > 1) {
        device = argv[1];
    }
    
    printf("Opening DRM device: %s\n", device);
    
    // 初始化 DRM
    if (drm_init(&dev, device) < 0) {
        fprintf(stderr, "Failed to initialize DRM\n");
        return 1;
    }
    
    // 創建 framebuffer
    if (drm_create_fb(&dev) < 0) {
        fprintf(stderr, "Failed to create framebuffer\n");
        drm_cleanup(&dev);
        return 1;
    }
    
    // 設置顯示模式
    if (drm_set_mode(&dev) < 0) {
        fprintf(stderr, "Failed to set display mode\n");
        drm_cleanup(&dev);
        return 1;
    }
    
    printf("Framebuffer created successfully\n");
    printf("Size: %dx%d, Pitch: %d, Total size: %d bytes\n",
           dev.mode.hdisplay, dev.mode.vdisplay, dev.pitch, dev.size);
    
    // 繪製測試圖案
    draw_test_pattern(&dev);
    
    printf("Test pattern drawn. Press Enter to exit...\n");
    getchar();
    
    // 清理
    drm_cleanup(&dev);
    printf("Cleanup complete\n");
    
    return 0;
}
