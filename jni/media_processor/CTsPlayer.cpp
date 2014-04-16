#include "CTsPlayer.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <android/native_window.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include "player_set_sys.h"
#include "Amsysfsutils.h"
#include <sys/times.h>
#include <time.h>

using namespace android;

#define M_LIVE	1
#define M_TVOD	2
#define M_VOD	3
#define RES_VIDEO_SIZE 256
#define RES_AUDIO_SIZE 64
#define MAX_WRITE_COUNT 20

static bool m_StopThread = false;

//log switch
static int prop_shouldshowlog = 0;
int prop_dumpfile = 0;
int prop_buffertime = 0;
int hasaudio = 0;
int hasvideo = 0;
int prop_softfit = 0;
float prop_audiobuflevel = 0.0;
float prop_videobuflevel = 0.0;

char old_free_scale_axis[64] = {0};
char old_window_axis[64] = {0};
char old_free_scale[64] = {0};

#define LOGV(...) \
    do { \
        if (prop_shouldshowlog) { \
            __android_log_print(ANDROID_LOG_VERBOSE, "TsPlayer", __VA_ARGS__); \
        } \
    } while (0)

#define LOGD(...) \
    do { \
        if (prop_shouldshowlog) { \
            __android_log_print(ANDROID_LOG_DEBUG, "TsPlayer", __VA_ARGS__); \
        } \
    } while (0)

#define LOGI(...) \
    do { \
        if (prop_shouldshowlog) { \
            __android_log_print(ANDROID_LOG_INFO, "TsPlayer", __VA_ARGS__); \
        } \
    } while (0)

//#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "TsPlayer", __VA_ARGS__) 
//#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG , "TsPlayer", __VA_ARGS__)
//#define LOGI(...) __android_log_print(ANDROID_LOG_INFO  , "TsPlayer", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN  , "TsPlayer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR  , "TsPlayer", __VA_ARGS__)

typedef enum {
    OUTPUT_MODE_480I = 0,
    OUTPUT_MODE_480P,
    OUTPUT_MODE_576I,
    OUTPUT_MODE_576P,
    OUTPUT_MODE_720P,
    OUTPUT_MODE_1080I,
    OUTPUT_MODE_1080P,
    OUTPUT_MODE_4K2K24HZ,
    OUTPUT_MODE_4K2K25HZ,
    OUTPUT_MODE_4K2K30HZ,
    OUTPUT_MODE_4K2KSMPTE,
}OUTPUT_MODE;

OUTPUT_MODE get_display_mode()
{
    int fd;
    char mode[16] = {0};
    char *path = "/sys/class/display/mode";
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        memset(mode, 0, 16); // clean buffer and read 15 byte to avoid strlen > 15	
        read(fd, mode, 15);
        mode[strlen(mode)] = '\0';
        close(fd);
        if(!strncmp(mode, "480i", 4) || !strncmp(mode, "480cvbs", 7)) {
            return OUTPUT_MODE_480I;
        } else if(!strncmp(mode, "480p", 4)) {
            return OUTPUT_MODE_480P;
        } else if(!strncmp(mode, "576i", 4) || !strncmp(mode, "576cvbs", 7)) {
            return OUTPUT_MODE_576I;
        } else if(!strncmp(mode, "576p", 4)) {
            return OUTPUT_MODE_576P;
        } else if(!strncmp(mode, "720p", 4)) {
            return OUTPUT_MODE_720P;
        } else if(!strncmp(mode, "1080i", 5)) {
            return OUTPUT_MODE_1080I;
        } else if(!strncmp(mode, "1080p", 5)) {
            return OUTPUT_MODE_1080P;
        } else if(!strncmp(mode, "4k2k24hz", 8)) {
            return OUTPUT_MODE_4K2K24HZ;
        } else if(!strncmp(mode, "4k2k25hz", 8)) {
            return OUTPUT_MODE_4K2K25HZ;
        } else if(!strncmp(mode, "4k2k30hz", 8)) {
            return OUTPUT_MODE_4K2K30HZ;
        } else if(!strncmp(mode, "4k2ksmpte", 8)) {
            return OUTPUT_MODE_4K2KSMPTE;
        }
    } else {
        LOGE("get_display_mode open file %s error\n", path);
    }
    return OUTPUT_MODE_720P;
}

void getPosition(OUTPUT_MODE output_mode, int *x, int *y, int *width, int *height)
{
    char vaxis_newx_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_newy_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_width_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_height_str[PROPERTY_VALUE_MAX] = {0};

    switch(output_mode) {
    case OUTPUT_MODE_480I:
        property_get("ubootenv.var.480ioutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.480ioutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.480ioutputwidth", vaxis_width_str, "720");
        property_get("ubootenv.var.480ioutputheight", vaxis_height_str, "480");
        break;
    case OUTPUT_MODE_480P:
        property_get("ubootenv.var.480poutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.480poutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.480poutputwidth", vaxis_width_str, "720");
        property_get("ubootenv.var.480poutputheight", vaxis_height_str, "480");
        break;
    case OUTPUT_MODE_576I:
        property_get("ubootenv.var.576ioutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.576ioutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.576ioutputwidth", vaxis_width_str, "720");
        property_get("ubootenv.var.576ioutputheight", vaxis_height_str, "576");
        break;
    case OUTPUT_MODE_576P:
        property_get("ubootenv.var.576poutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.576poutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.576poutputwidth", vaxis_width_str, "720");
        property_get("ubootenv.var.576poutputheight", vaxis_height_str, "576");
        break;
    case OUTPUT_MODE_720P:
        property_get("ubootenv.var.720poutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.720poutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.720poutputwidth", vaxis_width_str, "1280");
        property_get("ubootenv.var.720poutputheight", vaxis_height_str, "720");
        break;
    case OUTPUT_MODE_1080I:
        property_get("ubootenv.var.1080ioutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.1080ioutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.1080ioutputwidth", vaxis_width_str, "1920");
        property_get("ubootenv.var.1080ioutputheight", vaxis_height_str, "1080");
        break;
    case OUTPUT_MODE_1080P:
        property_get("ubootenv.var.1080poutputx", vaxis_newx_str, "0");
        property_get("ubootenv.var.1080poutputy", vaxis_newy_str, "0");
        property_get("ubootenv.var.1080poutputwidth", vaxis_width_str, "1920");
        property_get("ubootenv.var.1080poutputheight", vaxis_height_str, "1080");
        break;
    case OUTPUT_MODE_4K2K24HZ:
        property_get("ubootenv.var.4k2k24hz_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2k24hz_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2k24hz_width", vaxis_width_str, "3840");
        property_get("ubootenv.var.4k2k24hz_height", vaxis_height_str, "2160");
        break;
    case OUTPUT_MODE_4K2K25HZ:
        property_get("ubootenv.var.4k2k25hz_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2k25hz_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2k25hz_width", vaxis_width_str, "3840");
        property_get("ubootenv.var.4k2k25hz_height", vaxis_height_str, "2160");
        break;
    case OUTPUT_MODE_4K2K30HZ:
        property_get("ubootenv.var.4k2k30hz_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2k30hz_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2k30hz_width", vaxis_width_str, "3840");
        property_get("ubootenv.var.4k2k30hz_height", vaxis_height_str, "2160");
        break;
    case OUTPUT_MODE_4K2KSMPTE:
        property_get("ubootenv.var.4k2ksmpte_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2ksmpte_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2ksmpte_width", vaxis_width_str, "4096");
        property_get("ubootenv.var.4k2ksmpte_height", vaxis_height_str, "2160");
        break;
    default:
        LOGW("UNKNOW MODE:%d", output_mode);
        break;
    }
    *x = atoi(vaxis_newx_str);
    *y = atoi(vaxis_newy_str);
    *width = atoi(vaxis_width_str);
    *height = atoi(vaxis_height_str);
}

