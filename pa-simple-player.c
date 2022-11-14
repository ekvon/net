#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
 
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_BUF_SIZE 1024
static char errbuf[ERROR_BUF_SIZE];
static int pa_error=0;
/*	Pulse audio sample specification	*/
static pa_sample_spec ss = {
    .format = PA_SAMPLE_FLOAT32,
    .rate = 44100,
    .channels = 1
};
static pa_simple * gPaSimple;
static AVPacket * gPkt=0;
static AVCodecContext * gCodecCtx=0;
static AVFrame * gFrame=0;

/*	This function is used to configure Pulse audio sample parametrs	*/
int process_first_packet(){
	int ret=avcodec_send_packet(gCodecCtx,gPkt);
	if(ret<0){
		av_strerror(ret,errbuf,ERROR_BUF_SIZE);  	
		fprintf(stderr,"unable to send packet (%d) (%s)\n",ret,errbuf);
		return -1;
	}
	ret=avcodec_receive_frame(gCodecCtx,gFrame);
	if(ret<0){
		av_strerror(ret,errbuf,ERROR_BUF_SIZE);  	
		fprintf(stderr,"unable to receive frame (%d) (%s)\n",ret,errbuf);
		return -1;
	}
	/*	from ffmpeg to Pulse audio format	*/
	switch(gFrame->format){
		case(AV_SAMPLE_FMT_U8):{
			ss.format=PA_SAMPLE_U8;
			break;
		}
		case(AV_SAMPLE_FMT_S16):{
			ss.format=PA_SAMPLE_S16LE;
			break;
		}
		case(AV_SAMPLE_FMT_S16P):{
		/*	not supported	*/
			fprintf(stderr,"process_first_frame: unsupported format (%d: %s)\n",
				gFrame->format,av_get_sample_fmt_name(gFrame->format));
			return -1;
		}
		case(AV_SAMPLE_FMT_S32):{
		/*	not supported	*/
			fprintf(stderr,"process_first_frame: unsupported format (%d: %s)\n",
				gFrame->format,av_get_sample_fmt_name(gFrame->format));
			return -1;
		}
		case(AV_SAMPLE_FMT_FLT):{
			ss.format=PA_SAMPLE_FLOAT32LE;
			break;
		}
		case(AV_SAMPLE_FMT_FLTP):{
			ss.format=PA_SAMPLE_FLOAT32LE;
			break;
		}
		/*	not supported	*/
		default:{
			fprintf(stderr,"process_first_frame: unsupported format (%d: %s)\n",
				gFrame->format,av_get_sample_fmt_name(gFrame->format));
			return -1;
		}
	}
	ss.rate=gFrame->sample_rate;
	ss.channels=gFrame->channels;
	return 0;
}
   
int main(int argc,char * argv[]){
	if(argc<2){
		printf("usage: decode_audio_2 path-to-file");
		return -1;
	}
	memset(errbuf,0,ERROR_BUF_SIZE);
  /* Create a new playback stream */
  if (!(gPaSimple = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &pa_error))) {
      fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(pa_error));
      exit(EXIT_FAILURE);
  }
	/*	initialization	*/
	AVFormatContext *s = NULL;
	int ret = avformat_open_input(&s, argv[1], NULL, NULL);
	if (ret < 0){
		fprintf(stderr,"unable to open input\n");
		exit(EXIT_FAILURE);
	}
	/*	debug-output	*/
	fprintf(stdout,"\tAVFormatContext: nb_streams=%u,duration=%ld,bit_rate=%ld,packet_size=%u\n",
		s->nb_streams,s->duration,s->bit_rate,s->packet_size);
		
	gPkt=av_packet_alloc();
	if(!gPkt){
		fprintf(stderr,"unable to allocate packet\n");
		avformat_close_input(&s);
		return -1;
	}
	/*	it's assumed the only stream	*/
	gPkt->stream_index=0;
	
	gFrame=av_frame_alloc();
	if(!gFrame){
		fprintf(stderr,"unable to allocate frame\n");
		avformat_close_input(&s);
		return -1;
	}
	/* find the MPEG audio decoder */
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MP3);
  if (!codec) {
      fprintf(stderr, "Codec not found\n");
      return -1;
  }
  gCodecCtx = avcodec_alloc_context3(codec);
  if (!gCodecCtx) {
      fprintf(stderr, "Could not allocate audio codec context\n");
      return -1;
  }
  /* open it */
  if (avcodec_open2(gCodecCtx, codec, NULL) < 0) {
      fprintf(stderr, "Could not open codec\n");
      exit(1);
  }
  /*	statistic	*/
  int num_packets=0;
  /*	read the first packet	*/
	ret=av_read_frame(s,gPkt);
	if(!gPkt->size){
		fprintf(stderr,"input stream is empty\n");
		exit(EXIT_FAILURE);
	}
	ret=process_first_packet();
	if(ret<0){
		fprintf(stderr,"unable to configure Pulse audio\n");
		exit(EXIT_FAILURE);
	}
	while(1){
		/*	save raw-pcm to output file	*/
		int data_size = av_get_bytes_per_sample(gCodecCtx->sample_fmt);
    if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        fprintf(stderr, "Failed to calculate data size\n");
        avformat_close_input(&s);
        exit(1);
    }
    /*	write only the first channel	*/
    for (int i = 0; i < gFrame->nb_samples; i++){
		  if (pa_simple_write(gPaSimple, gFrame->data[0] + data_size*i, data_size, &pa_error) < 0) {
		      fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(pa_error));
		      exit(EXIT_FAILURE);
		  }
    }
    /*	read next packet	*/
    ret=av_read_frame(s,gPkt);
    if(!gPkt->size)
    	/*	input stream is exhausted	*/
    	break;
    /*	fprintf(stdout,"AVPacket: size=%d,duration=%ld\n",pkt->size,pkt->duration);	*/
		ret=avcodec_send_packet(gCodecCtx,gPkt);
		if(ret<0){
			av_strerror(ret,errbuf,ERROR_BUF_SIZE);  	
			fprintf(stderr,"unable to send packet (%d) (%s)\n",ret,errbuf);
			avformat_close_input(&s);
			return -1;
		}
		ret=avcodec_receive_frame(gCodecCtx,gFrame);
		if(ret<0){
			av_strerror(ret,errbuf,ERROR_BUF_SIZE);  	
			fprintf(stderr,"unable to receive frame (%d) (%s)\n",ret,errbuf);
			avformat_close_input(&s);
			return -1;
		}
		/*	statistic	*/
    num_packets++;
  }
	avformat_close_input(&s);
	printf("ok: %d packets are processed\n",num_packets);
	return 0;
}
