#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define TAG "zyh"
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,TAG,__VA_ARGS__)
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)

#define  GET_STR(x) #x
//顶点着色器
static const char *vertexShader = GET_STR(
        attribute
        vec4 aPosition;//顶点坐标
        attribute
        vec2 aTexCoord;//顶点材质坐标
        varying
        vec2 vTexCoord;//输出材质坐标
        void main() {
            vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);//坐标转换
            gl_Position = aPosition;//显示顶点
        }
);

//片元着色器
//片元着色器,软解码和部分x86硬解码
static const char *fragYUV420P = GET_STR(
        precision
        mediump float;    //精度
        varying
        vec2 vTexCoord;     //顶点着色器传递的坐标
        uniform
        sampler2D yTexture; //输入的材质（不透明灰度，单像素）
        uniform
        sampler2D uTexture;
        uniform
        sampler2D vTexture;
        void main() {
            vec3 yuv;
            vec3 rgb;
            yuv.r = texture2D(yTexture, vTexCoord).r;
            yuv.g = texture2D(uTexture, vTexCoord).r - 0.5;//- 0.5 确保精度
            yuv.b = texture2D(vTexture, vTexCoord).r - 0.5;//- 0.5 确保精度
            //转换算法 网上很多，直接拿过来用就可以
            rgb = mat3(1.0, 1.0, 1.0,
                       0.0, -0.39465, 2.03211,
                       1.13983, -0.58060, 0.0) * yuv;
            //输出像素颜色
            gl_FragColor = vec4(rgb, 1.0);
        }
);


GLuint InitShader(const char *code, GLenum type) {
    GLuint gLuint = glCreateShader(type);
    if (gLuint == 0) {
        LOGE("InitShader 失败 %d", gLuint);
        return 0;
    }
    //加载shader
    glShaderSource(gLuint,
                   1, //shader数量
                   &code,//shader代码
                   0); //代码长度
    //编译shader
    glCompileShader(gLuint);
    //获取编译情况
    GLint status;
    glGetShaderiv(gLuint, GL_COMPILE_STATUS, &status);
    if (status == 0) {
        LOGE("glCompileShader failed!");
        //失败释放
        return 0;
    }
    LOGE("glCompileShader success!");
    return gLuint;
}


extern "C"
JNIEXPORT void JNICALL
Java_vip_ruoyun_openglesdemo_XPlay_open(JNIEnv *env, jobject thiz, jstring url, jobject surface) {

    const char *path = env->GetStringUTFChars(url, 0);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOGD("open file %s failed!", path);
        env->ReleaseStringUTFChars(url, path);
        return;
    }

    //创建 EGL
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay 失败");
        return;
    }
    //初始化
    if (EGL_TRUE != eglInitialize(eglDisplay, 0, 0)) {
        LOGE("eglInitialize 失败");
        return;
    }

    //输入配置
    //错误配置
