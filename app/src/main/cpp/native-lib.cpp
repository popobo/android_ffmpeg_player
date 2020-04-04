#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "testff", __VA_ARGS__)
extern "C"{//找不到头文件，则在CMakeLists.txt中添加头文件路径
#include "include/libavcodec/avcodec.h"
#include "include/libavformat/avformat.h"
#include "include/libavcodec/jni.h"
#include "include/libswscale/swscale.h" //像素格式转换头文件
#include "libswresample/swresample.h" //重采样

}

//顶点着色器代码glsl代码编写
#define  GET_STR(x) #x
static const char *vertexShader = GET_STR(
        attribute vec4 aPosition; //顶点信息
        attribute vec2 aTexCoord; //材质坐标
        varying vec2 vTexCoord; //输出的材质坐标
        void main(){
            vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);//以左上角为原点
            gl_Position = aPosition; //现实的顶点
        }
        );

//像素(片元)着色器代码glsl代码编写, 软解码和部分x86硬解码格式yuv420p
static const char *fragYUV420P = GET_STR(
        precision mediump float; //精度
        varying vec2 vTexCoord; //顶点着色器传递的坐标
        uniform sampler2D yTexture;  //输入的材质(不透明灰度, 单像素)
        uniform sampler2D uTexture;
        uniform sampler2D vTexture;
        void main(){
            vec3 yuv;//vec3含有三个元素的向量
            vec3 rgb;
            //这变的rgb相当于yuv
            yuv.r = texture2D(yTexture, vTexCoord).r;
            yuv.g = texture2D(uTexture, vTexCoord).r - 0.5;
            yuv.b = texture2D(uTexture, vTexCoord).r - 0.5;
            rgb = mat3(1.0, 1.0, 1.0,
                       0.0, -0.39465, 2.03211,
                       1.13983, -0.5863, 0.0) * yuv;
            //输出像素颜色
            gl_FragColor = vec4(rgb, 1.0);
        }
        );

GLint InitShader(const char *code, GLint type){
    //创建shader
    GLint sh = glCreateShader(type);
    if(0 == sh){
        LOGW("glCreateShader %d falied", type);
        return 0;
    }
    //加载shader
    glShaderSource(sh,
                    1,//shader数量
                    &code,//shader代码
                    0);//代码长度, 传0则自动寻找结尾
    //编译shader让显卡运行
    glCompileShader(sh);
    //获取编译情况
    GLint status;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (0 == status){
        LOGW("glCompileShader failed!");
        return 0;
    }
    LOGW("glCompileShader successfully!");
    return  sh;
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
    return  JNI_VERSION_1_6;
}

//只有在清理时用得到,所以为了测试方便就用静态了
static  SLObjectItf engineSL = NULL;
SLEngineItf CreateSL(){
    SLresult  ret;
    SLEngineItf en;//指针
    //创建引擎
    ret = slCreateEngine(&engineSL, 0, 0, 0, 0, 0);
    if(ret != SL_RESULT_SUCCESS){
        return  NULL;
    }

    //实例化, 传入SL_BOOLEAN_FALSE表示等待对象的创建
    ret = (*engineSL)->Realize(engineSL, SL_BOOLEAN_FALSE);
    if(ret != SL_RESULT_SUCCESS){
        return  NULL;
    }

    //获取引擎的接口
    ret = (*engineSL)->GetInterface(engineSL, SL_IID_ENGINE, &en);
    if(ret != SL_RESULT_SUCCESS){
        return  NULL;
    }

    //返回地址
    return  en;
}