void InitOsdScale(int width, int height)
{
    LOGI("InitOsdScale, width: %d, height: %d\n", width, height);
    int x = 0, y = 0, w = 0, h = 0;
    char fsa_bcmd[64] = {0};
    char wa_bcmd[64] = {0};
    
    sprintf(fsa_bcmd, "0 0 %d %d", width-1, height-1);
    LOGI("InitOsdScale, free_scale_axis: %s\n", fsa_bcmd);
    OUTPUT_MODE output_mode = get_display_mode();
    getPosition(output_mode, &x, &y, &w, &h);
    sprintf(wa_bcmd, "%d %d %d %d", x, y, x+w-1, y+h-1);
    LOGI("InitOsdScale, window_axis: %s\n", wa_bcmd);
    
    amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 1);
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/freescale_mode", "1");
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/free_scale_axis", fsa_bcmd);
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/window_axis", wa_bcmd);
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/free_scale", "0x10001");
    amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 0);
}

void reinitOsdScale()
{
    LOGI("reinitOsdScale, old_free_scale_axis: %s\n", old_free_scale_axis);
    LOGI("reinitOsdScale, old_window_axis: %s\n", old_window_axis);
    LOGI("reinitOsdScale, old_free_scale: %s\n", old_free_scale);
    amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 1);
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/freescale_mode", "1");
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/free_scale_axis", old_free_scale_axis);
    amsysfs_set_sysfs_str("/sys/class/graphics/fb0/window_axis", old_window_axis);
    if(!strncmp(old_free_scale, "free_scale_enable:[0x1]", 23)) {
        amsysfs_set_sysfs_str("/sys/class/graphics/fb0/free_scale", "0x10001");
    }
    else {
        amsysfs_set_sysfs_str("/sys/class/graphics/fb0/free_scale", "0x0");
    }
    amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 0);
}

void LunchIptv(bool isSoftFit)
{
    LOGI("LunchIptv\n");
    if(!isSoftFit) {
        //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 1280 720 0 8");
        amsysfs_set_sysfs_str("/sys/class/deinterlace/di0/config", "disable");
        amsysfs_set_sysfs_int("/sys/module/di/parameters/buf_mgr_mode", 0);
    }else {
        amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 0);
    }
}

void QuitIptv(bool isSoftFit)
{
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_hd", 0);
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 0 0 0 0");
    amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);  
    if(!isSoftFit) {
        reinitOsdScale();
    } else {
        amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 0);
    }
    LOGI("QuitIptv\n");
}

int64_t av_gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

