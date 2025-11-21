#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

// Pbuffer 表面 (Surface) 的維度
#define WIDTH  640
#define HEIGHT 480

// 儲存 EGL 相關狀態
typedef struct {
    EGLDisplay display;
    EGLConfig  config;
    EGLContext context;
    EGLSurface surface;
} EglState;

// 初始化 EGL Pbuffer (離屏渲染)
int init_egl(EglState *state) {
    EGLint major, minor;

    // 1. 取得 EGL Display
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state->display == EGL_NO_DISPLAY) {
        printf("EGL: Failed to get display\n");
        return -1;
    }

    // 2. 初始化 EGL
    if (!eglInitialize(state->display, &major, &minor)) {
        printf("EGL: Failed to initialize\n");
        return -1;
    }
    printf("EGL Version: %d.%d\n", major, minor);

    // 3. 配置屬性 (Config Attributes)
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, // 我們要 Pbuffer
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, // GLES 2.0
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8, // RGBA 8888
        EGL_NONE
    };

    // 4. 選擇 EGL Config
    EGLint num_configs;
    if (!eglChooseConfig(state->display, config_attribs, &state->config, 1, &num_configs) || num_configs < 1) {
        printf("EGL: Failed to choose config\n");
        return -1;
    }

    // 5. 創建 Pbuffer Surface
    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, WIDTH,
        EGL_HEIGHT, HEIGHT,
        EGL_NONE
    };
    state->surface = eglCreatePbufferSurface(state->display, state->config, pbuffer_attribs);
    if (state->surface == EGL_NO_SURFACE) {
        printf("EGL: Failed to create pbuffer surface (Error: 0x%x)\n", eglGetError());
        return -1;
    }

    // 6. 創建 EGL Context
    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, // GLES 2.0
        EGL_NONE
    };
    state->context = eglCreateContext(state->display, state->config, EGL_NO_CONTEXT, context_attribs);
    if (state->context == EGL_NO_CONTEXT) {
        printf("EGL: Failed to create context\n");
        return -1;
    }

    // 7. 綁定 Context 和 Surface
    if (!eglMakeCurrent(state->display, state->surface, state->surface, state->context)) {
        printf("EGL: Failed to make current\n");
        return -1;
    }

    printf("EGL Pbuffer initialized successfully.\n");
    return 0;
}

// 執行 GLES 繪圖 (清除為紅色)
void render_scene() {
    glViewport(0, 0, WIDTH, HEIGHT);
    
    // 設置清除顏色為紅色 (R=1.0, G=0.0, B=0.0, A=1.0)
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    
    // 執行清除
    glClear(GL_COLOR_BUFFER_BIT);
    
    // **重要**: 確保 GLES 命令已發送
    // 使用 glFlush() 確保命令提交，但 glReadPixels() 本身會隱含刷新
    // glFlush(); 
}

// 將 RGBA 緩衝區儲存為 PPM 檔案 (P6 格式, 僅 RGB)
void save_to_ppm(const char *filename, GLubyte *buffer, int width, int height) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Failed to open file %s\n", filename);
        return;
    }

    // 寫入 PPM 標頭 (P6 = 二進位 RGB)
    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    // 寫入像素資料 (RGBA -> RGB)
    for (int i = 0; i < width * height; i++) {
        // buffer 是 RGBA (4 bytes), PPM P6 是 RGB (3 bytes)
        fwrite(buffer + (i * 4), 1, 3, fp);
    }

    fclose(fp);
    printf("Saved pixels to %s\n", filename);
}


int main() {
    EglState egl_state;

    // 1. 初始化 EGL 和 GLES
    if (init_egl(&egl_state) != 0) {
        return -1;
    }

    // 2. 執行繪圖
    render_scene();

    // 3. 準備讀取像素
    // 分配 CPU 記憶體緩衝區 (寬 * 高 * 4 (RGBA))
    GLubyte *pixel_buffer = (GLubyte *)malloc(WIDTH * HEIGHT * 4);
    if (!pixel_buffer) {
        printf("Failed to allocate pixel buffer\n");
        return -1;
    }

    printf("Calling glReadPixels...\n");

    // 設置像素儲存對齊方式 (1-byte alignment 是最安全的)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // ************* 核心：glReadPixels *************
    // (x, y, width, height, format, type, data_buffer)
    // 讀取整個 Pbuffer (從 0,0 到 WIDTH,HEIGHT)
    // 格式為 RGBA, 類型為 UNSIGNED_BYTE
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixel_buffer);
    
    // 檢查 GL 錯誤
    EGLint err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("glReadPixels failed. GL Error: 0x%x\n", err);
        free(pixel_buffer);
        return -1;
    }
    
    printf("glReadPixels finished.\n");

    // 4. 驗證結果
    // 檢查左上角 (0,0) 的像素是否為紅色 (R=255, G=0, B=0)
    printf("Top-left pixel (RGBA): %d, %d, %d, %d\n", 
           pixel_buffer[0], 
           pixel_buffer[1], 
           pixel_buffer[2],
           pixel_buffer[3]);

    // 5. 儲存為 PPM 檔案
    save_to_ppm("kv260_output.ppm", pixel_buffer, WIDTH, HEIGHT);

    // 6. 清理
    free(pixel_buffer);
    eglMakeCurrent(egl_state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_state.display, egl_state.surface);
    eglDestroyContext(egl_state.display, egl_state.context);
    eglTerminate(egl_state.display);

    return 0;
}