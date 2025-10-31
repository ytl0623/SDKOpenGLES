#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// FFmpeg headers (must be wrapped in extern "C")
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include "XLinuxPodium.h"
#include "mp4.h" // Re-using the header
#include "XGLSLCompile.h"
#include "XEGLIntf.h"
#include "XMatrixAPI.h"

#define SCENE_WIDTH 800
#define SCENE_HEIGHT 600

using std::string;

// --- OpenGL Globals ---
string resourceDirectory = "Supportingfiles/";
string vertexShaderFilename = "mp4.vert";
string fragmentShaderFilename = "mp4.frag";

GLuint programID;
GLint iLocPosition = -1;
GLint iLocTexture = -1;
GLint iLocMVP = -1;

GLuint id_y; // Only one texture needed for grayscale (Y plane)
GLuint textureUniformY;

// --- FFmpeg Globals ---
AVFormatContext *pFormatCtx = NULL;
AVCodecContext  *pCodecCtx = NULL;
AVFrame         *pFrame = NULL;
int             videoStreamIndex = -1;


bool prepareGraphics(int width, int height) {
    printf("setupGraphics(%d, %d)\n", width, height);
    string vertexShaderPath = resourceDirectory + vertexShaderFilename;
    string fragmentShaderPath = resourceDirectory + fragmentShaderFilename;

    GLuint vertexShaderID = 0;
    GLuint fragmentShaderID = 0;

    Shader::processShader(&vertexShaderID, vertexShaderPath.c_str(), GL_VERTEX_SHADER);
    printf("Vertex Shader ID = %d\n", vertexShaderID);
    Shader::processShader(&fragmentShaderID, fragmentShaderPath.c_str(), GL_FRAGMENT_SHADER);
    printf("Fragment Shader ID = %d\n", fragmentShaderID);

    programID = glCreateProgram();
    if (programID == 0) {
        printf("Could not create program.\n");
        return false;
    }

    glAttachShader(programID, vertexShaderID);
    glAttachShader(programID, fragmentShaderID);
    glLinkProgram(programID);
    glUseProgram(programID);

    iLocPosition = glGetAttribLocation(programID, "av4position");
    iLocTexture = glGetAttribLocation(programID, "av3colour");
    iLocMVP = glGetUniformLocation(programID, "mvp");
    textureUniformY = glGetUniformLocation(programID, "tex_y");

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glUseProgram(programID);
    glEnableVertexAttribArray(iLocPosition);
    glEnableVertexAttribArray(iLocTexture);
    glVertexAttribPointer(iLocPosition, 2, GL_FLOAT, GL_FALSE, 0, vertexVertices);
    glVertexAttribPointer(iLocTexture, 2, GL_FLOAT, GL_FALSE, 0, textureVertices);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Only need to set up one texture for the Y plane (luminance/grayscale)
    glGenTextures(1, &id_y);
    glBindTexture(GL_TEXTURE_2D, id_y);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

void GraphicsUpdate() {
    // We can use a simple identity matrix as we are drawing a 2D quad
    XMatrixAPI modelViewPerspective = XMatrixAPI::identityXMatrixAPI;
    glUniformMatrix4fv(iLocMVP, 1, GL_FALSE, modelViewPerspective.getAsArray());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- FFmpeg: Read and decode one frame ---
    AVPacket packet;
    if (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            if (avcodec_send_packet(pCodecCtx, &packet) == 0) {
                while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                    // Frame successfully decoded, now upload it to GPU

                    // Activate texture unit 0
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, id_y);
                    
                    // Upload the Y plane from the decoded frame.
                    // Note: pFrame->linesize[0] is the stride, which might not be equal to width.
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, pCodecCtx->width, pCodecCtx->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pFrame->data[0]);
                    
                    glUniform1i(textureUniformY, 0);

                    // Draw the quad
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }
            }
        }
        av_packet_unref(&packet);
    } else {
        // End of video, seek back to the beginning to loop
        av_seek_frame(pFormatCtx, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    }
}

int main(int argc, char* argv[]) {
    // --- FFmpeg Initialization ---
    string mp4File = resourceDirectory + "6026408-sd_640_360_24fps.mp4"; // !!! IMPORTANT: Change this to your video file !!!

    if (avformat_open_input(&pFormatCtx, mp4File.c_str(), NULL, NULL) != 0) {
        printf("Error: Could not open video file %s\n", mp4File.c_str());
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Error: Could not find stream information\n");
        return -1;
    }

    AVCodec* pCodec = NULL;
    videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
    if (videoStreamIndex < 0 || pCodec == NULL) {
        printf("Error: Could not find a video stream or a decoder\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        printf("Error: Could not allocate codec context\n");
        return -1;
    }
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStreamIndex]->codecpar);

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Error: Could not open codec\n");
        return -1;
    }

    pFrame = av_frame_alloc();
    if (!pFrame) {
        printf("Error: Could not allocate video frame\n");
        return -1;
    }
    
    int videoWidth = pCodecCtx->width;
    int videoHeight = pCodecCtx->height;
    printf("Video Details: %s, %dx%d\n", pCodec->long_name, videoWidth, videoHeight);

    // --- EGL and Window Initialization ---
    XPodium *podium = XPodium::getHandler();
    podium->prepareWindow(videoWidth, videoHeight); // Use video dimensions for window
    CoreEGL::initializeEGL(CoreEGL::OPENGLES2);
    eglMakeCurrent(CoreEGL::display, CoreEGL::surface, CoreEGL::surface, CoreEGL::context);
    
    prepareGraphics(videoWidth, videoHeight);

    // --- Main Loop ---
    bool end = false;
    while (!end) {
        if (podium->checkWindow() != XPodium::WINDOW_IDLE) {
            end = true;
        }
        GraphicsUpdate();
        eglSwapBuffers(CoreEGL::display, CoreEGL::surface);
    }

    // --- Cleanup ---
    // FFmpeg
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);
    
    // EGL and Window
    CoreEGL::terminateEGL();
    podium->destroyWindow();
    delete podium;

    return 0;
}