CTsPlayer::CTsPlayer()
{
    char vaule[PROPERTY_VALUE_MAX] = {0};
    
    property_get("iptv.shouldshowlog", vaule, "0");//initial the log switch
    prop_shouldshowlog = atoi(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.dumpfile", vaule, "0");
    prop_dumpfile = atoi(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.buffer.time", vaule, "1100");
    prop_buffertime = atoi(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.audio.bufferlevel", vaule, "0.6");
    prop_audiobuflevel = atof(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.video.bufferlevel", vaule, "0.8");
    prop_videobuflevel = atof(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.softfit", vaule, "1");
    prop_softfit = atoi(vaule);
    __android_log_print(ANDROID_LOG_INFO, "TsPlayer", "CTsPlayer, prop_shouldshowlog: %d, prop_buffertime: %d, prop_dumpfile: %d, audio bufferlevel: %f, video bufferlevel: %f, prop_softfit: %d\n", 
            prop_shouldshowlog, prop_buffertime, prop_dumpfile, prop_audiobuflevel, prop_videobuflevel, prop_softfit);
            
    char buf[64] = {0};
    memset(old_free_scale_axis, 0, 64);
    memset(old_window_axis, 0, 64);
    memset(old_free_scale, 0, 64);
    amsysfs_get_sysfs_str("/sys/class/graphics/fb0/free_scale_axis", old_free_scale_axis, 64);
    amsysfs_get_sysfs_str("/sys/class/graphics/fb0/window_axis", buf, 64);
    amsysfs_get_sysfs_str("/sys/class/graphics/fb0/free_scale", old_free_scale, 64);
    
    LOGI("window_axis: %s\n", buf);
    char *pr = strstr(buf, "[");
    if(pr != NULL) {
        int len = strlen(pr);
        int i = 0, j = 0;
        for(i=1; i<len-1; i++) {
            old_window_axis[j++] = pr[i];
        }
        old_window_axis[j] = 0;
    }

    LOGI("free_scale_axis: %s\n", old_free_scale_axis);
    LOGI("window_axis: %s\n", old_window_axis);
    LOGI("free_scale: %s\n", old_free_scale);

    amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);
    amsysfs_set_sysfs_int("/sys/class/video/disable_video", 2);	
    memset(a_aPara, 0, sizeof(AUDIO_PARA_T)*MAX_AUDIO_PARAM_SIZE);
    memset(sPara, 0, sizeof(SUBTITLE_PARA_T)*MAX_SUBTITLE_PARAM_SIZE);
    memset(&vPara, 0, sizeof(vPara));
    memset(&codec, 0, sizeof(codec));
    player_pid = -1;
    pcodec = &codec;
    codec_audio_basic_init();
    lp_lock_init(&mutex, NULL);
    //0:normal��1:full stretch��2:4-3��3:16-9
    amsysfs_set_sysfs_int("/sys/class/video/screen_mode", 1);
    amsysfs_set_sysfs_int("/sys/class/tsync/enable", 1);

    m_bIsPlay = false;
    pfunc_player_evt = NULL;
    m_nOsdBpp = 16;//SYS_get_osdbpp();
    m_nAudioBalance = 3;

    m_nVolume = 100;
    m_bFast = false;
    m_bSetEPGSize = false;
    m_bWrFirstPkg = false;
    m_StartPlayTimePoint = 0;
    m_isSoftFit = (prop_softfit == 1) ? true : false;

    m_StopThread = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&mThread, &attr, threadCheckAbend, this);
    pthread_attr_destroy(&attr);

    m_nMode = M_LIVE;
    LunchIptv(m_isSoftFit);
    m_fp = NULL;
}

CTsPlayer::~CTsPlayer()
{
    m_StopThread = true;
    pthread_join(mThread, NULL);
    QuitIptv(m_isSoftFit);
}

//ȡ�ò���ģʽ,�������ݲ���
int CTsPlayer::GetPlayMode()
{
    return 1;
}

int CTsPlayer::SetVideoWindow(int x,int y,int width,int height)
{
    int epg_centre_x = 0;
    int epg_centre_y = 0;
    int old_videowindow_certre_x = 0;
    int old_videowindow_certre_y = 0;
    int new_videowindow_certre_x = 0;
    int new_videowindow_certre_y = 0;
    int new_videowindow_width = 0;
    int new_videowindow_height = 0;
    char vaxis_newx_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_newy_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_width_str[PROPERTY_VALUE_MAX] = {0};
    char vaxis_height_str[PROPERTY_VALUE_MAX] = {0};
    int vaxis_newx= -1, vaxis_newy = -1, vaxis_width= -1, vaxis_height= -1;
    int fd_axis, fd_mode;
    int x2 = 0, y2 = 0, width2 = 0, height2 = 0;
    int ret = 0;
    const char *path_mode = "/sys/class/video/screen_mode";
    const char *path_axis = "/sys/class/video/axis";
    char bcmd[32];
    char buffer[15];
    int mode_w = 0, mode_h = 0;

    LOGI("CTsPlayer::SetVideoWindow: %d, %d, %d, %d\n", x, y, width, height);
    OUTPUT_MODE output_mode = get_display_mode();
    if(m_isSoftFit) {
        int x_b=0, y_b=0, w_b=0, h_b=0;
        if((output_mode == OUTPUT_MODE_720P) || (output_mode == OUTPUT_MODE_1080I)
                || (output_mode == OUTPUT_MODE_1080P)) {
            x_b = x;
            y_b = y;
            w_b = width + x_b;
            h_b = height + y_b;
        } else if((output_mode == OUTPUT_MODE_4K2K24HZ) || (output_mode == OUTPUT_MODE_4K2K25HZ)
                || (output_mode == OUTPUT_MODE_4K2K30HZ) || (output_mode == OUTPUT_MODE_4K2KSMPTE)) {
            //restore to 1280*720
            x_b = (int)(x*1280/1920);
            y_b = (int)(y*720/1080);
            w_b = (int)(width*1280/1920) + x_b;
            h_b = (int)(height*720/1080) + y_b;
        } else if((output_mode == OUTPUT_MODE_480I) || (output_mode == OUTPUT_MODE_480P)) {  
            //restore to 1280*720
            x_b = (int)(x*1280/720);
            y_b = (int)(y*720/480);
            w_b = (int)(width*1280/720) + x_b;
            h_b = (int)(height*720/480) + y_b;
        } else if((output_mode == OUTPUT_MODE_576I) || (output_mode == OUTPUT_MODE_576P)) { 
            //restore to 1280*720
            x_b = (int)(x*1280/720);
            y_b = (int)(y*720/576);
            w_b = (int)(width*1280/720) + x_b;
            h_b = (int)(height*720/576) + y_b;
        }
        if(m_nEPGWidth !=0 && m_nEPGHeight !=0) {
            amsysfs_set_sysfs_str(path_mode, "1");
        }
        sprintf(bcmd, "%d %d %d %d", x_b, y_b, w_b, h_b);
        ret = amsysfs_set_sysfs_str(path_axis, bcmd);
        LOGI("setvideoaxis: %s\n", bcmd);
        return ret;
    }

    /*adjust axis as rate recurrence*/
    GetVideoPixels(mode_w, mode_h);

    x2 = x*mode_w/m_nEPGWidth;
    width2 = width*mode_w/m_nEPGWidth;
    y2 = y*mode_h/m_nEPGHeight;
    height2 = height*mode_h/m_nEPGHeight;

    old_videowindow_certre_x = x2+int(width2/2);
    old_videowindow_certre_y = y2+int(height2/2);
    
    getPosition(output_mode, &vaxis_newx, &vaxis_newy, &vaxis_width, &vaxis_height);
    LOGI("output_mode: %d, vaxis_newx: %d, vaxis_newy: %d, vaxis_width: %d, vaxis_height: %d\n",
            output_mode, vaxis_newx, vaxis_newy, vaxis_width, vaxis_height);
    epg_centre_x = vaxis_newx+int(vaxis_width/2);
    epg_centre_y = vaxis_newy+int(vaxis_height/2);
    new_videowindow_certre_x = epg_centre_x + int((old_videowindow_certre_x-mode_w/2)*vaxis_width/mode_w);
    new_videowindow_certre_y = epg_centre_y + int((old_videowindow_certre_y-mode_h/2)*vaxis_height/mode_h);
    new_videowindow_width = int(width2*vaxis_width/mode_w);
    new_videowindow_height = int(height2*vaxis_height/mode_h);
    LOGI("CTsPlayer::mode_w = %d, mode_h = %d, mw = %d, mh = %d \n",
            mode_w, mode_h, m_nEPGWidth, m_nEPGHeight);

    if(m_nEPGWidth !=0 && m_nEPGHeight !=0) {
        amsysfs_set_sysfs_str(path_mode, "1");
    }

    sprintf(bcmd, "%d %d %d %d", new_videowindow_certre_x-int(new_videowindow_width/2)-1,
            new_videowindow_certre_y-int(new_videowindow_height/2)-1,
            new_videowindow_certre_x+int(new_videowindow_width/2)+1,
            new_videowindow_certre_y+int(new_videowindow_height/2)+1);            

    ret = amsysfs_set_sysfs_str(path_axis, bcmd);
    LOGI("setvideoaxis: %s\n", bcmd);

    if((width2 > 0)&&(height2 > 0)&&((width2 < (mode_w -10))||(height2< (mode_h -10))))
        amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_hd",1);
    else
        amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_hd",0);
    return ret;
}

int CTsPlayer::VideoShow(void)
{
    LOGI("VideoShow\n");
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 1280 720 0 8");
    return 0;
}

int CTsPlayer::VideoHide(void)
{
    LOGI("VideoHide\n");
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 0 0 0 0");
    return 0;
}

void CTsPlayer::InitVideo(PVIDEO_PARA_T pVideoPara)
{
    vPara=*pVideoPara;
    LOGI("InitVideo vPara->pid: %d, vPara->vFmt: %d\n", vPara.pid, vPara.vFmt);
}

void CTsPlayer::InitAudio(PAUDIO_PARA_T pAudioPara)
{
    PAUDIO_PARA_T pAP = pAudioPara;
    int count = 0;

    LOGI("InitAudio\n");
    memset(a_aPara,0,sizeof(AUDIO_PARA_T)*MAX_AUDIO_PARAM_SIZE);
    while((pAP->pid != 0)&&(count<MAX_AUDIO_PARAM_SIZE)) {
        a_aPara[count]= *pAP;
        LOGI("InitAudio pAP->pid: %d, pAP->aFmt: %d\n", pAP->pid, pAP->aFmt);
        pAP++;
        count++;
    }
    return ;
}

void CTsPlayer::InitSubtitle(PSUBTITLE_PARA_T pSubtitlePara)
{
    int count = 0;

    LOGI("InitSubtitle\n");
    memset(sPara,0,sizeof(SUBTITLE_PARA_T)*MAX_SUBTITLE_PARAM_SIZE);
    while((pSubtitlePara->pid != 0)&&(count<MAX_SUBTITLE_PARAM_SIZE)) {
        sPara[count]= *pSubtitlePara;
        LOGI("InitSubtitle pSubtitlePara->pid:%d\n",pSubtitlePara->pid);
        pSubtitlePara++;
        count++;
    }
    amsysfs_set_sysfs_int("/sys/class/subtitle/total",count);
    return ;
}

void setSubType(PSUBTITLE_PARA_T pSubtitlePara)
{
    if(!pSubtitlePara)
        return;
    LOGI("setSubType pSubtitlePara->pid:%d pSubtitlePara->sub_type:%d\n",pSubtitlePara->pid,pSubtitlePara->sub_type);
    if (pSubtitlePara->sub_type== CODEC_ID_DVD_SUBTITLE) {
        set_subtitle_subtype(0);
    } else if (pSubtitlePara->sub_type== CODEC_ID_HDMV_PGS_SUBTITLE) {
        set_subtitle_subtype(1);
    } else if (pSubtitlePara->sub_type== CODEC_ID_XSUB) {
        set_subtitle_subtype(2);
    } else if (pSubtitlePara->sub_type == CODEC_ID_TEXT || \
                pSubtitlePara->sub_type == CODEC_ID_SSA) {
        set_subtitle_subtype(3);
    } else if (pSubtitlePara->sub_type == CODEC_ID_DVB_SUBTITLE) {
        set_subtitle_subtype(5);
    } else {
        set_subtitle_subtype(4);
    }
}

#define FILTER_AFMT_MPEG		(1 << 0)
#define FILTER_AFMT_PCMS16L	    (1 << 1)
#define FILTER_AFMT_AAC			(1 << 2)
#define FILTER_AFMT_AC3			(1 << 3)
#define FILTER_AFMT_ALAW		(1 << 4)
#define FILTER_AFMT_MULAW		(1 << 5)
#define FILTER_AFMT_DTS			(1 << 6)
#define FILTER_AFMT_PCMS16B		(1 << 7)
#define FILTER_AFMT_FLAC		(1 << 8)
#define FILTER_AFMT_COOK		(1 << 9)
#define FILTER_AFMT_PCMU8		(1 << 10)
#define FILTER_AFMT_ADPCM		(1 << 11)
#define FILTER_AFMT_AMR			(1 << 12)
#define FILTER_AFMT_RAAC		(1 << 13)
#define FILTER_AFMT_WMA			(1 << 14)
#define FILTER_AFMT_WMAPRO		(1 << 15)
#define FILTER_AFMT_PCMBLU		(1 << 16)
#define FILTER_AFMT_ALAC		(1 << 17)
#define FILTER_AFMT_VORBIS		(1 << 18)
#define FILTER_AFMT_AAC_LATM		(1 << 19)
#define FILTER_AFMT_APE		       (1 << 20)
#define FILTER_AFMT_EAC3		       (1 << 21)

int TsplayerGetAFilterFormat(const char *prop)
{
    char value[PROPERTY_VALUE_MAX];
    int filter_fmt = 0;
    /* check the dts/ac3 firmware status */
    if(access("/system/etc/firmware/audiodsp_codec_ddp_dcv.bin",F_OK)) {
        filter_fmt |= (FILTER_AFMT_AC3|FILTER_AFMT_EAC3);
    }
    if(access("/system/etc/firmware/audiodsp_codec_dtshd.bin",F_OK) ) {
        filter_fmt  |= FILTER_AFMT_DTS;
    }
    if(property_get(prop, value, NULL) > 0) {
        LOGI("[%s:%d]disable_adec=%s\n", __FUNCTION__, __LINE__, value);
        if(strstr(value,"mpeg") != NULL || strstr(value,"MPEG") != NULL) {
            filter_fmt |= FILTER_AFMT_MPEG;
        }
        if(strstr(value,"pcms16l") != NULL || strstr(value,"PCMS16L") != NULL) {
            filter_fmt |= FILTER_AFMT_PCMS16L;
        }
        if(strstr(value,"aac") != NULL || strstr(value,"AAC") != NULL) {
            filter_fmt |= FILTER_AFMT_AAC;
        }
        if(strstr(value,"ac3") != NULL || strstr(value,"AC#") != NULL) {
            filter_fmt |= FILTER_AFMT_AC3;
        }
        if(strstr(value,"alaw") != NULL || strstr(value,"ALAW") != NULL) {
            filter_fmt |= FILTER_AFMT_ALAW;
        }
        if(strstr(value,"mulaw") != NULL || strstr(value,"MULAW") != NULL) {
            filter_fmt |= FILTER_AFMT_MULAW;
        }
        if(strstr(value,"dts") != NULL || strstr(value,"DTS") != NULL) {
            filter_fmt |= FILTER_AFMT_DTS;
        }
        if(strstr(value,"pcms16b") != NULL || strstr(value,"PCMS16B") != NULL) {
            filter_fmt |= FILTER_AFMT_PCMS16B;
        }
        if(strstr(value,"flac") != NULL || strstr(value,"FLAC") != NULL) {
            filter_fmt |= FILTER_AFMT_FLAC;
        }
        if(strstr(value,"cook") != NULL || strstr(value,"COOK") != NULL) {
            filter_fmt |= FILTER_AFMT_COOK;
        }
        if(strstr(value,"pcmu8") != NULL || strstr(value,"PCMU8") != NULL) {
            filter_fmt |= FILTER_AFMT_PCMU8;
        }
        if(strstr(value,"adpcm") != NULL || strstr(value,"ADPCM") != NULL) {
            filter_fmt |= FILTER_AFMT_ADPCM;
        }
        if(strstr(value,"amr") != NULL || strstr(value,"AMR") != NULL) {
            filter_fmt |= FILTER_AFMT_AMR;
        }
        if(strstr(value,"raac") != NULL || strstr(value,"RAAC") != NULL) {
            filter_fmt |= FILTER_AFMT_RAAC;
        }
        if(strstr(value,"wma") != NULL || strstr(value,"WMA") != NULL) {
            filter_fmt |= FILTER_AFMT_WMA;
        }
        if(strstr(value,"wmapro") != NULL || strstr(value,"WMAPRO") != NULL) {
            filter_fmt |= FILTER_AFMT_WMAPRO;
        }
        if(strstr(value,"pcmblueray") != NULL || strstr(value,"PCMBLUERAY") != NULL) {
            filter_fmt |= FILTER_AFMT_PCMBLU;
        }
        if(strstr(value,"alac") != NULL || strstr(value,"ALAC") != NULL) {
            filter_fmt |= FILTER_AFMT_ALAC;
        }
        if(strstr(value,"vorbis") != NULL || strstr(value,"VORBIS") != NULL) {
            filter_fmt |= FILTER_AFMT_VORBIS;
        }
        if(strstr(value,"aac_latm") != NULL || strstr(value,"AAC_LATM") != NULL) {
            filter_fmt |= FILTER_AFMT_AAC_LATM;
        }
        if(strstr(value,"ape") != NULL || strstr(value,"APE") != NULL) {
            filter_fmt |= FILTER_AFMT_APE;
        }
        if(strstr(value,"eac3") != NULL || strstr(value,"EAC3") != NULL) {
            filter_fmt |= FILTER_AFMT_EAC3;
        }  
    }
    LOGI("[%s:%d]filter_afmt=%x\n", __FUNCTION__, __LINE__, filter_fmt);
    return filter_fmt;
}

bool CTsPlayer::StartPlay()
{
    int ret;
    int filter_afmt;
    char vaule[PROPERTY_VALUE_MAX] = {0};

    memset(pcodec,0,sizeof(*pcodec));
    pcodec->stream_type = STREAM_TYPE_TS;
    pcodec->video_type = vPara.vFmt;
    pcodec->has_video = 1;
    pcodec->audio_type = a_aPara[0].aFmt;
    
    property_get("iptv.hasaudio", vaule, "1");
    hasaudio = atoi(vaule);

    memset(vaule, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.hasvideo", vaule, "1");
    hasvideo = atoi(vaule);

    if(pcodec->audio_type == 19) {
        pcodec->audio_type = AFORMAT_EAC3;
    }

    if(IS_AUIDO_NEED_EXT_INFO(pcodec->audio_type)) {
        pcodec->audio_info.valid = 1;
        LOGI("set audio_info.valid to 1");
    }

    if(!m_bFast) {
        if((int)a_aPara[0].pid != 0) {
            pcodec->has_audio = 1;
            pcodec->audio_pid = (int)a_aPara[0].pid;
        }
        LOGI("pcodec->audio_samplerate: %d, pcodec->audio_channels: %d\n",
                pcodec->audio_samplerate, pcodec->audio_channels);

        if((int)sPara[0].pid != 0) {
            pcodec->has_sub = 1;
            pcodec->sub_pid = (int)sPara[0].pid;
            setSubType(&sPara[0]);
        }
        LOGI("pcodec->sub_pid: %d \n", pcodec->sub_pid);
    }

    pcodec->video_pid = (int)vPara.pid;
    if(pcodec->video_type == VFORMAT_H264) {
        pcodec->am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
        pcodec->am_sysinfo.param = (void *)(0);
    }

    filter_afmt = TsplayerGetAFilterFormat("media.amplayer.disable-acodecs");
    if(((1 << pcodec->audio_type) & filter_afmt) != 0) {
        LOGI("## filtered format audio_format=%d,----\n", pcodec->audio_type);
        pcodec->has_audio = 0;
    }
    if(hasaudio == 0)
        pcodec->has_audio = 0;
    if(hasvideo == 0)
        pcodec->has_video = 0;
    LOGI("set %d, %d, %d, %d\n",vPara.vFmt, a_aPara[0].aFmt, vPara.pid, a_aPara[0].pid);
    pcodec->noblock = 0;

    if(prop_dumpfile){
        if(m_fp == NULL)
            m_fp = fopen("/storage/external_storage/sda1/Live.ts", "wb+");
    }

    /*other setting*/
    lp_lock(&mutex);
    ret = codec_init(pcodec);
    LOGI("StartPlay codec_init After: %d\n", ret);
    lp_unlock(&mutex);

    if(ret == 0) {
        if(m_nMode == M_LIVE)
            amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);
        m_bIsPlay = true;
        if(!m_bFast) {
            LOGI("StartPlay: codec_pause to buffer sometime");
            codec_pause(pcodec);
        }
    }
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole","0 0 1280 720 0 8");
    m_bWrFirstPkg = true;
    writecount = 0;
    m_StartPlayTimePoint = av_gettime();
    LOGI("StartPlay: m_StartPlayTimePoint = %d\n", m_StartPlayTimePoint);
    return !ret;
}

int CTsPlayer::WriteData(unsigned char* pBuffer, unsigned int nSize)
{
    int ret = -1;
    static int retry_count = 0;
    buf_status audio_buf;
    buf_status video_buf;
    float audio_buf_level;
    float video_buf_level;

    if(!m_bIsPlay)
        return -1;

    codec_get_vbuf_state(pcodec, &audio_buf);
    codec_get_vbuf_state(pcodec, &video_buf);
    audio_buf_level = (float)audio_buf.data_len / audio_buf.size;
    video_buf_level = (float)video_buf.data_len / video_buf.size;
	
    if(m_StartPlayTimePoint > 0)
        LOGI("WriteData: audio_buf.data_len: %d, video_buf.data_len: %d!\n", audio_buf.data_len, video_buf.data_len);
    /*if(m_bWrFirstPkg == false) {
        if(pcodec->has_video) {
            if(pcodec->video_type == VFORMAT_MJPEG) {
                if(video_buf.data_len < (RES_VIDEO_SIZE >> 2)) {
                    if(pfunc_player_evt != NULL) {
                        pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                    }
                }
            } else {
                if(video_buf.data_len< RES_VIDEO_SIZE) {
                    if(pfunc_player_evt != NULL) {
                        pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                    }
                }
            }
        }

        if(pcodec->has_audio) {
            if(audio_buf.data_len < RES_AUDIO_SIZE) {
                if(pfunc_player_evt != NULL) {
                    pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                }
            }
        }
    }*/

    lp_lock(&mutex);
    for(int retry_count=0; retry_count<10; retry_count++) {
        ret = codec_write(pcodec, pBuffer, nSize);
        if((ret < 0) || (ret > nSize)) {
            if(ret == EAGAIN) {
                usleep(10);
                LOGI("WriteData: codec_write return EAGAIN!\n");
                continue;
            } else {
                LOGI("WriteData: codec_write return %d!\n", ret);
                if((pcodec->handle)&&(pcodec->adec_priv)) {
                    ret = codec_close(pcodec);
                    ret = codec_init(pcodec);
                    LOGI("WriteData: codec need close and reinit!\n");
                } else {
                    LOGI("WriteData: codec_write return error or stop by called!\n");
                    break;
                }
            }
        } else {
            LOGI("WriteData: codec_write no error and nSize is %d!\n", nSize);
            break;
        }
    }

    if(!m_bFast && m_StartPlayTimePoint > 0 && (((av_gettime() - m_StartPlayTimePoint)/1000 >= prop_buffertime) 
            || (audio_buf_level >= prop_audiobuflevel || video_buf_level >= prop_videobuflevel))) {
        LOGI("WriteData: resume play now!\n");
        codec_resume(pcodec);
        m_StartPlayTimePoint = 0;
    }
    lp_unlock(&mutex);

    if(ret > 0) {
        if(m_fp != NULL) {
            fwrite(pBuffer, 1, nSize, m_fp);
        }
        if(writecount >= MAX_WRITE_COUNT) {
            m_bWrFirstPkg = false;
            writecount = 0;
        }

        if(m_bWrFirstPkg == true) {
            writecount++;
        }
    } else {
        return -1;
    }
    return ret;
}

bool CTsPlayer::Pause()
{
    codec_pause(pcodec);
    return true;
}

bool CTsPlayer::Resume()
{
    codec_resume(pcodec);
    return true;
}

bool CTsPlayer::Fast()
{
    int ret;

    LOGI("Fast");
    ret = amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 0);
    if(ret)
        return false;
    Stop();
    m_bFast = true;
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 1);
    ret = StartPlay();
    if(!ret)
        return false;

    LOGI("Fast: codec_set_cntl_mode: %d\n", TRICKMODE_I);
    ret = codec_set_cntl_mode(pcodec, TRICKMODE_I);
    return !ret;
}

bool CTsPlayer::StopFast()
{
    int ret;

    LOGI("StopFast");
    m_bFast = false;
    ret = codec_set_cntl_mode(pcodec, TRICKMODE_NONE);
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
    Stop();
    ret = StartPlay();
    if(!ret)
        return false;

    ret = amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);
    if(ret)
        return false;

    return true;
}

