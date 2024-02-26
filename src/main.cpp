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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_PTSIZE  9
#define WIDTH   480
#define HEIGHT  360

using namespace std;

typedef enum {
    RENDER_LATIN1,
    RENDER_UTF8,
    RENDER_UNICODE
} RenderType;

typedef enum
{
    TextRenderSolid,
    TextRenderShaded,
    TextRenderBlended
} TextRenderMethod;

typedef struct {
    SDL_Texture *caption;
    SDL_Rect captionRect;
    SDL_Texture *message;
    SDL_Rect messageRect;
} Scene;

typedef struct SDLContext
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    SDL_Surface *text;
    Scene scene;
    int ptsize;
    SDL_Color white;
    SDL_Color black;
    SDL_Color *forecol;
    SDL_Color *backcol;
    SDL_Event event;
    TextRenderMethod rendermethod;
    RenderType rendertype;
    int renderstyle;
    int outline;
    int hinting;
    int kerning;
    int wrap;
    int dump;
    char *message, string[128];
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


void save_frame_ascii(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    const float char_count = (float)(sizeof(characters)-1);
    
    // Open file
    sprintf(szFilename, "frame%d.txt", iFrame);
    pFile=fopen(szFilename, "w");
    if(pFile==NULL)
        return;

    // Write ascii
    for (int heightIdx = 0; heightIdx < height; heightIdx++) 
    {
      for (int widthIdx = 0; widthIdx < width; widthIdx++) 
      {
          const int intensity = pFrame->data[0][heightIdx * pFrame->linesize[0] + widthIdx];
          const int character_index = intensity / (255 / char_count);

          fputc(characters[character_index], pFile);
      }
      fputc('\n', pFile);
    }
    // Close file
    fclose(pFile);
}

void save_frame_luminance(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write ascii
    for (int heightIdx = 0; heightIdx < height; heightIdx++) 
    {
      for (int widthIdx = 0; widthIdx < width; widthIdx++) 
      {
        fwrite(pFrame->data[0]+heightIdx*pFrame->linesize[0] + widthIdx, 1, 1, pFile);
        fwrite(pFrame->data[0]+heightIdx*pFrame->linesize[0] + widthIdx, 1, 1, pFile);
        fwrite(pFrame->data[0]+heightIdx*pFrame->linesize[0] + widthIdx, 1, 1, pFile);
      }
    }
    // Close file
    fclose(pFile);
}

void save_frame_rgb(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
}

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


static void draw_scene(SDL_Renderer *renderer, Scene *scene)
{
    /* Clear the background to background color */
    SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(renderer);

    SDL_RenderCopy(renderer, scene->caption, NULL, &scene->captionRect);
    SDL_RenderCopy(renderer, scene->message, NULL, &scene->messageRect);
    SDL_RenderPresent(renderer);
}

static void cleanup(int exitcode)
{
    TTF_Quit();
    SDL_Quit();
    exit(exitcode);
}

