#include <jni.h>
#include <string>
#include <android/log.h>
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "testff", __VA_ARGS__)
extern "C"{//找不到头文件，则在CMakeLists.txt中添加头文件路径
#include "include/libavcodec/avcodec.h"
#include "include/libavformat/avformat.h"
}



static double r2d(AVRational r){
    return  r.num == 0|| r.den == 0 ? 0. : (double)r.num/(double)r.den;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo_test_1ffmpeg_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    //链接错误，声明：这是一个函数，到时候你运行的时候去找它
    hello += avcodec_configuration();
    //初始化,解封装库
    av_register_all();
    //初始化网络
    avformat_network_init();
    //打开文件
    AVFormatContext *ic = NULL;
    char path[] = "/sdcard/test.mp4";
    int ret = avformat_open_input(&ic, path, 0, 0);
    if (0 != ret) {
        LOGW("avformat_open_input %s failed!:%s", path, av_err2str(ret));
        return env->NewStringUTF(hello.c_str());
    }
    LOGW("avformat_open_input %s success", path);
    LOGW("duration = %lld, nb_streams = %d", ic->duration, ic->nb_streams); //读不到flv信息, flv文件头部没有相关信息
    //获取流信息, 如果文件没有头部信息
    ret = avformat_find_stream_info(ic, 0);
    if (0 != ret){
        LOGW("avformat_find_stream_info failed %s", av_err2str(ret));
        return env->NewStringUTF(hello.c_str());
    }
    LOGW("duration = %lld, nb_streams = %d", ic->duration, ic->nb_streams);

    double fps = 0;
    int videoStream = 0;
    int audioStream = 0;
    for (int i = 0; i < ic->nb_streams; i++) {
        AVStream *as = ic->streams[i];
        if (AVMEDIA_TYPE_VIDEO == as->codecpar->codec_type){
            LOGW("视频数据");
            videoStream = i;
            fps = r2d(as->avg_frame_rate);
            LOGW("fps = %lf, width = %d, height = %d, codec_id = %d pixformat = %d",
                    fps,
                    as->codecpar->width,
                    as->codecpar->height,
                    as->codecpar->codec_id,
                    as->codecpar->format);
        } else if (AVMEDIA_TYPE_AUDIO == as->codecpar->codec_type){
            LOGW("音频数据");
            audioStream = i;
            LOGW("sample_rate = %d, channels = %d, sample_format = %d",
                    as->codecpar->sample_rate,
                    as->codecpar->channels,
                    as->codecpar->format);
        }
    }

    //av_find_best_stream获取音频流信息
    audioStream = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    LOGW("av_find_best_stream audioStream=%d\n", audioStream);

    //读取帧数据
    AVPacket *pkt = av_packet_alloc();//创建AVPacket对象并初始化
    for(;;){
        ret = av_read_frame(ic, pkt);
        if (0 != ret){
            LOGW("读取到结尾处");
            int pos = 0 * r2d(ic->streams[videoStream]->time_base);
            av_seek_frame(ic, videoStream, pos, AVSEEK_FLAG_BACKWARD|AVSEEK_FLAG_FRAME);
            continue;
        }
        LOGW("stream = %d, size = %d, pts = %lld, flag = %d",
                pkt->stream_index, pkt->size, pkt->pts, pkt->flags);
        //操作
        //释放内存
        av_packet_unref(pkt);
    }

    //关闭上下文
    avformat_close_input(&ic);
    return env->NewStringUTF(hello.c_str());
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_bo_test_1ffmpeg_MainActivity_open(JNIEnv *env, jobject thiz, jstring url, jobject handle) {
    const char *cUrl = env->GetStringUTFChars(url, 0);

    FILE *fp = fopen(cUrl, "rb");
    if(!fp){
        LOGW("File %s open failed!", cUrl);
    } else{
        LOGW("File %s open successfully!", cUrl);
        fclose(fp);
    }

    env->ReleaseStringUTFChars(url, cUrl);

    return true;
}