bool CTsPlayer::Stop()
{    
    int ret;

    if(m_bIsPlay) {
        if(m_fp != NULL) {
            fclose(m_fp);
            m_fp = NULL;
        }

        m_bFast = false;
        m_bIsPlay = false;
        m_StartPlayTimePoint = 0;
        ret = codec_set_cntl_mode(pcodec, TRICKMODE_NONE);
        amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
        LOGI("m_bIsPlay is true");

        lp_lock(&mutex);
        ret = codec_close(pcodec);
        pcodec->handle = -1;
        LOGI("Stop  codec_close After:%d\n", ret);
        lp_unlock(&mutex);
    } else {
        LOGI("m_bIsPlay is false");
    }
    m_bWrFirstPkg = true;
    return true;
}

bool CTsPlayer::Seek()
{
    LOGI("Seek");
    amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);
    Stop();
    usleep(500*1000);
    StartPlay();
    return true;
}

int CTsPlayer::GetVolume()
{
    float volume = 1.0f;
    int ret;

    LOGI("GetVolume");
    ret = codec_get_volume(pcodec, &volume);
    if(ret < 0) {
        return m_nVolume;
    }
    int nVolume = volume * 100;
    if(nVolume <= 0)
        return m_nVolume;
    
    return (int)(volume*100);
}