int draw_text(char* message, TSDLContext *sdlctx)
{
    switch (sdlctx->rendertype) {
        case RENDER_LATIN1:
            switch (sdlctx->rendermethod) {
            case TextRenderSolid:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderText_Solid_Wrapped(sdlctx->font, message, *sdlctx->forecol, 0);
                } else {
                    sdlctx->text = TTF_RenderText_Solid(sdlctx->font, message, *(sdlctx->forecol));
                }
                break;
            case TextRenderShaded:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderText_Shaded_Wrapped(sdlctx->font, message, *(sdlctx->forecol), *(sdlctx->backcol), 0);
                } else {
                    sdlctx->text = TTF_RenderText_Shaded(sdlctx->font, message, *(sdlctx->forecol), *(sdlctx->backcol));
                }
                break;
            case TextRenderBlended:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderText_Blended_Wrapped(sdlctx->font, message, *(sdlctx->forecol), 0);
                } else {
                    sdlctx->text = TTF_RenderText_Blended(sdlctx->font, message, *(sdlctx->forecol));
                }
                break;
            }
            break;

        case RENDER_UTF8:
            switch (sdlctx->rendermethod) {
            case TextRenderSolid:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUTF8_Solid_Wrapped(sdlctx->font, message, *(sdlctx->forecol), 0);
                } else {
                    sdlctx->text = TTF_RenderUTF8_Solid(sdlctx->font, message, *(sdlctx->forecol));
                }
                break;
            case TextRenderShaded:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUTF8_Shaded_Wrapped(sdlctx->font, message, *(sdlctx->forecol), *(sdlctx->backcol), 0);
                } else {
                    sdlctx->text = TTF_RenderUTF8_Shaded(sdlctx->font, message, *(sdlctx->forecol), *(sdlctx->backcol));
                }
                break;
            case TextRenderBlended:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUTF8_Blended_Wrapped(sdlctx->font, message, *(sdlctx->forecol), 0);
                } else {
                    sdlctx->text = TTF_RenderUTF8_Blended(sdlctx->font, message, *(sdlctx->forecol));
                }
                break;
            }
            break;

        case RENDER_UNICODE:
        {
            Uint16 *unicode_text = SDL_iconv_utf8_ucs2(message);
            switch (sdlctx->rendermethod) {
            case TextRenderSolid:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUNICODE_Solid_Wrapped(sdlctx->font, unicode_text, *(sdlctx->forecol), 0);
                } else {
                    sdlctx->text = TTF_RenderUNICODE_Solid(sdlctx->font, unicode_text, *(sdlctx->forecol));
                }
                break;
            case TextRenderShaded:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUNICODE_Shaded_Wrapped(sdlctx->font, unicode_text, *(sdlctx->forecol), *(sdlctx->backcol), 0);
                } else {
                    sdlctx->text = TTF_RenderUNICODE_Shaded(sdlctx->font, unicode_text, *(sdlctx->forecol), *(sdlctx->backcol));
                }
                break;
            case TextRenderBlended:
                if (sdlctx->wrap) {
                    sdlctx->text = TTF_RenderUNICODE_Blended_Wrapped(sdlctx->font, unicode_text, *(sdlctx->forecol), 0);
                } else {
                    sdlctx->text = TTF_RenderUNICODE_Blended(sdlctx->font, unicode_text, *(sdlctx->forecol));
                }
                break;
            }
            SDL_free(unicode_text);
        }
        break;
    }
    
    if (sdlctx->text == NULL) {
        SDL_Log("Couldn't render text: %s\n", SDL_GetError());
        TTF_CloseFont(sdlctx->font);
        cleanup(2);
    }
    
    sdlctx->scene.messageRect.x = 0;
    sdlctx->scene.messageRect.y = 0;
    sdlctx->scene.messageRect.w = sdlctx->text->w;
    sdlctx->scene.messageRect.h = sdlctx->text->h;
    sdlctx->scene.message = SDL_CreateTextureFromSurface(sdlctx->renderer, sdlctx->text);

    /* Update window size to fit the surface */
    if (sdlctx->text->w != WIDTH || sdlctx->text->h != HEIGHT)
    {
        SDL_SetWindowSize(sdlctx->window, sdlctx->text->w, sdlctx->text->h);
    }

    SDL_FreeSurface(sdlctx->text);
    sdlctx->text = nullptr;

    draw_scene(sdlctx->renderer, &(sdlctx->scene));
    SDL_DestroyTexture(sdlctx->scene.message);
    sdlctx->scene.message = nullptr;

    return 0;
}