void PcmCall(SLAndroidSimpleBufferQueueItf bf, void *contex)
{
    LOGW("PcmCall");
    static FILE *fp = NULL;
    static char *buf = NULL;
    if(!buf){
        buf = new char[1024 * 1024];
    }
    if(!fp){
        //用二进制的方式打开, 因为ASCII只用7位, 会把符号位去掉
        fp = fopen("/sdcard/test.pcm", "rb");
    }
    if (!fp){
        return;
    }
    //没有到结尾
    if(feof(fp) == 0){
        int len = fread(buf, 1, 1024, fp);
        if (len > 0){
            (*bf)->Enqueue(bf, buf, len);
        }
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo_test_1ffmpeg_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";

    /*
    //1.创建引擎
    SLEngineItf en = CreateSL();
    if(en){
        LOGW("CreateSL success!");
    }else{
        LOGW("CreateSL failed!");
    }

    //2.创建混音器
    //混音器
    SLObjectItf mix = NULL;
    SLresult ret;
    ret = (*en)->CreateOutputMix(en, &mix, 0, 0, 0);
    if (ret != SL_RESULT_SUCCESS){
        LOGW("(*en)->CreateOutputMix failed!");
    }
    //实例化混音器 阻塞式地等待创建
    ret = (*mix)->Realize(mix, SL_BOOLEAN_FALSE);
    if (ret != SL_RESULT_SUCCESS){
        LOGW("(*en)->CreateOutputMix failed!");
    }

    //存储一个接口给player使用
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, mix};
    //结构体
    SLDataSink audioSink = {&outputMix, 0};

    //3.配置音频信息
    //缓冲队列
    SLDataLocator_AndroidSimpleBufferQueue que = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 10};
    //音频格式
    SLDataFormat_PCM pcm = {
            SL_DATAFORMAT_PCM,
            2,//声道数
            SL_SAMPLINGRATE_48,//采样率
            SL_PCMSAMPLEFORMAT_FIXED_16,//存储大小
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT,//左右声道
            SL_BYTEORDER_LITTLEENDIAN//字节序,小端,主机
    };
    //生成结构体给播放器使用
    SLDataSource ds = {&que, &pcm};

    //4 创建播放器
    SLObjectItf player = NULL;
    SLPlayItf playerInterface;
    SLAndroidSimpleBufferQueueItf pcmQueue;
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};//表示需要哪些接口
    const SLboolean req[] = {SL_BOOLEAN_TRUE};//表示这个接口是开放还是关闭
    ret = (*en)->CreateAudioPlayer(en, &player, &ds, &audioSink, sizeof(ids)/ sizeof(SLInterfaceID), ids ,req);
    if (ret != SL_RESULT_SUCCESS){
        LOGW("(*en)->CreateAudioPlayer failed!");
    }else{
        LOGW("(*en)->CreateAudioPlayer successfully!");
    }
    //实例化
    (*player)->Realize(player, SL_BOOLEAN_FALSE);
    //获取player接口
    ret = (*player)->GetInterface(player, SL_IID_PLAY, &playerInterface);
    if (ret != SL_RESULT_SUCCESS){
        LOGW("(*player)->GetInterface playerInterface failed!");
    }else{
        LOGW("(*player)->GetInterface playerInterface successfully!");
    }
    //获取队列接口
    ret = (*player)->GetInterface(player, SL_IID_BUFFERQUEUE, &pcmQueue);
    if (ret != SL_RESULT_SUCCESS){
        LOGW("(*player)->GetInterface pcmQueue failed!");
    }else{
        LOGW("(*player)->GetInterface pcmQueue successfully!");
    }

    //设置缓冲接口回调函数,播放队列空的时候调用, 第一次要先放点东西
    (*pcmQueue)->RegisterCallback(pcmQueue, PcmCall, 0);
    //设置为播放状态
    (*playerInterface)->SetPlayState(playerInterface, SL_PLAYSTATE_PLAYING);
    //启动队列回调
    (*pcmQueue)->Enqueue(pcmQueue, "", 1);*/

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
    /*const char *path = env->GetStringUTFChars(url, 0);


    //初始化,解封装库
    av_register_all();
    //初始化网络
    avformat_network_init();
    //编码器注册
    avcodec_register_all();

    //打开文件
    AVFormatContext *ic = NULL;
    int ret = avformat_open_input(&ic, path, 0, 0);
    if (0 != ret) {
        LOGW("avformat_open_input %s failed!:%s", path, av_err2str(ret));
        return;
    }
    LOGW("avformat_open_input %s success", path);
    LOGW("duration = %lld, nb_streams = %d", ic->duration, ic->nb_streams); //读不到flv信息, flv文件头部没有相关信息
    //获取流信息, 如果文件没有头部信息
    ret = avformat_find_stream_info(ic, 0);
    if (0 != ret){
        LOGW("avformat_find_stream_info failed %s", av_err2str(ret));
        return;
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
//        return;
//    }
    //硬解码
    AVCodec *vcodec = avcodec_find_decoder_by_name("h264_mediacodec");
    if (!vcodec){
        LOGW("avcodec_find_decoder_by_name fialed");
        return;
    }
    //解码器初始化
    AVCodecContext * vc = avcodec_alloc_context3(vcodec);
    //复制
    avcodec_parameters_to_context(vc, ic->streams[videoStream]->codecpar);
    vc->thread_count = 1;
    ret = avcodec_open2(vc, 0, 0);
    if (0 != ret){
        LOGW("avcodec_open2 video failed");
        return;
    }

    //打开音频解码器
    AVCodec *acodec = avcodec_find_decoder(ic->streams[audioStream]->codecpar->codec_id);
    if (!acodec){
        LOGW("avcodec_find_decoder audio fialed");
        return;
    }
    //音频解码器上下文初始化
    AVCodecContext * ac = avcodec_alloc_context3(acodec);
    //复制
    avcodec_parameters_to_context(ac, ic->streams[audioStream]->codecpar);
    ac->thread_count = 1;
    ret = avcodec_open2(ac, 0, 0);
    if (0 != ret){
        LOGW("avcodec_open2 audio failed");
        return;
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
    if (ret != 0){
        LOGW("swr_init failed");
    } else{
        LOGW("swr_init success");
    }

    long long start = GetNowMs();
    int frameConut = 1;
    char *rgb = new char[outWidth * outHeight * 4];
    char *pcm = new char[48000 * 4 * 2];

    //显示窗口初始化
    ANativeWindow *nwin = ANativeWindow_fromSurface(env, surface);
    ANativeWindow_setBuffersGeometry(nwin, outWidth, outHeight, WINDOW_FORMAT_RGBA_8888);
    //存放
    ANativeWindow_Buffer wBuf;

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
//            break;
            int pos = 20 * r2d(ic->streams[videoStream]->time_base);
            av_seek_frame(ic, videoStream, pos, AVSEEK_FLAG_BACKWARD|AVSEEK_FLAG_FRAME);
            continue;
        }
//        LOGW("stream = %d, size = %d, pts = %lld, flag = %d",
//             pkt->stream_index, pkt->size, pkt->pts, pkt->flags);

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
        //清理释放内存
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
                    if (height > 0){
                        //锁住
                        ANativeWindow_lock(nwin, &wBuf, 0);
                        //用来做交换的内存
                        uint8_t *dst = (uint8_t*)wBuf.bits;
                        //RGBA_8888, 一个像素点4个字节, 输出高度和输出宽度要与转换出来的数据一致,否则会报错
                        memcpy(dst, rgb, outWidth * outHeight * 4);
                        ANativeWindow_unlockAndPost(nwin);
                    }
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
    */

    const char *path = env->GetStringUTFChars(url, 0);
    LOGW("open url is %s", path);

    FILE * fp = fopen(path, "rb");
    if (!fp){
        LOGW("open file %s failed!", path);
        return;
    }

    //1.获取原始窗口
    ANativeWindow *nwin = ANativeWindow_fromSurface(env, surface);

    //EGL
    //1.display(显示策略)创建和初始化
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display ==  EGL_NO_DISPLAY){
        LOGW("eglGetDisplay failed!");
        return;
    }

    if(EGL_TRUE != eglInitialize(display, 0, 0)){
        LOGW("eglInitialize failed!");
        return;
    }
    //2.surface
    //2-1 surface配置, 窗口
    //输出配置
    EGLConfig config;
    EGLint configNum;
    EGLint configSpec[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
    };
    if(EGL_TRUE != eglChooseConfig(display, configSpec, &config, 1, &configNum)){
        LOGW("eglChooseConfig failed!");
        return;
    }

    //创建surface, nwin跟窗口关联
    EGLSurface winSurface =  eglCreateWindowSurface(display, config, nwin, 0);
    if (winSurface ==  EGL_NO_SURFACE){
        LOGW("eglCreateWindowSurface failed!");
        return;
    }

    //3.context 常见关联的上下文, 和OpenGL关联起来
    const EGLint ctxAttr[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttr);
    if (context ==  EGL_NO_CONTEXT){
        LOGW("eglCreateContext failed!");
        return;
    }
    //EGL和OpenGL关联起来
    if(EGL_TRUE != eglMakeCurrent(display, winSurface,  winSurface, context)){
        LOGW("eglMakeCurrent failed!");
        return;
    }
    LOGW("EGL Init Successfully!");

    //顶点和片元shader初始化
    //顶点shader初始化
    GLint vsh = InitShader(vertexShader, GL_VERTEX_SHADER);
    //片元yuv420p shader初始化
    GLint fsh = InitShader(fragYUV420P, GL_FRAGMENT_SHADER);

    /*---------------------------------------------------*/
    //创建渲染程序
    GLint  program = glCreateProgram();
    if (0 == program){
        LOGW("glCreateProgram failed!");
        return;
    }
    //渲染程序中加入着色器
    glAttachShader(program, vsh);
    glAttachShader(program, fsh);

    //链接程序
    glLinkProgram(program);
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE){
        LOGW("glLinkProgram failed!");
        return;
    }
    //激活渲染程序
    glUseProgram(program);
    LOGW("glLinkProgram successfully!");
    /*---------------------------------------------------*/

    //加入三维顶点数据 两个三角形组成正方形 用static保证程序运行过程中一直存在
    static float vers[] = {
        1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f
    };
    //获取shader当中的顶点变量,并使其有效
    GLuint apos = (GLuint)glGetAttribLocation(program, "aPosition");
    glEnableVertexAttribArray(apos);
    //传递顶点, xyz三个数据, 12(3个float是12个字节)是每一个值的间隔
    glVertexAttribPointer(apos, 3, GL_FLOAT, GL_FALSE, 12, vers);

    //加入材质坐标数据
    static float txts[] = {
            1.0f, 0.0f,//右下
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
    };
    GLuint atex = (GLuint)glGetAttribLocation(program, "aTexCoord");
    glEnableVertexAttribArray(atex);
    glVertexAttribPointer(atex, 2, GL_FLOAT, GL_FALSE, 8, txts);

    int width = 424;
    int height = 240;

    //材质纹理初始化
    //设置纹理层
    glUniform1i(glGetUniformLocation(program, "yTexture"), 0);//对应纹理第1层
    glUniform1i(glGetUniformLocation(program, "uTexture"), 1);//对应纹理第2层
    glUniform1i(glGetUniformLocation(program, "vTexture"), 2);//对应纹理第3层

    //创建openGL纹理
    GLuint texts[3] = {0};
    //创建三个纹理
    glGenTextures(3, texts);
    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[0]);//绑定,表示下面的属性是针对它来设置的
    //缩小时的过滤器, 线性插值
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //放大过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,//细节基本 0默认
                 GL_LUMINANCE,//GPU内部格式,亮度,灰度图.y
                 width, height, //尺寸要是2的倍数, 默认拉伸到全屏
                 0, //边框
                 GL_LUMINANCE, //数据的像素格式, 亮度, 灰度图, 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL //纹理的数据
    );

    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[1]);//绑定,表示下面的属性是针对它来设置的
    //缩小时的过滤器, 线性插值
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //放大过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,//细节基本 0默认
                 GL_LUMINANCE,//GPU内部格式,亮度,灰度图.y
                 width/2, height/2, //尺寸要是2的倍数, 默认拉伸到全屏
                 0, //边框
                 GL_LUMINANCE, //数据的像素格式, 亮度, 灰度图, 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL //纹理的数据
    );


    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[2]);//绑定,表示下面的属性是针对它来设置的
    //缩小时的过滤器, 线性插值
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    //放大过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,//细节基本 0默认
                 GL_LUMINANCE,//GPU内部格式,亮度,灰度图.y
                 width/2, height/2, //尺寸要是2的倍数, 默认拉伸到全屏
                 0, //边框
                 GL_LUMINANCE, //数据的像素格式, 亮度, 灰度图, 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL //纹理的数据
    );

    /*---------------------------------------------------*/
    //纹理的修改和显示
    unsigned char *buf[3] = {0};
    buf[0] = new unsigned char[width * height];
    buf[1] = new unsigned char[width * height/4];//yuv420p格式 uv数据比y少一半. 且默认拉伸
    buf[2] = new unsigned char[width * height/4];

    for (int i = 0; i < 10000; ++i) {
//        memset(buf[0], i, width * height);
//        memset(buf[1], i, width * height/4);
//        memset(buf[2], i, width * height/4);

        //yuv420p yyyyyyyt uu vv
        if (feof(fp) == 0){
            fread(buf[0], 1, width*height, fp);
            fread(buf[1], 1, width*height/4, fp);
            fread(buf[2], 1, width*height/4, fp);
        }

        //激活第1层纹理, 绑定到创建的openGL纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texts[0]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                        buf[0]);

        //激活第1层纹理, 绑定到创建的openGL纹理
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texts[1]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE,
                        GL_UNSIGNED_BYTE, buf[1]);

        //激活第1层纹理, 绑定到创建的openGL纹理
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texts[2]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE,
                        GL_UNSIGNED_BYTE, buf[2]);

        //三维绘制
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        //窗口显示 把winSurface中的转换到窗口
        eglSwapBuffers(display, winSurface);
    }

    env->ReleaseStringUTFChars(url, path);
}