bool CTsPlayer::SetVolume(int volume)
{
    LOGI("SetVolume");
    int ret = codec_set_volume(pcodec, (float)volume/100.0);
    m_nVolume = volume;
    return true;
}

//��ȡ��ǰ����,1:��������2:��������3:˫����
int CTsPlayer::GetAudioBalance()
{
    return m_nAudioBalance;
}

//��������
//nAudioBlance:,1:��������2:��������3:˫����
bool CTsPlayer::SetAudioBalance(int nAudioBalance)
{
    if((nAudioBalance < 1) && (nAudioBalance > 3))
        return false;
    m_nAudioBalance = nAudioBalance;
    if(nAudioBalance == 1) {
        LOGI("SetAudioBalance 1\n");
        codec_left_mono(pcodec);
    } else if(nAudioBalance == 2) {
        LOGI("SetAudioBalance 2\n");
        codec_right_mono(pcodec);
    } else if(nAudioBalance == 3) {
        LOGI("SetAudioBalance 3\n");
        codec_stereo(pcodec);
    }
    return true;
}

void CTsPlayer::GetVideoPixels(int& width, int& height)
{
    OUTPUT_MODE output_mode = get_display_mode();
    if((output_mode == OUTPUT_MODE_480I) || (output_mode == OUTPUT_MODE_480P)) {
        width = 720;
        height = 480;
    }
    else if((output_mode == OUTPUT_MODE_576I) || (output_mode == OUTPUT_MODE_576P)) {
        width = 720;
        height = 576;
    }
    else if(output_mode == OUTPUT_MODE_720P) {
        width = 1280;
        height = 720;
    }
    else if((output_mode == OUTPUT_MODE_1080I) || (output_mode == OUTPUT_MODE_1080P)) {
        width = 1920;
        height = 1080;
    }
    else {
        width = 1280;
        height = 720;
    }
    LOGI("GetVideoPixels, width: %d, height: %d", width, height);
}

