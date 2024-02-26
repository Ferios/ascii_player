#include <stdio.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <SDL.h>
#include <SDL_ttf.h>
#include "SDL_FontCache.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_PTSIZE  9
#define WIDTH   480
#define HEIGHT  360

using namespace std;

typedef struct SDLContext
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;
    FC_Font* fc_font;
}TSDLContext;

typedef struct FfmpegContext
{
  AVCodec* codec;
  AVStream* stream;
  AVFrame* decframe;
  AVPacket* pkt;
  AVFormatContext* input_ctx;
  AVCodecContext* codec_ctx;

  vector<uint8_t> framebuf;

  char* file = nullptr;
  bool end_of_stream = false;
  int got_image = 0;
  int stream_idx;
}TFfmpegCtx;

static const char characters[] = "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. ";
static const char* font_name = "SpaceMono-Regular.ttf";

int init_ffmpeg(TFfmpegCtx* ffmpegctx)
{
  int ret = 0;

  ffmpegctx->codec = nullptr;
  ffmpegctx->stream = nullptr;
  ffmpegctx->decframe = nullptr;
  
  ffmpegctx->end_of_stream = false;
  ffmpegctx->got_image = 0;

  // Open file context
  ffmpegctx->input_ctx = nullptr;
  if (avformat_open_input(&(ffmpegctx->input_ctx), ffmpegctx->file, nullptr, nullptr) < 0) 
  {
      std::cerr << "Avformat open error: " << ret;
      return 2;
  }
  // Get input stream info
  if (avformat_find_stream_info(ffmpegctx->input_ctx, nullptr) < 0) 
  {
      std::cerr << "Find stream info error: " << ret;
      return 2;
  }

  // Detect video stream
  ffmpegctx->stream_idx = av_find_best_stream(ffmpegctx->input_ctx, AVMEDIA_TYPE_VIDEO, 
                                                -1, -1, (const AVCodec**)(&(ffmpegctx->codec)), 0);
  if (ffmpegctx->stream_idx < 0) 
  {
      std::cerr << "Find best stream error: " << ret;
      return 2;
  }
  
  ffmpegctx->stream = ffmpegctx->input_ctx->streams[ffmpegctx->stream_idx];
  
    ffmpegctx->codec_ctx = avcodec_alloc_context3(ffmpegctx->codec);
    if (!ffmpegctx->codec_ctx) {
        std::cout << "Error allocating codec context" << std::endl;
        avformat_free_context(ffmpegctx->input_ctx);
        return 1;
    }

    ret = avcodec_parameters_to_context(ffmpegctx->codec_ctx, ffmpegctx->stream->codecpar);
    if (ret < 0) {
        std::cout << "Error setting codec context parameters: " << ret << std::endl;
        avcodec_free_context(&ffmpegctx->codec_ctx);
        avformat_free_context(ffmpegctx->input_ctx);
        return 1;
    }

  // Open decoder context
  if (avcodec_open2(ffmpegctx->codec_ctx, ffmpegctx->codec, nullptr) < 0) {
      std::cerr << "Av codec open error: " << ret;
      return 2;
  }

  ffmpegctx->pkt = av_packet_alloc();

  // Print video info
  cout
      << "format: " << ffmpegctx->input_ctx->iformat->name << endl
      << "codec: "  << ffmpegctx->codec->name << endl
      << "size:   " << ffmpegctx->stream->codecpar->width << 'x' << ffmpegctx->stream->codecpar->height << endl
      << "fps:    " << av_q2d(ffmpegctx->stream->r_frame_rate) << " [fps]" << endl
      << "length: " << av_rescale_q(ffmpegctx->stream->duration, ffmpegctx->stream->time_base, {1,1000}) / 1000. << " [sec]" << endl
      << "pixfmt: " << av_get_pix_fmt_name((AVPixelFormat)ffmpegctx->stream->codecpar->format) << endl
      << "frame:  " << ffmpegctx->stream->nb_frames << endl
      << flush;
  
  return 0;
}

static void cleanup(int exitcode)
{
    TTF_Quit();
    SDL_Quit();
    exit(exitcode);
}

int init_sdl(TSDLContext *sdlctx)
{
    sdlctx->window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if(sdlctx->window == NULL)
    {
        SDL_Log("Failed to create window.\n");
        return 2;
    }

    sdlctx->renderer = SDL_CreateRenderer(sdlctx->window, -1, SDL_RENDERER_PRESENTVSYNC);
    if(sdlctx->renderer == NULL)
    {
        SDL_Log("Failed to create renderer.\n");
        return 3;
    }
    
    sdlctx->fc_font = FC_CreateFont();  
    FC_LoadFont(sdlctx->fc_font, sdlctx->renderer, font_name, 9, FC_MakeColor(255,255,255,255), TTF_STYLE_NORMAL);
                  
    return 0;
}

