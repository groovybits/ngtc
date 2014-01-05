#include "ngtc.h"
#include "lav.h"
#include "demuxer.h"

int setupDemuxerFormat(DemuxerSettings *ds, char *file, struct hqv_data *hqv) {
	AVProbeData probe_data, *pd = &probe_data;

	ds->pFormatCtx = avformat_alloc_context();
	ds->pFormatCtx->flags |= AVFMT_FLAG_NONBLOCK;
	ds->pFormatCtx->flags |= AVFMT_FLAG_GENPTS;
#ifdef AVFMT_FLAG_IGNDTS
	ds->pFormatCtx->flags |= AVFMT_FLAG_IGNDTS;
#endif
	if (ds->streaming)
		ds->pFormatCtx->flags |= AVFMT_NOFILE|AVFMT_FLAG_IGNIDX;

        // Setup input format for raw A/V
        if (ds->input_format != NULL) {
                ds->fmt = av_iformat_next(NULL);
                if (ds->fmt != NULL && ds->fmt->name != NULL && strcmp(ds->fmt->name, ds->input_format) != 0) {
                        do {
                                ds->fmt = av_iformat_next(ds->fmt);
                                if (ds->fmt == NULL || ds->fmt->name == NULL)
                                        break;
                        } while (strcmp(ds->fmt->name, ds->input_format) != 0);
                }
                if (ds->fmt)
                        av_log(NULL, AV_LOG_INFO, "Input format: %s\n", ds->fmt->long_name);
                else
                        av_log(NULL, AV_LOG_WARNING, "Failed finding input format: %s\n", ds->input_format);
        }

	if (ds->streaming)
		ds->stream_buffer = (uint8_t*)av_malloc(STREAM_BUFFER_SIZE); 

        // Try to figure out input format
        if (ds->streaming && ds->fmt == NULL) {

                pd->filename = "";
                if (file)
                        pd->filename = file;
                pd->buf = ds->stream_buffer;
                pd->buf_size = STREAM_BUFFER_SIZE;

                // Wait till have enough input data
                while(av_fifo_size(hqv->fifo) < STREAM_BUFFER_SIZE && continueDecoding)
                        usleep(33000);
                if (!continueDecoding)
                        exit(0);
                // Copy some fifo data 
                memcpy(ds->stream_buffer, hqv->fifo->buffer, STREAM_BUFFER_SIZE);

                // Probe input format
                ds->fmt = av_probe_input_format(pd, 1);

                if (!ds->fmt) {
                        av_log(NULL, AV_LOG_FATAL, "Failed probing input file: %s\n", file);
                        exit(1);
                } else
                        av_log(NULL, AV_LOG_INFO, "Input format: %s\n", ds->fmt->long_name);
        }

        // Open Input File
        if (ds->streaming) {
                // Streaming input
                ds->pb = av_alloc_put_byte(ds->stream_buffer, STREAM_BUFFER_SIZE, 0,
                        hqv, fifo_read, NULL, NULL);
                ds->pb->is_streamed = 1;

                // Open video device stream
                if (av_open_input_stream(&ds->pFormatCtx, ds->pb, file, ds->fmt, ds->ap) != 0) {
                        av_log(NULL, AV_LOG_FATAL, "%s: could not open device stream\n", file);
                        return -1;        // Couldn't open file
                }
        } else {
                // Open file
                if (av_open_input_file(&ds->pFormatCtx, file, ds->fmt, 0, ds->ap) != 0) {
                        av_log(NULL, AV_LOG_FATAL, "%s: could not openfile\n", file);
                        return -1;        // Couldn't open file
                }
        }

        // Retrieve stream information
        if (av_find_stream_info(ds->pFormatCtx) < 0) {
                av_log(NULL, AV_LOG_FATAL, "%s: could not find codec parameters\n", file);
                return -1;
        }
	return 0;
}

int setupDemuxerCodecs(DemuxerSettings *ds) {
	AVCodec *pCodecVideo, *pCodecAudio, *pCodecSub;

	if (ds->do_video) {
                ds->pCodecCtxVideo = ds->pFormatCtx->streams[ds->videoStream]->codec;

                // Find the decoder for the video stream
                pCodecVideo = avcodec_find_decoder(ds->pCodecCtxVideo->codec_id);
                if (pCodecVideo == NULL) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec [%d] for video not found.\n", ds->pCodecCtxVideo->codec_id);
                        return -1;        // Codec not found
                }
                // Open video codec
                if (avcodec_open(ds->pCodecCtxVideo, pCodecVideo) < 0) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec for video not able to open.\n");
                        return -1;        // Could not open codec
                }
                // Hack to correct wrong frame rates that seem to be generated by some codecs
                if (ds->pCodecCtxVideo->time_base.num > 1000
                    && ds->pCodecCtxVideo->time_base.den == 1)
                        ds->pCodecCtxVideo->time_base.den = 1000;
	}

	if (ds->do_audio) {
                ds->pCodecCtxAudio = ds->pFormatCtx->streams[ds->audioStream]->codec;

                // Find the decoder for the audio stream
                pCodecAudio = avcodec_find_decoder(ds->pCodecCtxAudio->codec_id);
                if (pCodecAudio == NULL) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec [%d] for audio not found.\n", ds->pCodecCtxAudio->codec_id);
                        return -1;        // Codec not found
                }
                // Open audio codec
                if (avcodec_open(ds->pCodecCtxAudio, pCodecAudio) < 0) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec for audio not able to open.\n");
                        return -1;        // Could not open codec
                }
	}

	if (ds->do_sub) {
                ds->pCodecCtxSub = ds->pFormatCtx->streams[ds->subStream]->codec;

                // Find the decoder for the subtitle stream
                pCodecSub = avcodec_find_decoder(ds->pCodecCtxSub->codec_id);
                if (pCodecSub == NULL) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec [%d] for subtitle not found.\n", ds->pCodecCtxSub->codec_id);
                        //return -1;        // Codec not found
			ds->pCodecCtxSub = NULL;
                }
                // Open subtitle codec
                if (ds->pCodecCtxSub && avcodec_open(ds->pCodecCtxSub, pCodecSub) < 0) {
                        av_log(NULL, AV_LOG_FATAL,
                                "Error: Codec for subtitle not able to open.\n");
                        return -1;        // Could not open codec
                }
	}

	return 0;	
}

void freeDemuxer(DemuxerSettings *ds) {
	if (ds->do_video && ds->pCodecCtxVideo)
		avcodec_close(ds->pCodecCtxVideo);
	if (ds->do_audio && ds->pCodecCtxAudio)
		avcodec_close(ds->pCodecCtxAudio);
	if (ds->do_sub && ds->pCodecCtxSub)
		avcodec_close(ds->pCodecCtxSub);
	if (ds->streaming) {
		av_free(ds->stream_buffer);
		av_close_input_stream(ds->pFormatCtx);
		av_freep(&ds->pb);
	} else
		av_close_input_file(ds->pFormatCtx);
}