//    const EGLint attrib_list[] = {
//            EGL_RED_SIZE, 8,
//            EGL_GREEN_SIZE, 8,
//            EGL_BLUE_SIZE, 8,
//            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
//    };

    //https://github.com/android/ndk-samples/blob/fc2ed743f9e34e5b7dcfc06c11420f3eb886f1bf/teapots/common/ndk_helper/GLContext.cpp
    //有的手机 不设置 EGL_DEPTH_SIZE 的话就会报错
    const EGLint attribs_24bit[] = {
            EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES2_BIT,  // Request opengl ES2.0
            EGL_SURFACE_TYPE,
            EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE};

    //输出配置
    EGLConfig configs;
    EGLint num_configs;
    if (EGL_TRUE != eglChooseConfig(eglDisplay, attribs_24bit, &configs, 1, &num_configs)) {
        LOGE("eglChooseConfig 失败");
        return;
    }

    if (!num_configs) {
        LOGE("attribs_16bit");
        // Fall back to 16bit depth buffer
        const EGLint attribs_16bit[] = {EGL_RENDERABLE_TYPE,
                                        EGL_OPENGL_ES2_BIT,  // Request opengl ES2.0
                                        EGL_SURFACE_TYPE,
                                        EGL_WINDOW_BIT,
                                        EGL_BLUE_SIZE, 8,
                                        EGL_GREEN_SIZE, 8,
                                        EGL_RED_SIZE, 8,
                                        EGL_DEPTH_SIZE, 16,
                                        EGL_NONE};
        if (EGL_TRUE != eglChooseConfig(eglDisplay, attribs_16bit, &configs, 1, &num_configs)) {
            LOGE("eglChooseConfig 失败");
            return;
        }
    }

    if (!num_configs) {
        LOGE("Unable to retrieve EGL config");
        return;
    }

    //创建 ANativeWindow，获取原始窗口
    ANativeWindow *aNativeWindow = ANativeWindow_fromSurface(env, surface);

    //创建egl 的 surface，关联
    EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, configs, aNativeWindow, NULL);
    if (eglSurface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface 失败");
        return;
    }

    //创建关联上下文，和 OpenGL 关联
    const EGLint attrib_list_context[] = {
            EGL_CONTEXT_CLIENT_VERSION,
            2,// Request opengl ES2.0
            EGL_NONE};
    EGLContext eglContext = eglCreateContext(eglDisplay, configs, NULL,
                                             attrib_list_context);
    if (eglContext == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext 失败");
        return;
    }
    if (EGL_TRUE != eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        LOGE("eglMakeCurrent 失败");
        return;
    }
    LOGE("egl 初始化成功");

    //初始化
    //顶点着色器
    GLuint vsh = InitShader(vertexShader, GL_VERTEX_SHADER);
    //片元着色器
    GLuint fsh = InitShader(fragYUV420P, GL_FRAGMENT_SHADER);

    //创建渲染程序
    GLuint program = glCreateProgram();
    if (program == 0) {
        LOGE("glCreateProgram 失败");
        return;
    }
    //加入着色器
    glAttachShader(program, vsh);
    glAttachShader(program, fsh);

    //链接程序
    glLinkProgram(program);

    //获取运行状态
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        LOGE("glGetProgramiv 运行失败");
        return;
    }

    //激活渲染程序
    glUseProgram(program);
    LOGE("glUseProgram 运行成功");


    //加入三维顶点数据 两个三角形组成正方形
    static float vers[] = {
            1.0f, -1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
            1.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f,
    };
    GLuint apos = (GLuint) glGetAttribLocation(program, "aPosition");
    glEnableVertexAttribArray(apos);
    //传递顶点
    glVertexAttribPointer(apos, 3, GL_FLOAT, GL_FALSE, 12, vers);

    //加入材质坐标数据
    static float txts[] = {
            1.0f, 0.0f, //右下
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0, 1.0
    };
    GLuint atex = (GLuint) glGetAttribLocation(program, "aTexCoord");
    glEnableVertexAttribArray(atex);
    glVertexAttribPointer(atex, 2, GL_FLOAT, GL_FALSE, 8, txts);


    //352x288
    int width = 352;
    int height = 288;

    //材质纹理初始化
    //设置纹理层
    glUniform1i(glGetUniformLocation(program, "yTexture"), 0); //对于纹理第1层
    glUniform1i(glGetUniformLocation(program, "uTexture"), 1); //对于纹理第2层
    glUniform1i(glGetUniformLocation(program, "vTexture"), 2); //对于纹理第3层

    //创建opengl纹理
    GLuint texts[3] = {0};
    //创建三个纹理
    glGenTextures(3, texts);

    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[0]);
    //缩小的过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,           //细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width, height, //拉升到全屏
                 0,             //边框
                 GL_LUMINANCE,//数据的像素格式 亮度，灰度图 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL                    //纹理的数据
    );

    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[1]);
    //缩小的过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,           //细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width / 2, height / 2, //拉升到全屏
                 0,             //边框
                 GL_LUMINANCE,//数据的像素格式 亮度，灰度图 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL                    //纹理的数据
    );

    //设置纹理属性
    glBindTexture(GL_TEXTURE_2D, texts[2]);
    //缩小的过滤器
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //设置纹理的格式和大小
    glTexImage2D(GL_TEXTURE_2D,
                 0,           //细节基本 0默认
                 GL_LUMINANCE,//gpu内部格式 亮度，灰度图
                 width / 2, height / 2, //拉升到全屏
                 0,             //边框
                 GL_LUMINANCE,//数据的像素格式 亮度，灰度图 要与上面一致
                 GL_UNSIGNED_BYTE, //像素的数据类型
                 NULL                    //纹理的数据
    );

    //////////////////////////////////////////////////////
    ////纹理的修改和显示
    unsigned char *buf[3] = {0};
    buf[0] = new unsigned char[width * height];
    buf[1] = new unsigned char[width * height / 4];
    buf[2] = new unsigned char[width * height / 4];


    for (int i = 0; i < 100000; i++) {
        //memset(buf[0],i,width*height);
        // memset(buf[1],i,width*height/4);
        //memset(buf[2],i,width*height/4);

        //420p   yyyyyyyy uu vv
        if (feof(fp) == 0) {
            //yyyyyyyy
            fread(buf[0], 1, static_cast<size_t>(width * height), fp);
            fread(buf[1], 1, static_cast<size_t>(width * height / 4), fp);
            fread(buf[2], 1, static_cast<size_t>(width * height / 4), fp);
        }

        //激活第1层纹理,绑定到创建的opengl纹理
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texts[0]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                        buf[0]);



        //激活第2层纹理,绑定到创建的opengl纹理
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, texts[1]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE,
                        GL_UNSIGNED_BYTE, buf[1]);


        //激活第2层纹理,绑定到创建的opengl纹理
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, texts[2]);
        //替换纹理内容
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, GL_LUMINANCE,
                        GL_UNSIGNED_BYTE, buf[2]);

        //三维绘制
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        //窗口显示
        eglSwapBuffers(eglDisplay, eglSurface);
    }
    env->ReleaseStringUTFChars(url, path);
}