int main(int argc, char *argv[])
{
    TSDLContext sdlctx = {0};
    TFfmpegCtx ffmpegctx = {0};

    int ret = 0;
    bool done = false;
    char* ascii_art = nullptr;
    const float coeff = 255 / (float)(sizeof(characters) - 1);

    if (argc < 2) 
    {
        std::cout << "Usage: ascii_player <file>" << std::endl;
        return 1;
    }
    ffmpegctx.file = argv[1];

    if (init_ffmpeg(&ffmpegctx))
    {
        return -1;
    }

    init_sdl(&sdlctx);

    /* Allocate space for frame decoder */
    ffmpegctx.decframe = av_frame_alloc();
    
    /* Calculate frame time */
    const auto frametime = std::chrono::milliseconds(1000 / (int)av_q2d(ffmpegctx.stream->r_frame_rate));

    /* Allocate ASCII frame */
    ascii_art = (char*)malloc(ffmpegctx.stream->codecpar->width / 4 + 1);

    /* Update window size */
    const int winheight = (ffmpegctx.stream->codecpar->height) * 1.77;
    const int winwidth = (ffmpegctx.stream->codecpar->width / 4) * FC_GetWidth(sdlctx.fc_font, "%s", "c");
    if (winwidth != WIDTH || winheight!= HEIGHT)
    {
        SDL_SetWindowSize(sdlctx.window, winwidth, winheight);
    }

    do
    {
        auto now = std::chrono::system_clock::now();
        auto start = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

        while (SDL_PollEvent(&sdlctx.event))
        {
          switch (sdlctx.event.type) 
          {
              case SDL_KEYDOWN:
              case SDL_QUIT:
                  done = true;
                  break;
              default:
                  break;
          }
        }

        /* Read next packet */
        if (!ffmpegctx.end_of_stream) 
        {
            ret = av_read_frame(ffmpegctx.input_ctx, ffmpegctx.pkt);
            if (ret < 0 && ret != AVERROR_EOF) {
                std::cerr << "read frame error: " << ret;
                break;
            }
            

            if (ret == 0 && ffmpegctx.pkt->stream_index != ffmpegctx.stream_idx)
            {
                av_packet_unref(ffmpegctx.pkt);
                continue;
            }
            
            ffmpegctx.end_of_stream = (ret == AVERROR_EOF);
        }
        
        /* Make a final dummy packet to end loop */
        if (ffmpegctx.end_of_stream) 
        {
            ffmpegctx.pkt = av_packet_alloc();
            ffmpegctx.pkt->data = nullptr;
            ffmpegctx.pkt->size = 0;
        }
        
        /* Decode packet and see if we have a frame */

        ret = avcodec_send_packet(ffmpegctx.codec_ctx, ffmpegctx.pkt);
        if (ret < 0) {
            fprintf(stderr, "Error sending a packet for decoding\n");
            exit(1);
        }

        ffmpegctx.got_image = 1;
        ret = avcodec_receive_frame(ffmpegctx.codec_ctx, ffmpegctx.decframe);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            ffmpegctx.got_image = 0;
            av_packet_unref(ffmpegctx.pkt);
            continue;
        }
        else if (ret < 0) {
            ffmpegctx.got_image = 0;
            fprintf(stderr, "Error during decoding\n");
            break;
        }

        av_packet_unref(ffmpegctx.pkt);

        /* Reset viewport */
        SDL_SetRenderDrawColor(sdlctx.renderer, 0x00, 0x00, 0x00, 0x00);
        SDL_RenderClear(sdlctx.renderer);

        /* Average values of pixels within a tile of a frame and store them in an array */
        for (int heightIdx = 0; heightIdx < ffmpegctx.decframe->height; heightIdx+=4) 
        {
            for (int widthIdx = 0, cellId = 0; widthIdx < ffmpegctx.decframe->width; widthIdx+=4, cellId++) 
            {
                /* For simplicity tiling is hardcoded */
                const uint8_t* line1 = &ffmpegctx.decframe->data[0][heightIdx * ffmpegctx.decframe->linesize[0] + widthIdx];
                const uint8_t* line2 = line1 + ffmpegctx.decframe->linesize[0];
                const uint8_t* line3 = line2 + ffmpegctx.decframe->linesize[0];
                const uint8_t* line4 = line3 + ffmpegctx.decframe->linesize[0];
                const float intensity = (line1[0] + line1[1] + line1[2] + line1[3] +
                                        line2[0] + line2[1] + line2[2] + line2[3] +
                                        line3[0] + line3[1] + line3[2] + line3[3] +
                                        line4[0] + line4[1] + line4[2] + line4[3]) / 16;
                const int character_index = intensity / coeff;
                ascii_art[cellId] = characters[character_index];
            }
            ascii_art[ffmpegctx.decframe->width / 4] = '\n';

            /* Draw line by line to control lineheight */
            FC_Draw(sdlctx.fc_font, sdlctx.renderer, 0, heightIdx * 1.75, "%s", ascii_art);
        }

        /* Update viewport */
        SDL_RenderPresent(sdlctx.renderer);

        /* Wait until we need to present next frame */
        now = std::chrono::system_clock::now();
        auto end = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        if ((end - start) < frametime)
        {
            std::this_thread::sleep_for(frametime - (end - start));
        }
    }
    while ((!ffmpegctx.end_of_stream || ffmpegctx.got_image) && (done == false));
    
    /* Release ASCII frame */
    free(ascii_art);

    /* De-init ffmpeg */
    av_frame_free(&ffmpegctx.decframe);
    avcodec_close(ffmpegctx.codec_ctx);
    avformat_close_input(&ffmpegctx.input_ctx);

    /* De-init SDL */
    FC_FreeFont(sdlctx.fc_font);
    cleanup(0);

    /* Not reached, but fixes compiler warnings */
    return 0;
}