bool CTsPlayer::SetRatio(int nRatio)
{
    char writedata[40] = {0};
    int width = 0;
    int height = 0;
    int new_x = 0;
    int new_y = 0;
    int new_width = 0;
    int new_height = 0;
    int mode_width = 0;
    int mode_height = 0;
    vdec_status vdec;
    codec_get_vdec_state(pcodec,&vdec);
    width = vdec.width;
    height = vdec.height;

    LOGI("SetRatio width: %d, height: %d\n", width, height);
    GetVideoPixels(mode_width, mode_height);

    if(nRatio == 1) {	 //Full screen
        new_x = 0;
        new_y = 0;
        new_width = mode_width;
        new_height = mode_height;
        amsysfs_set_sysfs_int("/sys/class/video/screen_mode", 1);
        sprintf(writedata, "%d %d %d %d", new_x, new_y, new_width, new_height);
        amsysfs_set_sysfs_str("/sys/class/video/axis", writedata);
        return true;
    } else if(nRatio == 2) {	//Fit by width
        amsysfs_set_sysfs_int("/sys/class/video/screen_mode", 1);
        new_width = mode_width;
        new_height = int(mode_width*height/width);
        new_x = 0;
        new_y = int((mode_height-new_height)/2);
        LOGI("SetRatio new_x: %d, new_y: %d, new_width: %d, new_height: %d\n"
                , new_x, new_y, new_width, new_height);
        sprintf(writedata, "%d %d %d %d", new_x, new_y, new_x+new_width-1, new_y+new_height-1);
        amsysfs_set_sysfs_str("/sys/class/video/axis",writedata);
        return true;
    } else if(nRatio == 3) {	//Fit by height
        amsysfs_set_sysfs_int("/sys/class/video/screen_mode", 1);
        new_width = int(mode_height*width/height);
        new_height = mode_height;
        new_x = int((mode_width - new_width)/2);
        new_y = 0;
        LOGI("SetRatio new_x: %d, new_y: %d, new_width: %d, new_height: %d\n"
                , new_x, new_y, new_width, new_height);
        sprintf(writedata, "%d %d %d %d", new_x, new_y, new_x+new_width-1, new_y+new_height-1);
        amsysfs_set_sysfs_str("/sys/class/video/axis", writedata);
        return true;
    } else if(nRatio == 255) {
        amsysfs_set_sysfs_int("/sys/class/video/disable_video", 1);
        return true;
    }
    return false;
}

