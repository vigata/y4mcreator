#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>


////////////////////////////////////////////////////////////////////////////////////////////////
// The following is a heap/priority queue -> https://en.wikipedia.org/wiki/Heap_(data_structure)   
// implementation where the data stored is AVFrame pointers and the order of the nodes is 
// determined by a comparison function passed to the constructor avframe_heap_create.
//
// As written this is a min heap. Will order items from min to max, and top of the heap
// will always be the AVFrame with the minimum either coded_picture_number or display_picture_number 
//
// This allows us to reorder frames on decode (presentation->coded order) with flexibility and also 
// without assuming an underlying
// frame GOP structure. Extracting the min element from the heap is a cost of O(log n) n being the 
// maximum number of reference pictures in the underlying coded stream. We define a max capacity for the heap
// but we are unlikely to ever get there.
//
#define MAX_REFERENCE_FRAMES 1000

typedef struct {
    AVFrame *q[MAX_REFERENCE_FRAMES];
    int n;  // elements in the queue
    int (* cmp)(AVFrame *, AVFrame *);
} avframe_heap_t;

int avframe_heap_parent(int n) {
    if (n==1) 
        return -1;
    else 
        return n/2; 
}

int avframe_heap_young_child(int n) {
    return 2*n;
}

void avframe_heap_swap(avframe_heap_t *h, int a, int b) {
  AVFrame *tmp = h->q[a];
  h->q[a] = h->q[b];
  h->q[b] = tmp;
}

void avframe_heap_bubble_up( avframe_heap_t *h, int p ) {
  if( avframe_heap_parent(p) == -1 ) return;  //heap root
  
  if( h->cmp( h->q[avframe_heap_parent(p)], h->q[p]) >= 0 ) {
    avframe_heap_swap(h, p, avframe_heap_parent(p));
    avframe_heap_bubble_up(h, avframe_heap_parent(p));
  }
}

void avframe_heap_insert( avframe_heap_t *h, AVFrame *f ) {
    if( h->n >= MAX_REFERENCE_FRAMES ) 
        return;
    else {
        h->n += 1;
        h->q[h->n] = f;
        avframe_heap_bubble_up(h, h->n);
    }
}

// >0 if a>b
// ==0 if a==b
// <0 if a<b
int avframe_heap_cmp_coded(AVFrame *a, AVFrame *b) {
    return a->coded_picture_number - b->coded_picture_number;
}

int avframe_heap_cmp_display(AVFrame *a, AVFrame *b) {
  return a->display_picture_number - b->display_picture_number;
}

avframe_heap_t *avframe_heap_create(int (* cmp)(AVFrame *, AVFrame *)) {
    avframe_heap_t *r = malloc(sizeof(avframe_heap_t));
    r->n = 0;
    if(!cmp)
      r->cmp = avframe_heap_cmp_display;
    else
      r->cmp = cmp;
  
    return r;
}

void avframe_heap_destroy(avframe_heap_t *h) {
    free(h);
}

AVFrame *avframe_heap_peek_min( avframe_heap_t *h ) {
    if(h->n>0) {
        return h->q[1];
    } else {
        return 0;
    }
}
void avframe_heap_bubble_down( avframe_heap_t *h, int p ) {
  int c,i,min_index;
  c = avframe_heap_young_child(p);
  min_index = p;

  
  // figure out which one of the two children, if any is the minimum
  if( c<=h->n &&
     h->cmp( h->q[c], h->q[c+1] )<=0 &&
     h->cmp( h->q[c], h->q[p]   )<=0 ) {
    min_index = c;
  } else if( c+1 <= h->n &&
            h->cmp( h->q[c+1], h->q[c] )<=0 &&
            h->cmp( h->q[c+1], h->q[p] )<=0 ) {
    min_index = c+1;
  }
  
  if( min_index!=p) {
    avframe_heap_swap(h, p, min_index );
    avframe_heap_bubble_down(h, min_index);
  }
}

