#include <jni.h>
#include <string>
#include <android/log.h>
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "testff", __VA_ARGS__)
extern "C"{//找不到头文件，则在CMakeLists.txt中添加头文件路径
#include "include/libavcodec/avcodec.h"
#include "include/libavformat/avformat.h"
#include "include/libavcodec/jni.h"
#include "include/libswscale/swscale.h" //像素格式转换头文件
#include "libswresample/swresample.h" //重采样
}

static double r2d(AVRational r){
    return  r.num == 0|| r.den == 0 ? 0. : (double)r.num/(double)r.den;
}

//当前时间戳 clock多线程不可靠
long long GetNowMs(){
    struct timeval tv;
    gettimeofday(&tv, NULL);//因为操作系统有误差
    int sec = tv.tv_sec % 360000;//防止返回的值太大
    long long t = sec * 1000 + tv.tv_usec / 1000;
    return  t;
}

extern  "C"
JNIEXPORT
//android加载后会调用
jint JNI_OnLoad(JavaVM *vm, void *res){
    //将java虚拟机的环境传递给ffmpeg
    av_jni_set_java_vm(vm, 0);
    return  JNI_VERSION_1_4;
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
    //编码器注册
    avcodec_register_all();

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

    //打开视频编码器
    //软解码器
//    AVCodec *vcodec = avcodec_find_decoder(ic->streams[videoStream]->codecpar->codec_id);
//    if (!vcodec){
//        LOGW("avcodec_find_decoder video fialed");
//        return env->NewStringUTF(hello.c_str());
//    }
    //硬解码
    AVCodec *vcodec = avcodec_find_decoder_by_name("h264_mediacodec");
    if (!vcodec){
        LOGW("avcodec_find_decoder_by_name fialed");
        return env->NewStringUTF(hello.c_str());
    }
    //解码器初始化
    AVCodecContext * vc = avcodec_alloc_context3(vcodec);
    //复制
    avcodec_parameters_to_context(vc, ic->streams[videoStream]->codecpar);
    vc->thread_count = 1;
    ret = avcodec_open2(vc, 0, 0);
    if (0 != ret){
        LOGW("avcodec_open2 video failed");
        return env->NewStringUTF(hello.c_str());
    }

    //打开音频解码器
    AVCodec *acodec = avcodec_find_decoder(ic->streams[audioStream]->codecpar->codec_id);
    if (!acodec){
        LOGW("avcodec_find_decoder audio fialed");
        return env->NewStringUTF(hello.c_str());
    }
    //音频解码器上下文初始化
    AVCodecContext * ac = avcodec_alloc_context3(acodec);
    //复制
    avcodec_parameters_to_context(ac, ic->streams[audioStream]->codecpar);
    ac->thread_count = 1;
    ret = avcodec_open2(ac, 0, 0);
    if (0 != ret){
        LOGW("avcodec_open2 audio failed");
        return env->NewStringUTF(hello.c_str());
    }

    //读取帧数据
    AVPacket *pkt = av_packet_alloc();//创建AVPacket对象并初始化
    AVFrame *frame = av_frame_alloc();

    //初始化像素格式转换的上下文
    SwsContext *vctx = NULL;
    int outWidth = 1920;
    int outHeight = 1080;

    //音频重采样上下文初始化
    SwrContext *actx = swr_alloc();
    actx = swr_alloc_set_opts(actx, av_get_default_channel_layout(2), AV_SAMPLE_FMT_S16, ac->sample_rate, av_get_default_channel_layout(ac->channels), ac->sample_fmt, ac->sample_rate, 0, 0);
    ret = swr_init(actx);

    long long start = GetNowMs();
    int frameConut = 1;
    char *rgb = new char[1920 * 1080 * 4];
    char *pcm = new char[48000 * 4 * 2];

    if (ret != 0){
        LOGW("swr_init failed");
    } else{
        LOGW("swr_init success");
    }

    for(;;){
        //超过三秒
        if (GetNowMs() - start >= 3000){
            LOGW("now decode fps = %d", frameConut/3);
            start = GetNowMs();
            frameConut = 0;
        }

        ret = av_read_frame(ic, pkt);

        if (0 != ret){
            LOGW("读取到结尾处");
            int pos = 20 * r2d(ic->streams[videoStream]->time_base);
            av_seek_frame(ic, videoStream, pos, AVSEEK_FLAG_BACKWARD|AVSEEK_FLAG_FRAME);
            continue;
        }
//        LOGW("stream = %d, size = %d, pts = %lld, flag = %d",
//             pkt->stream_index, pkt->size, pkt->pts, pkt->flags);
        //操作
        //清理释放内存

        //只测试视频
//        if (pkt->stream_index != videoStream){
//            continue;
//        }
        AVCodecContext *tempC = vc;
        if (pkt->stream_index == audioStream){
            tempC = ac;
        }

        //发送到线程中解码, pkt会被复制一份, 所以可以unref掉
        ret = avcodec_send_packet(tempC, pkt);
        av_packet_unref(pkt);
        if (ret != 0){
            LOGW("avcodec_send_packet failed");
            continue;
        }


        //这样能够保证收到所有解码后的数据, 取最后一帧时. 要send一个null pkt进去
        for(;;){
            ret = avcodec_receive_frame(tempC, frame);
            if (ret != 0){
                break;
            }
//            LOGW("avcodec_receive_frame %lld", frame->pts);
            //如果是视频帧
            if (tempC == vc){
                frameConut++;
                //从缓冲中获取SwsContext, 如果宽高比不变只会初始化一次, 在解码后获取,保证能够得到宽高
                vctx = sws_getCachedContext(vctx,
                        frame->width,
                        frame->height,
                        (AVPixelFormat)frame->format,
                        outWidth,
                        outHeight,
                        AV_PIX_FMT_RGBA,
                        SWS_FAST_BILINEAR,
                        0,0,0);
                if(!vctx){
                    LOGW("sws_getCachedContext failed");
                } else{
                    uint8_t *data[AV_NUM_DATA_POINTERS] = {0};
                    //转换出的数据不是平面方式存储,申请一个数组
                    data[0] = (uint8_t*)rgb;
                    int lines[AV_NUM_DATA_POINTERS] = {0};
                    lines[0] = outWidth * 4;
                    //视频 改变图像宽高和转换像素格式
                    int height = sws_scale(vctx, frame->data, frame->linesize, 0, frame->height, data, lines);
                    LOGW("sws_scale = %d", height);
                }
            } else{
                uint8_t *out[2] = {0};
                out[0] = (uint8_t*)pcm;
                //音频重采样
                int len = swr_convert(actx, out, frame->nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
                LOGW("swr_convert = %d", len);
            }
        }
    }

    delete[] rgb;
    delete[] pcm;
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


extern "C"
JNIEXPORT void JNICALL
Java_com_bo_test_1ffmpeg_XPlay_Open(JNIEnv *env, jobject thiz, jstring url, jobject surface) {

}