bool CTsPlayer::IsSoftFit()
{
    return m_isSoftFit;
}

void CTsPlayer::SetEPGSize(int w, int h)
{
    LOGI("SetEPGSize: w=%d, h=%d, m_bIsPlay=%d\n", w, h, m_bIsPlay);
    m_nEPGWidth = w;
    m_nEPGHeight = h;
    if(!m_isSoftFit && !m_bIsPlay){
        InitOsdScale(m_nEPGWidth, m_nEPGHeight);
    }
}

void CTsPlayer::SwitchAudioTrack(int pid)
{
    int count = 0;

    while((a_aPara[count].pid != pid) &&(a_aPara[count].pid != 0)
            &&(count < MAX_AUDIO_PARAM_SIZE)) {
        count++;
    }

    if(!m_bIsPlay)
        return;

    codec_audio_automute(pcodec->adec_priv, 1);
    codec_close_audio(pcodec);
    pcodec->audio_pid = 0xffff;

    if(codec_set_audio_pid(pcodec)) {
        LOGE("set invalid audio pid failed\n");
        return;
    }

    if(count < MAX_AUDIO_PARAM_SIZE) {
        pcodec->has_audio = 1;
        pcodec->audio_type = a_aPara[count].aFmt;
        pcodec->audio_pid = (int)a_aPara[count].pid;
    }
    LOGI("SwitchAudioTrack pcodec->audio_samplerate: %d, pcodec->audio_channels: %d\n", pcodec->audio_samplerate, pcodec->audio_channels);
    LOGI("SwitchAudioTrack pcodec->audio_type: %d, pcodec->audio_pid: %d\n", pcodec->audio_type, pcodec->audio_pid);

    //codec_set_audio_pid(pcodec);
    if(IS_AUIDO_NEED_EXT_INFO(pcodec->audio_type)) {
        pcodec->audio_info.valid = 1;
        LOGI("set audio_info.valid to 1");
    }

    if(codec_audio_reinit(pcodec)) {
        LOGE("reset init failed\n");
        return;
    }

    if(codec_reset_audio(pcodec)) {
        LOGE("reset audio failed\n");
        return;
    }
    codec_resume_audio(pcodec, 1);
    codec_audio_automute(pcodec->adec_priv, 0);
}