AVFrame *avframe_heap_get_min( avframe_heap_t *h ) {
  AVFrame *min = NULL;
    if(h->n>0) {
        min = h->q[1];
        h->q[1] = h->q[h->n];
        h->n--;
        avframe_heap_bubble_down(h,1);
    }

    return min;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  AVFormat/AVCodec routines follow
//
//
//
typedef struct {
    AVFormatContext *inctx;
    AVFormatContext *outctx;
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;

    int vidstream_idx;
} app_ctx_t;


#define APPERROR -1

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// open input file, setup decoder. Will pick the first video stream available on input
//
static int open_input(app_ctx_t *ctx, const char *fname) {
    int ret;
    unsigned int i;
  
    // open input AVFormatContext
    ctx->inctx = NULL;
    if ((ret=avformat_open_input(&(ctx->inctx), fname, NULL, NULL)) < 0){
        fprintf(stderr, "avformat_open_input failed\n");
        return ret;
    }

    // find streams in input
    if ((ret=avformat_find_stream_info(ctx->inctx, NULL)) < 0) {
        fprintf(stderr, "avformat_find_stream info failed\n");
        return ret;
    }

    // setup decoder for the first video stream in input if any
    AVStream *vidstream = NULL;
    for( i=0; i< ctx->inctx->nb_streams; i++ ) {
        AVStream *st = ctx->inctx->streams[i];
        if( st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ){
            vidstream = st;
            ctx->vidstream_idx = st->index;
            break;
        }
    }

    if (!vidstream) {
        fprintf(stderr, "couldn't find video stream in input\n");
        return APPERROR;
    }

    // find decoder
    AVCodec *dec = avcodec_find_decoder(vidstream->codecpar->codec_id);
    if(!dec) {
        fprintf(stderr, "failed to find decoder\n");
        return APPERROR;
    }

    // create context for decoder
    AVCodecContext *codec_ctx = avcodec_alloc_context3(dec);
    if(!codec_ctx) {
        fprintf(stderr, "couldn't allocate codec context\n");
        return APPERROR;
    }

    // copy parameters from stream onto codec context. potentially copies SPS and PPS from mp4
    ret = avcodec_parameters_to_context(codec_ctx, vidstream->codecpar);
    if(ret<0) {
        fprintf(stderr, "failed to copy decoder pars from stream to codec\n");
        return APPERROR;
    }

    // open decoder
    codec_ctx->framerate = av_guess_frame_rate( ctx->inctx, vidstream, NULL );
    ret = avcodec_open2(codec_ctx, dec, NULL);
    if(ret<0) {
        fprintf(stderr, "couldn't open decoder\n");
        return APPERROR;
    }

   
    ctx->dec_ctx = codec_ctx;

    // show input format
    av_dump_format(ctx->inctx, 0, fname, 0);
    return 0;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////
// open output file and setup y4m muxer. assumes only one video stream
//
static int open_output(app_ctx_t *ctx, const char *fname) {
    ctx->outctx = NULL;
    AVCodec *encoder;
    int ret;
    AVStream *out_stream;

    // allocates output format based on extension of file 'fname'
    avformat_alloc_output_context2(&ctx->outctx, NULL, NULL, fname);
    if(!ctx->outctx) {
        fprintf(stderr, "couldn't open output context\n");
        return APPERROR;
    }

    out_stream = avformat_new_stream(ctx->outctx, NULL);
    if(!out_stream) {
        fprintf(stderr, "can't allocation output stream\n");
        return APPERROR;
    }


    // allocate output stream for video of AV_CODEC_ID_WRAPPED_AVFRAME type
    encoder = avcodec_find_encoder(AV_CODEC_ID_WRAPPED_AVFRAME);
    if(!encoder) {
        fprintf(stderr, "couldn't allocate encoder\n");
        return APPERROR;
    }

    // allocate encoder context
    ctx->enc_ctx = avcodec_alloc_context3(encoder);
    if(!ctx->enc_ctx) {
        fprintf(stderr,"couldn't allocate encoder context\n");
        return APPERROR;
    }

    // we use the same format from the input
    ctx->enc_ctx->height = ctx->dec_ctx->height;
    ctx->enc_ctx->width  = ctx->dec_ctx->width;
    ctx->enc_ctx->sample_aspect_ratio = ctx->dec_ctx->sample_aspect_ratio;
    ctx->enc_ctx->pix_fmt = encoder->pix_fmts ? encoder->pix_fmts[0] : ctx->dec_ctx->pix_fmt;
    ctx->enc_ctx->time_base = av_inv_q(ctx->dec_ctx->framerate);

    // open the actual codec
    ret = avcodec_open2(ctx->enc_ctx, encoder, NULL);
    if(ret<0) {
        fprintf(stderr, "couldn't open video encoder\n");
        return ret;
    }

    // update parameters from encoder context to output stream
    ret = avcodec_parameters_from_context(out_stream->codecpar, ctx->enc_ctx);
    if(ret<0) {
        fprintf(stderr,"couldn't copy pars from encoder context to stream\n");
        return ret;
    }

    out_stream->time_base = ctx->enc_ctx->time_base;

    // show input format
    av_dump_format(ctx->outctx, 0, fname, 1);

    // open output file
    ret = avio_open(&ctx->outctx->pb, fname, AVIO_FLAG_WRITE );
    if(ret<0) {
        fprintf(stderr, "couldn't open file for writing\n");
        return ret;
    }

    // write header
    ret = avformat_write_header(ctx->outctx, NULL);
    if( ret<0 ) {
        fprintf(stderr, "coudln't write output header\n");
        return ret;
    }

    return 0;
    

}

///////////////////////////////////////////////////////////////////////////////////////
//  writes video frame to output muxer
// 
static int write_frame(app_ctx_t *ctx, AVFrame *frame) {
    AVPacket enc_packet;
    int got_frame;
    int ret;

    enc_packet.data = NULL;
    enc_packet.size = 0;
    av_init_packet(&enc_packet);
    ret = avcodec_encode_video2(ctx->enc_ctx, &enc_packet, frame,  &got_frame);
    if(ret<0) 
        return ret;
    
    // and mux
    int stream_index = 0; // we only have one output stream by design
    enc_packet.stream_index = stream_index;
    av_packet_rescale_ts(&enc_packet, ctx->enc_ctx->time_base, ctx->outctx->streams[stream_index]->time_base );
    ret = av_interleaved_write_frame(ctx->outctx, &enc_packet);
    return ret;
}


///////////////////////////////////
//  avframe_heap testing routines
//
AVFrame *test_alloc(int n) {
    AVFrame *r = av_frame_alloc();
    r->coded_picture_number = n;
    r->display_picture_number = n;
    return r;
}

void avframe_heap_testing() {
    avframe_heap_t *h = avframe_heap_create(NULL);
    avframe_heap_insert(h, test_alloc(3));
    avframe_heap_insert(h, test_alloc(4));
    avframe_heap_insert(h, test_alloc(9));
    avframe_heap_insert(h, test_alloc(7));
    avframe_heap_insert(h, test_alloc(84));
    avframe_heap_insert(h, test_alloc(1));
    avframe_heap_insert(h, test_alloc(7));
    avframe_heap_insert(h, test_alloc(16));

    AVFrame *f;
    while(avframe_heap_peek_min(h)) {
        f = avframe_heap_get_min(h);
      printf("%d\n", f->coded_picture_number);
        av_frame_free(&f);
    }
    avframe_heap_destroy(h);
}


///////////////////////////////////////////
// main
//
// use debug flag to enable testing. It will exercise the heap testing routines. Also outputs
// use coded_order=1 to output coded order [default]
// 
int main(int argc, char *argv[]) {
    int ret;
    char *urlout = "file:out.y4m";
    char *url;
    int debug = 0;

    if(argc==1) {
        printf("Usage: y4mcreator [file:input.mp4 | url] [out.mp4]\n");
        return 0;
    } else if(argc==2) {
        url = argv[1];
    } else if (argc==3) {
        url = argv[1];
        urlout = argv[2];
    } else if (argc==4) {
        url = argv[1];
        urlout = argv[2];
        debug = 1;
    }


    app_ctx_t ctx;
    AVPacket packet = { .data=NULL, .size = 0 };
    unsigned int sidx;
    enum AVMediaType type;
    int isframe=0;
    AVFrame *frame=NULL;

    if(debug)
        avframe_heap_testing();

    if(open_input(&ctx, url)<0) {
        fprintf(stderr, "failed to open input\n");
        return APPERROR;
    }
    
    if(open_output(&ctx, urlout)<0) {
        fprintf(stderr, "failed to open output\n");
        return APPERROR;
    }

    int i =0;
    int coded_order = 1;  // switch to display either coded order or display order
    int nextidx = 0;


    // AVFrame heap creation
    avframe_heap_t *frame_heap = avframe_heap_create(coded_order ? avframe_heap_cmp_coded : avframe_heap_cmp_display );
  
    // main loop
    while(1) {
        // read data from demuxer
        if ((ret = av_read_frame(ctx.inctx, &packet))<0 )
            break;
        
        sidx = packet.stream_index;
        type = ctx.inctx->streams[sidx]->codecpar->codec_type;

        // allocate AVFrame
        frame = av_frame_alloc();
        if(!frame) {
          ret = AVERROR(ENOMEM);
          break;
        }

        // only deal with selected video track on input
        if( type==AVMEDIA_TYPE_VIDEO && sidx==ctx.vidstream_idx ) {
            // decode video
            ret= avcodec_decode_video2( ctx.dec_ctx, frame, &isframe,  &packet);
            if(ret<0) {
                av_frame_free(&frame);
                fprintf(stderr, "couldn't decode frame %d", i);
                break;
            }


            // if frame was decoded push it onto the heap
            if(isframe) {
                frame->width = ctx.enc_ctx->width;
                frame->height = ctx.enc_ctx->height;
                frame->format = ctx.enc_ctx->pix_fmt;
                frame->display_picture_number = nextidx;

                avframe_heap_insert( frame_heap, frame );

            } else {
                av_frame_free(&frame);
            }

            // At this poing we have buffered frames into the heap. The frame on top of the heap
            // is the one with the minimum coded_picture_number or display_picture_number. 
            // We know what the next frame to be written is, nextidx, so we just have keep checking
            // the top of the heap for consecutive indexes. A top of the heap number that is higher
            // than nextidx implies we are still waiting for the frame to passed down from the decode
            // functions.
            //
            // Note we are still using the heap for presentation order when it's not necessary. It does
            // however allows us to have the same code flow for both if we funnel everything through the heap.
            while(1) {
              AVFrame *m = avframe_heap_peek_min(frame_heap);
              if(m) {
                  int minidx = coded_order ? m->coded_picture_number : m->display_picture_number;
                  if(minidx == nextidx) {
                      avframe_heap_get_min(frame_heap); // pop top of the heap. Frame already in m from calling avframe_heap_peek_min()
                      m->pts = nextidx*1001;
                      ret = write_frame(&ctx, m );
                      if(ret<0) {
                          fprintf(stderr,"write frame %d failed\n", i);
                          av_frame_free(&m);
                          break;
                      }

                      if(debug)
                          printf("%d ", minidx);

                      nextidx++;
                      av_frame_free(&m);
                  } else {
                    break;
                  }
              } else {
                break;
              }
            }

            av_packet_unref(&packet);
            i++;
        }
    }

    avframe_heap_destroy(frame_heap);
    av_write_trailer(ctx.outctx);
    avcodec_free_context(&ctx.dec_ctx);
    avcodec_free_context(&ctx.enc_ctx);
    avformat_close_input(&ctx.inctx);
    avformat_free_context(ctx.outctx);


    return ret ? 1 : 0;

}