int init_sdl(TSDLContext *sdlctx)
{
    sdlctx->text = NULL;
    sdlctx->wrap = 1;
    sdlctx->white = { 0xFF, 0xFF, 0xFF, 0 };
    sdlctx->black = { 0x00, 0x00, 0x00, 0 };

    /* Look for special execution mode */
    sdlctx->dump = 0;
    /* Look for special rendering types */
    sdlctx->rendermethod = TextRenderShaded;
    sdlctx->renderstyle = TTF_STYLE_NORMAL;
    sdlctx->rendertype = RENDER_LATIN1;
    sdlctx->outline = 0;
    sdlctx->hinting = TTF_HINTING_NORMAL;
    sdlctx->kerning = 1;
    /* Default is black and white */
    sdlctx->forecol = &sdlctx->white;
    sdlctx->backcol = &sdlctx->black;

    /* Initialize the TTF library */
    if (TTF_Init() < 0) {
        SDL_Log("Couldn't initialize TTF: %s\n",SDL_GetError());
        SDL_Quit();
        return(2);
    }

    /* Open the font file with the requested point size */
    sdlctx->ptsize = DEFAULT_PTSIZE;
    sdlctx->font = TTF_OpenFont(font_name, sdlctx->ptsize);
    if (sdlctx->font == NULL) {
        SDL_Log("Couldn't load %d pt font from %s: %s\n",
                    sdlctx->ptsize, font_name, SDL_GetError());
        cleanup(2);
    }
    TTF_SetFontStyle(sdlctx->font, sdlctx->renderstyle);
    TTF_SetFontOutline(sdlctx->font, sdlctx->outline);
    TTF_SetFontKerning(sdlctx->font, sdlctx->kerning);
    TTF_SetFontHinting(sdlctx->font, sdlctx->hinting);

    /* Create a window */
    if (SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, 0, &sdlctx->window, &sdlctx->renderer) < 0) {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s\n", SDL_GetError());
        cleanup(2);
    }

    /* Show which font file we're looking at */
    SDL_snprintf(sdlctx->string, sizeof(sdlctx->string), "Font file: \n%s", font_name);  /* possible overflow */
    switch (sdlctx->rendermethod) {
    case TextRenderSolid:
        sdlctx->text = TTF_RenderText_Solid_Wrapped(sdlctx->font, sdlctx->string, *sdlctx->forecol, 0);
        break;
    case TextRenderShaded:
        sdlctx->text = TTF_RenderText_Shaded_Wrapped(sdlctx->font, sdlctx->string, *sdlctx->forecol, *sdlctx->backcol, 0);
        break;
    case TextRenderBlended:
        sdlctx->text = TTF_RenderText_Blended_Wrapped(sdlctx->font, sdlctx->string, *sdlctx->forecol, 0);
        break;
    }
    if (sdlctx->text != NULL) {
        sdlctx->scene.captionRect.x = 4;
        sdlctx->scene.captionRect.y = 4;
        sdlctx->scene.captionRect.w = sdlctx->text->w;
        sdlctx->scene.captionRect.h = sdlctx->text->h;
        sdlctx->scene.caption = SDL_CreateTextureFromSurface(sdlctx->renderer, sdlctx->text);
        SDL_FreeSurface(sdlctx->text);
    }
    
    SDL_Log("Font is generally %d big, and string is %d big\n",
                        TTF_FontHeight(sdlctx->font), sdlctx->text->h);
                        
    return 0;
}

int main(int argc, char *argv[])
{
    TSDLContext sdlctx = {0};
    TFfmpegCtx ffmpegctx = {0};

    int counter = 0;
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
    ascii_art = (char*)malloc((ffmpegctx.stream->codecpar->width / 4 + 1) * (ffmpegctx.stream->codecpar->height / 4));

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

        /* Average values of pixels within a tile of a frame and store them in an array */
        counter = 0;
        for (int heightIdx = 0; heightIdx < ffmpegctx.decframe->height; heightIdx+=4) 
        {
            for (int widthIdx = 0; widthIdx < ffmpegctx.decframe->width; widthIdx+=4) 
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
        
                ascii_art[counter++] = characters[character_index];
            }
            ascii_art[counter++] = '\n';
        }
        
        /* Draw ASCII art */
        draw_text(ascii_art, &sdlctx);

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
    if (sdlctx.text)
    {
        SDL_FreeSurface(sdlctx.text);
    }
    if (sdlctx.scene.message)
    {
        SDL_DestroyTexture(sdlctx.scene.message);
    }
    TTF_CloseFont(sdlctx.font);
    SDL_DestroyTexture(sdlctx.scene.caption);
    cleanup(0);

    /* Not reached, but fixes compiler warnings */
    return 0;
}