void CTsPlayer::SwitchSubtitle(int pid) 
{
    LOGI("SwitchSubtitle be called pid is %d\n", pid);
    /* first set an invalid sub id */
    pcodec->sub_pid = 0xffff;
    if(codec_set_sub_id(pcodec)) {
        LOGE("[%s:%d]set invalid sub pid failed\n", __FUNCTION__, __LINE__);
        return;
    }
    int count=0;
    PSUBTITLE_PARA_T pSubtitlePara=sPara;
    while((pSubtitlePara->pid != 0) && (count < MAX_SUBTITLE_PARAM_SIZE)) {
        if(pSubtitlePara->pid == pid){
            setSubType(pSubtitlePara);
            break;
        }
        count++;
        pSubtitlePara++;
    }
    /* reset sub */
    pcodec->sub_pid = pid;
    if(codec_set_sub_id(pcodec)) {
        LOGE("[%s:%d]set invalid sub pid failed\n", __FUNCTION__, __LINE__);
        return;
    }

    if(codec_reset_subtile(pcodec)) {
        LOGE("[%s:%d]reset subtile failed\n", __FUNCTION__, __LINE__);
    }
}

void CTsPlayer::SetProperty(int nType, int nSub, int nValue) 
{

}

long CTsPlayer::GetCurrentPlayTime() 
{
    long video_pts = 0;
    video_pts = codec_get_vpts(pcodec);
    return video_pts;
}

void CTsPlayer::leaveChannel()
{
    LOGI("leaveChannel be call\n");
    Stop();
}

void CTsPlayer::SetSurface(Surface* pSurface)
{
    LOGI("SetSurface pSurface: %p\n", pSurface);
    sp<IGraphicBufferProducer> mGraphicBufProducer;
    sp<ANativeWindow> mNativeWindow;
    mGraphicBufProducer = pSurface->getIGraphicBufferProducer();
    if(mGraphicBufProducer != NULL) {
        mNativeWindow = new Surface(mGraphicBufProducer);
    } else {
        LOGE("SetSurface, mGraphicBufProducer is NULL!\n");
        return;
    }
    native_window_set_buffer_count(mNativeWindow.get(), 4);
    native_window_set_usage(mNativeWindow.get(), GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP | GRALLOC_USAGE_AML_VIDEO_OVERLAY);
    native_window_set_buffers_format(mNativeWindow.get(), WINDOW_FORMAT_RGBA_8888);
}

void CTsPlayer::playerback_register_evt_cb(IPTV_PLAYER_EVT_CB pfunc, void *hander)
{
    pfunc_player_evt = pfunc;
    player_evt_hander = hander;
}

void CTsPlayer::checkAbend() {
    int ret = 0;
    buf_status audio_buf;
    buf_status video_buf;

    if(!m_bWrFirstPkg){
        bool checkAudio = true;
        codec_get_abuf_state(pcodec, &audio_buf);
        codec_get_vbuf_state(pcodec, &video_buf);
        LOGI("checkAbend pcodec->video_type is %d, video_buf.data_len is %d\n", pcodec->video_type, video_buf.data_len);
        if(pcodec->has_video) {
            if(pcodec->video_type == VFORMAT_MJPEG) {
                if(video_buf.data_len < (RES_VIDEO_SIZE >> 2)) {
                    if(pfunc_player_evt != NULL) {
                        pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                    }
                    checkAudio = false;
                    LOGW("checkAbend video low level\n");
                }
            }
            else {
                if(video_buf.data_len< RES_VIDEO_SIZE) {
                    if(pfunc_player_evt != NULL) {
                        pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                    }
                    checkAudio = false;
                    LOGW("checkAbend video low level\n");
                }
            }
        }
        LOGI("checkAbend audio_buf.data_len is %d\n", audio_buf.data_len);
        if(pcodec->has_audio && checkAudio) {
            if(audio_buf.data_len < RES_AUDIO_SIZE) {
                if(pfunc_player_evt != NULL) {
                    pfunc_player_evt(IPTV_PLAYER_EVT_ABEND, player_evt_hander);
                }
                LOGW("checkAbend audio low level\n");
            }
        }
    }
}

void *CTsPlayer::threadCheckAbend(void *pthis) {
    LOGV("threadCheckAbend start pthis: %p\n", pthis);
    CTsPlayer *tsplayer = static_cast<CTsPlayer *>(pthis);
    do {
        //usleep(1000 * 1000);
        sleep(2);
        tsplayer->checkAbend();
    }
    while(!m_StopThread);
    LOGV("threadCheckAbend end\n");
    return NULL;
}