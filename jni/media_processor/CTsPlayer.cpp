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

#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <media/IMediaPlayerService.h>

using namespace android;

#define M_LIVE	1
#define M_TVOD	2
#define M_VOD	3
#define RES_VIDEO_SIZE 256
#define RES_AUDIO_SIZE 64
#define MAX_WRITE_COUNT 20

#define MAX_WRITE_ALEVEL 0.99
#define MAX_WRITE_VLEVEL 0.99 
#define READ_SIZE (64 * 1024)
#define CTC_BUFFER_LOOP_NSIZE 1316

static bool m_StopThread = false;

//log switch
static int prop_shouldshowlog = 0;
static int prop_playerwatchdog_support =0;
int prop_dumpfile = 0;
int prop_buffertime = 0;
int hasaudio = 0;
int hasvideo = 0;
int prop_softfit = 0;
int prop_blackout_policy = 1; 
float prop_audiobuflevel = 0.0;
float prop_videobuflevel = 0.0;
int prop_audiobuftime = 1000;
int prop_videobuftime = 1000;
int prop_show_first_frame_nosync = 0;
int keep_vdec_mem = 0;

int checkcount = 0;
int buffersize = 0;
char old_free_scale_axis[64] = {0};
char old_window_axis[64] = {0};
char old_free_scale[64] = {0};
static LPBUFFER_T lpbuffer_st;
//unsigned int am_sysinfo_param =0x08;

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

#ifdef TELECOM_VFORMAT_SUPPORT
/* 
//telecom video format define
typedef enum {
    VFORMAT_UNKNOWN = -1,
    VFORMAT_MPEG12 = 0,
    VFORMAT_MPEG4 = 1,
    VFORMAT_H264 = 2,
    VFORMAT_MJPEG = 3,
    VFORMAT_REAL = 4,
    VFORMAT_JPEG = 5,
    VFORMAT_VC1 = 6,
    VFORMAT_AVS = 7,
    VFORMAT_H265 = 8,
    VFORMAT_SW = 9,
    VFORMAT_UNSUPPORT,
    VFORMAT_MAX
} vformat_t;
*/
#define CT_VFORMAT_H265 8
#define CT_VFORMAT_SW 9
#define CT_VFORMAT_UNSUPPORT 10
/*
//telecom audio format define
typedef enum {
    AFORMAT_UNKNOWN2 = -2,
    AFORMAT_UNKNOWN = -1,
    AFORMAT_MPEG   = 0,
    AFORMAT_PCM_S16LE = 1,
    AFORMAT_AAC   = 2,
    AFORMAT_AC3   = 3,
    AFORMAT_ALAW = 4,
    AFORMAT_MULAW = 5,
    AFORMAT_DTS = 6,
    AFORMAT_PCM_S16BE = 7,
    AFORMAT_FLAC = 8,
    AFORMAT_COOK = 9,
    AFORMAT_PCM_U8 = 10,
    AFORMAT_ADPCM = 11,
    AFORMAT_AMR  = 12,
    AFORMAT_RAAC  = 13,
    AFORMAT_WMA  = 14,
    AFORMAT_WMAPRO   = 15,
    AFORMAT_PCM_BLURAY  = 16,
    AFORMAT_ALAC  = 17,
    AFORMAT_VORBIS    = 18,
    AFORMAT_DDPlUS = 19,
    AFORMAT_UNSUPPORT,
    AFORMAT_MAX
} aformat_t;
*/
#define CT_AFORMAT_UNKNOWN2 -2
#define CT_AFORMAT_DDPlUS 19
#define CT_AFORMAT_UNSUPPORT 20

vformat_t changeVformat(vformat_t index) 
{
    LOGI("changeVformat, vfromat: %d\n", index);
    if(index == CT_VFORMAT_H265)
        return VFORMAT_HEVC;
    else if(index == CT_VFORMAT_SW)
        return VFORMAT_SW;

    if(index >= CT_VFORMAT_UNSUPPORT)
        return VFORMAT_UNSUPPORT;
    else
        return index;
}

aformat_t changeAformat(aformat_t index) 
{
    LOGI("changeAformat, afromat: %d\n", index);
    if(index == CT_AFORMAT_UNKNOWN2)
        return AFORMAT_UNKNOWN;
    else if(index == CT_AFORMAT_DDPlUS)
        return AFORMAT_EAC3;
 
    if(index >= CT_AFORMAT_UNSUPPORT)
        return AFORMAT_UNSUPPORT;
    else
        return index;
}
#endif

typedef enum {
    OUTPUT_MODE_480I = 0,
    OUTPUT_MODE_480P,
    OUTPUT_MODE_576I,
    OUTPUT_MODE_576P,
    OUTPUT_MODE_720P,
    OUTPUT_MODE_1080I,
    OUTPUT_MODE_1080P,
    OUTPUT_MODE_4K2K,
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
        } else if(!strncmp(mode, "2160p", 5)) {
            return OUTPUT_MODE_4K2K;
        } else if(!strncmp(mode, "smpte", 5)) {
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
        property_get("ubootenv.var.480i_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.480i_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.480i_w", vaxis_width_str, "720");
        property_get("ubootenv.var.480i_h", vaxis_height_str, "480");
        break;
    case OUTPUT_MODE_480P:
        property_get("ubootenv.var.480p_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.480p_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.480p_w", vaxis_width_str, "720");
        property_get("ubootenv.var.480p_h", vaxis_height_str, "480");
        break;
    case OUTPUT_MODE_576I:
        property_get("ubootenv.var.576i_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.576i_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.576i_w", vaxis_width_str, "720");
        property_get("ubootenv.var.576i_h", vaxis_height_str, "576");
        break;
    case OUTPUT_MODE_576P:
        property_get("ubootenv.var.576p_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.576p_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.576p_w", vaxis_width_str, "720");
        property_get("ubootenv.var.576p_h", vaxis_height_str, "576");
        break;
    case OUTPUT_MODE_720P:
        property_get("ubootenv.var.720p_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.720p_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.720p_w", vaxis_width_str, "1280");
        property_get("ubootenv.var.720p_h", vaxis_height_str, "720");
        break;
    case OUTPUT_MODE_1080I:
        property_get("ubootenv.var.1080i_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.1080i_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.1080i_w", vaxis_width_str, "1920");
        property_get("ubootenv.var.1080i_h", vaxis_height_str, "1080");
        break;
    case OUTPUT_MODE_1080P:
        property_get("ubootenv.var.1080p_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.1080p_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.1080p_w", vaxis_width_str, "1920");
        property_get("ubootenv.var.1080p_h", vaxis_height_str, "1080");
        break;
    case OUTPUT_MODE_4K2K:
        property_get("ubootenv.var.4k2k_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2k_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2k_w", vaxis_width_str, "3840");
        property_get("ubootenv.var.4k2k_h", vaxis_height_str, "2160");
        break;
    case OUTPUT_MODE_4K2KSMPTE:
        property_get("ubootenv.var.4k2ksmpte_x", vaxis_newx_str, "0");
        property_get("ubootenv.var.4k2ksmpte_y", vaxis_newy_str, "0");
        property_get("ubootenv.var.4k2ksmpte_w", vaxis_width_str, "4096");
        property_get("ubootenv.var.4k2ksmpte_h", vaxis_height_str, "2160");
        break;
    default:
    	  *x = 0;
    	  *y = 0;
    	  *width = 1280;
    	  *height = 720;
        LOGW("UNKNOW MODE:%d", output_mode);
        return;
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
    LOGI("LunchIptv isSoftFit:%d\n", isSoftFit);
    if(!isSoftFit) {
        //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 1280 720 0 8");
        amsysfs_set_sysfs_str("/sys/class/deinterlace/di0/config", "disable");
        amsysfs_set_sysfs_int("/sys/module/di/parameters/buf_mgr_mode", 0);
    }else {
        amsysfs_set_sysfs_int("/sys/class/graphics/fb0/blank", 0);
    }
}

void QuitIptv(bool isSoftFit, bool isBlackoutPolicy)
{
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_hd", 0);
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 0 0 0 0");
    if(isBlackoutPolicy)
        amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 1);
    else
        amsysfs_set_sysfs_int("/sys/class/video/disable_video", 1);
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
int sysfs_get_long(char *path, unsigned long  *val)
{
    char buf[64];

    if (amsysfs_get_sysfs_str(path, buf, sizeof(buf)) == -1) {
        LOGI("unable to open file %s,err: %s", path, strerror(errno));
        return -1; 
    }   
    if (sscanf(buf, "0x%lx", val) < 1) {
        LOGI("unable to get pts from: %s", buf);
        return -1; 
    }   
    return 0;
}
CTsPlayer::CTsPlayer()
{
    char value[PROPERTY_VALUE_MAX] = {0};
    
    property_get("iptv.shouldshowlog", value, "1");//initial the log switch
    prop_shouldshowlog = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.dumpfile", value, "0");
    prop_dumpfile = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.buffer.time", value, "2300");
    prop_buffertime = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.audio.bufferlevel", value, "0.6");
    prop_audiobuflevel = atof(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.video.bufferlevel", value, "0.8");
    prop_videobuflevel = atof(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.audio.buffertime", value, "1000");
    prop_audiobuftime = atoi(value);
	
    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.video.buffertime", value, "1000");
    prop_videobuftime = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.show_first_frame_nosync", value, "0");
    prop_show_first_frame_nosync = atoi(value);
	
    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.softfit", value, "1");
    prop_softfit = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.blackout.policy",value,"0");
    prop_blackout_policy = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.buffersize", value, "5000");
    buffersize = atoi(value);

    memset(value, 0, PROPERTY_VALUE_MAX);
    property_get("iptv.playerwatchdog.support", value, "0");
    prop_playerwatchdog_support = atoi(value);


    LOGI("CTsPlayer, prop_shouldshowlog: %d, prop_buffertime: %d, prop_dumpfile: %d, audio bufferlevel: %f,video bufferlevel: %f, prop_softfit: %d,player_watchdog_support:%d\n",
		prop_shouldshowlog, prop_buffertime, prop_dumpfile, prop_audiobuflevel, prop_videobuflevel, prop_softfit,prop_playerwatchdog_support);
    LOGI("iptv.audio.buffertime = %d, iptv.video.buffertime = %d\n", prop_audiobuftime, prop_videobuftime);
	
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
    if(amsysfs_get_sysfs_int("/sys/class/video/disable_video") == 1)
        amsysfs_set_sysfs_int("/sys/class/video/disable_video", 2);
    memset(a_aPara, 0, sizeof(AUDIO_PARA_T)*MAX_AUDIO_PARAM_SIZE);
    memset(sPara, 0, sizeof(SUBTITLE_PARA_T)*MAX_SUBTITLE_PARAM_SIZE);
    memset(&vPara, 0, sizeof(vPara));
    memset(&codec, 0, sizeof(codec));
    player_pid = -1;
    pcodec = &codec;
    codec_audio_basic_init();
    lp_lock_init(&mutex, NULL);
    //0:normal，1:full stretch，2:4-3，3:16-9
    int screen_mode = 0;
    property_get("ubootenv.var.screenmode",value,"full");
    if(!strcmp(value,"normal"))
         screen_mode = 0;
    else if(!strcmp(value,"full"))
         screen_mode = 1;
    else if(!strcmp(value,"4_3"))
         screen_mode = 2;
    else if(!strcmp(value,"16_9"))
         screen_mode = 3;
    else if(!strcmp(value,"4_3 letter box"))
        screen_mode = 7;
    else if(!strcmp(value,"16_9 letter box"))
        screen_mode = 11;
    else
        screen_mode = 1;


    amsysfs_set_sysfs_int("/sys/class/video/screen_mode", screen_mode);
    amsysfs_set_sysfs_int("/sys/class/tsync/enable", 1);

	//set overflow status when decode h264_4k use format h264 .
	amsysfs_set_sysfs_int("/sys/module/amvdec_h264/parameters/fatal_error_reset", 1);

    m_bIsPlay = false;
    m_bIsPause = false;
    pfunc_player_evt = NULL;
    m_nOsdBpp = 16;//SYS_get_osdbpp();
    m_nAudioBalance = 3;

    m_nVolume = 100;
    m_bFast = false;
    m_bSetEPGSize = false;
    m_bWrFirstPkg = true;
    m_StartPlayTimePoint = 0;
    m_PreviousOverflowTime = 0;
    m_isSoftFit = (prop_softfit == 1) ? true : false;
    m_isBlackoutPolicy = (prop_blackout_policy == 1) ? true : false;
    m_StopThread = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&mThread, &attr, threadCheckAbend, this);
    pthread_attr_destroy(&attr);

    m_nMode = M_LIVE;
    LunchIptv(m_isSoftFit);
    m_fp = NULL;
    sp<IBinder> binder =defaultServiceManager()->getService(String16("media.player"));
    sp<IMediaPlayerService> service = interface_cast<IMediaPlayerService>(binder);
    if(service.get() != NULL){
			  LOGI("CTsPlayer stopPlayerIfNeed \n");
			  service->stopPlayerIfNeed();
		  	LOGI("CTsPlayer stopPlayerIfNeed ==end\n");
    }
}

CTsPlayer::CTsPlayer(bool omx_player)
{
    mIsOmxPlayer = omx_player;
}
CTsPlayer::~CTsPlayer()
{
    if (mIsOmxPlayer)
        return;
    amsysfs_set_sysfs_int("/sys/module/amvdec_h264/parameters/fatal_error_reset", 0);
    m_StopThread = true;
    pthread_join(mThread, NULL);
    QuitIptv(m_isSoftFit, m_isBlackoutPolicy);
}

//取得播放模式,保留，暂不用
int CTsPlayer::GetPlayMode()
{
    LOGI("GetPlayMode\n");
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
    //const char *path_mode = "/sys/class/video/screen_mode";
    const char *path_axis = "/sys/class/video/axis";
    char bcmd[32];
    char buffer[15];
    int mode_w = 0, mode_h = 0;

    LOGI("CTsPlayer::SetVideoWindow: %d, %d, %d, %d\n", x, y, width, height);
    OUTPUT_MODE output_mode = get_display_mode();
    if(m_isSoftFit) {
        int x_b=0, y_b=0, w_b=0, h_b=0;
        int mode_x = 0, mode_y = 0, mode_width = 0, mode_height = 0;
        getPosition(output_mode, &mode_x, &mode_y, &mode_width, &mode_height);
        LOGI("SetVideoWindow mode_x: %d, mode_y: %d, mode_width: %d, mode_height: %d\n", 
                mode_x, mode_y, mode_width, mode_height);
        /*if(((mode_x == 0) && (mode_y == 0) &&(width < (mode_width -1)) && (height < (mode_height - 1))) 
                || (mode_x != 0) || (mode_y != 0)) {
            LOGW("SetVideoWindow this is not full window!\n");
            amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 1);
        } else {
            amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
        }*/
        x_b = x + mode_x;
        y_b = y + mode_y;
        w_b = width + x_b - 1;
        h_b = height + y_b - 1;
        if (h_b < 576 && h_b % 2)
            h_b +=1;
        /*if(m_nEPGWidth !=0 && m_nEPGHeight !=0) {
            amsysfs_set_sysfs_str(path_mode, "1");
        }*/
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

    /*if(m_nEPGWidth !=0 && m_nEPGHeight !=0) {
        amsysfs_set_sysfs_str(path_mode, "1");
    }*/

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
    if(!m_isBlackoutPolicy) {
        if(amsysfs_get_sysfs_int("/sys/class/video/disable_video") == 1)
            amsysfs_set_sysfs_int("/sys/class/video/disable_video",2);
        else
            LOGW("video is enable, no need to set disable_video again\n");
    }
    return 0;
}

int CTsPlayer::VideoHide(void)
{
    LOGI("VideoHide\n");
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole", "0 0 0 0 0 0");
    if(!m_isBlackoutPolicy)
        amsysfs_set_sysfs_int("/sys/class/video/disable_video",1);
    return 0;
}

void CTsPlayer::InitVideo(PVIDEO_PARA_T pVideoPara)
{
    vPara=*pVideoPara;
#ifdef TELECOM_VFORMAT_SUPPORT
    vPara.vFmt = changeVformat(vPara.vFmt);
#endif
    LOGI("InitVideo vPara->pid: %d, vPara->vFmt: %d\n", vPara.pid, vPara.vFmt);
}

void CTsPlayer::InitAudio(PAUDIO_PARA_T pAudioPara)
{
    PAUDIO_PARA_T pAP = pAudioPara;
    int count = 0;

    LOGI("InitAudio\n");
    memset(a_aPara,0,sizeof(AUDIO_PARA_T)*MAX_AUDIO_PARAM_SIZE);
    while((pAP->pid != 0)&&(count<MAX_AUDIO_PARAM_SIZE)) {
#ifdef TELECOM_VFORMAT_SUPPORT
        pAP->aFmt = changeAformat(pAP->aFmt);
#endif
        a_aPara[count]= *pAP;
        LOGI("InitAudio pAP->pid: %d, pAP->aFmt: %d, channel=%d, samplerate=%d\n", pAP->pid, pAP->aFmt, pAP->nChannels, pAP->nSampleRate);
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
    if(access("/system/etc/firmware/audiodsp_codec_ddp_dcv.bin",F_OK) && access("/system/lib/libstagefright_soft_dcvdec.so",F_OK)){
        filter_fmt |= (FILTER_AFMT_AC3|FILTER_AFMT_EAC3);
    }
    if(access("/system/etc/firmware/audiodsp_codec_dtshd.bin",F_OK) && access("/system/lib/libstagefright_soft_dtshd.so",F_OK)){
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

/* 
 * player_startsync_set
 *
 * reset start sync using prop media.amplayer.startsync.mode
 * 0 none start sync
 * 1 slow sync repeate mode
 * 2 drop pcm mode
 *
 * */

int player_startsync_set(int mode)
{
    const char * startsync_mode = "/sys/class/tsync/startsync_mode";
    const char * droppcm_prop = "sys.amplayer.drop_pcm"; // default enable
    const char * slowsync_path = "/sys/class/tsync/slowsync_enable";
    const char * slowsync_repeate_path = "/sys/class/video/slowsync_repeat_enable";
   
/*
    char value[PROPERTY_VALUE_MAX];
    int mode = get_sysfs_int(startsync_mode);
    int ret = property_get(startsync_prop, value, NULL);
    if (ret <= 0) {
        log_print("start sync mode prop not setting ,using default none \n");
    }
    else
        mode = atoi(value);
*/
    LOGI("start sync mode desp: 0 -none 1-slowsync repeate 2-droppcm \n"); 
    LOGI("start sync mode = %d \n",mode); 
    
    if(mode == 0) // none case
    {
        set_sysfs_int(slowsync_path,0); 
        //property_set(droppcm_prop, "0");
        set_sysfs_int(slowsync_repeate_path,0); 
    }
    
    if(mode == 1) // slow sync repeat mode
    {
        set_sysfs_int(slowsync_path,1); 
        //property_set(droppcm_prop, "0");
        set_sysfs_int(slowsync_repeate_path,1); 
    }
    
    if(mode == 2) // drop pcm mode
    {
        set_sysfs_int(slowsync_path,0); 
        //property_set(droppcm_prop, "1");
        set_sysfs_int(slowsync_repeate_path,0); 
    }

    return 0;
}
bool CTsPlayer::StartPlay(){
        int ret;
        ret = iStartPlay();
        codec_set_freerun_mode(pcodec, 0);
        return ret;
}

bool CTsPlayer::iStartPlay()
{
    int ret;
    int filter_afmt;
    char vaule[PROPERTY_VALUE_MAX] = {0};
    char vfm_map[4096] = {0};
    char *s = NULL;
    char *p = NULL; 
    int sleep_number = 0; 
    int video_buf_used = 0;
    int audio_buf_used = 0;
    int subtitle_buf_used = 0;
    int userdata_buf_used = 0;


    if (m_bIsPlay) {
        LOGE("[%s:%d]Already StartPlay: m_bIsPlay=%s\n", __FUNCTION__, __LINE__, (m_bIsPlay ? "true" : "false"));
        return true;
    }

    amsysfs_set_sysfs_int("/sys/class/tsync/enable", 1);
    set_sysfs_int("/sys/class/tsync/vpause_flag",0); // reset vpause flag -> 0
    set_sysfs_int("/sys/class/video/show_first_frame_nosync", prop_show_first_frame_nosync);	//keep last frame instead of show first frame
    set_sysfs_int("/sys/module/amvideo/parameters/horz_scaler_filter", 0xff);

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


    /*if(pcodec->audio_type == AFORMAT_AAC_LATM) {
        pcodec->audio_type = AFORMAT_EAC3;
    }*/

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
    } else {
        pcodec->has_audio = 0;
        pcodec->audio_pid = -1;
    }

    pcodec->video_pid = (int)vPara.pid;
    if(pcodec->video_type == VFORMAT_H264) {
        pcodec->am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
        lpbuffer_st.buffer = (unsigned char *)malloc(CTC_BUFFER_LOOP_NSIZE*buffersize);
        if(lpbuffer_st.buffer == NULL) {
            LOGI("malloc failed\n");
            lpbuffer_st.enlpflag = false;
            lpbuffer_st.rp = NULL;
            lpbuffer_st.wp = NULL;
        } else{
            LOGI("malloc success\n");
            lp_lock_init(&mutex_lp, NULL);
            lpbuffer_st.enlpflag = true;
            lpbuffer_st.rp = lpbuffer_st.buffer;
            lpbuffer_st.wp = lpbuffer_st.buffer;
            lpbuffer_st.bufferend = lpbuffer_st.buffer + CTC_BUFFER_LOOP_NSIZE*buffersize;
            lpbuffer_st.valid_can_read = 0;
            memset(lpbuffer_st.buffer, 0, CTC_BUFFER_LOOP_NSIZE*buffersize);
        }

		/*if(m_bFast){
			pcodec->am_sysinfo.param=(void *)am_sysinfo_param;
			pcodec->am_sysinfo.height = vPara.nVideoHeight;
			pcodec->am_sysinfo.width = vPara.nVideoWidth;
			
		}
		else{
        	pcodec->am_sysinfo.param = (void *)(0);
		}*/
		pcodec->am_sysinfo.param = (void *)(0);
    }else if(pcodec->video_type == VFORMAT_H264_4K2K) {
        pcodec->am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K;
    }

    if(pcodec->video_type == VFORMAT_MPEG4) {
        pcodec->am_sysinfo.format= VIDEO_DEC_FORMAT_MPEG4_5;
        LOGI("VIDEO_DEC_FORMAT_MPEG4_5\n");
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
    LOGI("set vFmt:%d, aFmt:%d, vpid:%d, apid:%d\n", vPara.vFmt, a_aPara[0].aFmt, vPara.pid, a_aPara[0].pid);
    LOGI("set has_video:%d, has_audio:%d, video_pid:%d, audio_pid:%d\n", pcodec->has_video, pcodec->has_audio, 
            pcodec->video_pid, pcodec->audio_pid);
    pcodec->noblock = 0;

    if(prop_dumpfile){
        if(m_fp == NULL) {
            char tmpfilename[1024] = "";
            static int tmpfileindex = 0;
            memset(vaule, 0, PROPERTY_VALUE_MAX);
            property_get("iptv.dumppath", vaule, "/storage/external_storage/sda1");
            sprintf(tmpfilename, "%s/Live%d.ts", vaule, tmpfileindex);
            tmpfileindex++;
            m_fp = fopen(tmpfilename, "wb+");
        }
    }

    // enable avsync only when av both exists, not including trick
    if(hasaudio && hasvideo)
        player_startsync_set(2);

    /*other setting*/
    lp_lock(&mutex);

    do{
		get_vfm_map_info(vfm_map);
		s = strstr(vfm_map,"(1)");
		p = strstr(vfm_map,"ionvideo}");
		video_buf_used=amsysfs_get_sysfs_int("/sys/class/amstream/videobufused");
		audio_buf_used=amsysfs_get_sysfs_int("/sys/class/amstream/audiobufused");
		subtitle_buf_used=amsysfs_get_sysfs_int("/sys/class/amstream/subtitlebufused");
		userdata_buf_used=amsysfs_get_sysfs_int("/sys/class/amstream/userdatabufused");
		LOGI("s=%s,p=%s\n",s,p);
		LOGI("buf used video:%d,audio:%d,subtitle:%d,userdata:%d\n",
			video_buf_used,audio_buf_used,subtitle_buf_used,userdata_buf_used);
		if((s == NULL)&&(p == NULL)&&(video_buf_used==0)&&(audio_buf_used==0)&&
	   		(subtitle_buf_used==0)&&(userdata_buf_used==0))
    		LOGI("not find valid,begin init\n");
		else{
			sleep_number++;
			usleep(50*1000);
    		LOGI("find find find,sleep_number=%d\n",sleep_number);
		}
    }while((s != NULL)||(p != NULL)||(video_buf_used != 0)||(audio_buf_used != 0) ||
	(subtitle_buf_used != 0)||(userdata_buf_used != 0));    
 
    ret = codec_init(pcodec);
    LOGI("StartPlay codec_init After: %d\n", ret);
    lp_unlock(&mutex);
    if(ret == 0) {
        if (m_nMode == M_LIVE) {
            if(m_isBlackoutPolicy)
                amsysfs_set_sysfs_int("/sys/class/video/blackout_policy",1);
            else
                amsysfs_set_sysfs_int("/sys/class/video/blackout_policy",0);
        }
        m_bIsPlay = true;
        m_bIsPause = false;
        keep_vdec_mem = 0;
        amsysfs_set_sysfs_int("/sys/class/vdec/keep_vdec_mem", 1);
        /*if(!m_bFast) {
            LOGI("StartPlay: codec_pause to buffer sometime");
            codec_pause(pcodec);
        }*/
    }
    //init tsync_syncthresh
    codec_set_cntl_syncthresh(pcodec, pcodec->has_audio);

    if(amsysfs_get_sysfs_int("/sys/class/video/slowsync_flag")!=1){
        amsysfs_set_sysfs_int("/sys/class/video/slowsync_flag",1);
    }
    //amsysfs_set_sysfs_str("/sys/class/graphics/fb0/video_hole","0 0 1280 720 0 8");
    m_bWrFirstPkg = true;
    m_bchangeH264to4k = false;
    writecount = 0;
    /*if(pcodec->has_video && pcodec->video_type == VFORMAT_HEVC) {
       amsysfs_set_sysfs_int("/sys/class/video/blackout_policy",1);
    }*/
    m_StartPlayTimePoint = av_gettime();
    LOGI("StartPlay: m_StartPlayTimePoint = %lld\n", m_StartPlayTimePoint);
    return !ret;
}

int CTsPlayer::WriteData(unsigned char* pBuffer, unsigned int nSize)
{
    int ret = -1;
    int temp_size = 0;
    static int retry_count = 0;
    buf_status audio_buf;
    buf_status video_buf;
    float audio_buf_level = 0.00f;
    float video_buf_level = 0.00f;

    if(!m_bIsPlay || m_bchangeH264to4k)
        return -1;

    //checkBuffstate();
    codec_get_abuf_state(pcodec, &audio_buf);
    codec_get_vbuf_state(pcodec, &video_buf);
    if(audio_buf.size != 0)
        audio_buf_level = (float)audio_buf.data_len / audio_buf.size;
    if(video_buf.size != 0)
        video_buf_level = (float)video_buf.data_len / video_buf.size;

    if((audio_buf_level >= MAX_WRITE_ALEVEL) || (video_buf_level >= MAX_WRITE_VLEVEL)) {
        LOGI("WriteData : audio_buf_level= %.5f, video_buf_level=%.5f, Don't writedate()\n", audio_buf_level, video_buf_level);
        return -1;
    } 

    lp_lock(&mutex);
	
    if ((pcodec->video_type == VFORMAT_H264) && lpbuffer_st.enlpflag) {
        
        lp_lock(&mutex_lp);
        if (lpbuffer_st.wp + nSize < lpbuffer_st.bufferend) {
            lpbuffer_st.wp = (unsigned char *)memcpy(lpbuffer_st.wp, pBuffer, nSize);
            lpbuffer_st.wp += nSize;
            lpbuffer_st.valid_can_read += nSize;
        } else {
            lpbuffer_st.wp = lpbuffer_st.buffer;
            lpbuffer_st.enlpflag = false;
            LOGI("Don't use lpbuffer enlpflag:%d\n", lpbuffer_st.enlpflag);
            free(lpbuffer_st.buffer);
            lpbuffer_st.buffer = NULL;
            lpbuffer_st.rp = NULL;
            lpbuffer_st.wp = NULL;
            lpbuffer_st.bufferend = NULL;
            lpbuffer_st.enlpflag = 0;
            lpbuffer_st.valid_can_read = 0;
        }
        lp_unlock(&mutex_lp);

        LOGI("lpbuffer_st.valid_can_read:%d\n", lpbuffer_st.valid_can_read);

        for(int retry_count=0; retry_count<10; retry_count++) {
            ret = codec_write(pcodec, pBuffer+temp_size, nSize-temp_size);
            if((ret < 0) || (ret > nSize)) {
                if(ret < 0 && errno == EAGAIN) {
                    usleep(10);
                    LOGI("WriteData: codec_write return EAGAIN!\n");
                    continue;
                }
            } else {
                temp_size += ret;
                LOGI("WriteData: codec_write h264 nSize is %d! temp_size=%d\n", nSize, temp_size);
                if(temp_size >= nSize) {
                    temp_size = nSize;
                    break;
                }
            }
        }
    } else {
        for(int retry_count=0; retry_count<10; retry_count++) {
            ret = codec_write(pcodec, pBuffer+temp_size, nSize-temp_size);
            if((ret < 0) || (ret > nSize)) {
                if(ret < 0 && (errno == EAGAIN)) {
                    usleep(10);
                    LOGI("WriteData: codec_write return EAGAIN!\n");
                    continue;
                } else {
                    LOGI("WriteData: codec_write return %d!\n", ret);
                    if(pcodec->handle > 0){
                        ret = codec_close(pcodec);
                        ret = codec_init(pcodec);
                        if(m_bFast) {
                            codec_set_mode(pcodec, TRICKMODE_I);
                        }
                        LOGI("WriteData : codec need close and reinit m_bFast=%d\n", m_bFast);
                    } else {
                        LOGI("WriteData: codec_write return error or stop by called!\n");
                        break;
                    }
                }
            } else {
                temp_size += ret;
                //LOGI("WriteData: codec_write  nSize is %d! temp_size=%d retry_count=%d\n", nSize, temp_size, retry_count);
                if(temp_size >= nSize) {
                    temp_size = nSize;
                    break;
                }
                // release 10ms to other thread, for example decoder and drop pcm
                usleep(10000);
            }
        }
    }
    lp_unlock(&mutex);

    if(ret > 0) {
        if((m_fp != NULL) && (temp_size > 0)) {
            fwrite(pBuffer, 1, temp_size, m_fp);
            LOGI("ret[%d] temp_size[%d] nSize[%d] %d!\n", ret, temp_size, nSize);
        }
        if(writecount >= MAX_WRITE_COUNT) {
            m_bWrFirstPkg = false;
            writecount = 0;
        }

        if(m_bWrFirstPkg == true) {
            writecount++;
        }
    } else {
        LOGW("WriteData: codec_write fail(%d)\n", ret);
        return -1;
    }
    return ret;
}

bool CTsPlayer::Pause()
{
    m_bIsPause = true;
    codec_pause(pcodec);
    return true;
}

bool CTsPlayer::Resume()
{
    m_bIsPause = false;
    codec_resume(pcodec);
    return true;
}

#define AML_VFM_MAP "/sys/class/vfm/map"
static int add_di()
{
    amsysfs_set_sysfs_str(AML_VFM_MAP, "rm default");
    amsysfs_set_sysfs_str(AML_VFM_MAP, "add default decoder ppmgr deinterlace amvideo");
    return 0;
}

static int remove_di()
{
    amsysfs_set_sysfs_str(AML_VFM_MAP, "rm default");
    amsysfs_set_sysfs_str(AML_VFM_MAP, "add default decoder ppmgr amvideo");
    return 0;
}

bool CTsPlayer::Fast()
{
    int ret;

    LOGI("Fast");
    ret = amsysfs_set_sysfs_int("/sys/class/video/blackout_policy", 0);
    if(ret)
        return false;
    keep_vdec_mem = 1;
    iStop();
    m_bFast = true;

    // remove di from vfm path
    //remove_di();

    //amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 1);
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_trick_mode", 2);
    ret = iStartPlay();
    if(!ret)
        return false;

    LOGI("Fast: codec_set_mode: %d\n", pcodec->handle);
    amsysfs_set_sysfs_int("/sys/class/tsync/enable", 0); 
    codec_set_freerun_mode(pcodec, 1);
    if(pcodec->video_type == VFORMAT_HEVC) 
        ret = codec_set_mode(pcodec, TRICKMODE_I_HEVC); 
    else 
        ret = codec_set_mode(pcodec, TRICKMODE_I);
    return !ret;
}

bool CTsPlayer::StopFast()
{
    int ret;

    LOGI("StopFast");
    m_bFast = false;
    
    ret=codec_set_freerun_mode(pcodec, 0);
    if(ret){
        LOGI("error stopfast set freerun_mode 0 fail\n");
    }
    
    ret = codec_set_mode(pcodec, TRICKMODE_NONE);
    //amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
    keep_vdec_mem = 1;
    iStop();
    //amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
    amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_trick_mode", 1);
    amsysfs_set_sysfs_int("/sys/class/tsync/enable", 1);
    ret = iStartPlay();
    if(!ret)
        return false;
    if(m_isBlackoutPolicy) {
        ret = amsysfs_set_sysfs_int("/sys/class/video/blackout_policy",1);
        if (ret)
            return false;
	}

    return true;
}
bool CTsPlayer::Stop(){
        int ret;

        codec_set_freerun_mode(pcodec, 0);
        ret =  iStop();

        return ret;
}

bool CTsPlayer::iStop()
{    
    int ret;
    
    LOGI("Stop keep_vdec_mem: %d\n", keep_vdec_mem);
    amsysfs_set_sysfs_int("/sys/class/vdec/keep_vdec_mem", keep_vdec_mem);
	amsysfs_set_sysfs_int("/sys/module/amvideo/parameters/horz_scaler_filter", 2);
    if(m_bIsPlay) {
        LOGI("m_bIsPlay is true");
        if(m_fp != NULL) {
            fclose(m_fp);
            m_fp = NULL;
        }

        //amsysfs_set_sysfs_int("/sys/module/di/parameters/bypass_all", 0);
        lp_lock(&mutex);
        m_bFast = false;
        m_bIsPlay = false;
        m_bIsPause = false;
        m_StartPlayTimePoint = 0;
        m_PreviousOverflowTime = 0;
        ret = codec_set_mode(pcodec, TRICKMODE_NONE);
        LOGI("codec_close start");
        ret = codec_close(pcodec);
        pcodec->handle = -1;
        LOGI("Stop  codec_close After:%d\n", ret);
        m_bWrFirstPkg = true;
        //add_di();
        lp_unlock(&mutex);
        if (lpbuffer_st.buffer != NULL){
            free(lpbuffer_st.buffer);
            lpbuffer_st.buffer = NULL;
            lpbuffer_st.rp = NULL;
            lpbuffer_st.wp = NULL;
            lpbuffer_st.bufferend = NULL;
            lpbuffer_st.enlpflag = 0;
            lpbuffer_st.valid_can_read = 0;
       }
    } else {
        LOGI("m_bIsPlay is false");
    }

    return true;
}

bool CTsPlayer::Seek()
{
    LOGI("Seek");
    if(m_isBlackoutPolicy)
        amsysfs_set_sysfs_int("/sys/class/video/blackout_policy",1);
    iStop();
    usleep(500*1000);
    iStartPlay();
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

//get current sound track
//return parameter: 1, Left Mono; 2, Right Mono; 3, Stereo; 4, Sound Mixing
int CTsPlayer::GetAudioBalance()
{
    return m_nAudioBalance;
}

//set sound track 
//input paramerter: nAudioBlance, 1, Left Mono; 2, Right Mono; 3, Stereo; 4, Sound Mixing
bool CTsPlayer::SetAudioBalance(int nAudioBalance)
{
    if((nAudioBalance < 1) && (nAudioBalance > 4))
        return false;
    m_nAudioBalance = nAudioBalance;
    if(nAudioBalance == 1) {
        LOGI("SetAudioBalance 1 Left Mono\n");
        //codec_left_mono(pcodec);
        codec_lr_mix_set(pcodec, 0);
         amsysfs_set_sysfs_str("/sys/class/amaudio/audio_channels_mask", "l");
    } else if(nAudioBalance == 2) {
        LOGI("SetAudioBalance 2 Right Mono\n");
        //codec_right_mono(pcodec);
        codec_lr_mix_set(pcodec, 0);
        amsysfs_set_sysfs_str("/sys/class/amaudio/audio_channels_mask", "r");
    } else if(nAudioBalance == 3) {
        LOGI("SetAudioBalance 3 Stereo\n");
        //codec_stereo(pcodec);
        codec_lr_mix_set(pcodec, 0);
        amsysfs_set_sysfs_str("/sys/class/amaudio/audio_channels_mask", "s");
    } else if(nAudioBalance == 4) {
        LOGI("SetAudioBalance 4 Sound Mixing\n");
        //codec_stereo(pcodec);
        codec_lr_mix_set(pcodec, 1);
        //amsysfs_set_sysfs_str("/sys/class/amaudio/audio_channels_mask", "c");
    }
    return true;
}

void CTsPlayer::GetVideoPixels(int& width, int& height)
{
    int x = 0, y = 0;
    OUTPUT_MODE output_mode = get_display_mode();
    getPosition(output_mode, &x, &y, &width, &height);
    LOGI("GetVideoPixels, x: %d, y: %d, width: %d, height: %d", x, y, width, height);
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
    int mode_x = 0;
    int mode_y = 0;
    int mode_width = 0;
    int mode_height = 0;
    vdec_status vdec;
    codec_get_vdec_state(pcodec,&vdec);
    width = vdec.width;
    height = vdec.height;

    LOGI("SetRatio width: %d, height: %d, nRatio: %d\n", width, height, nRatio);
    OUTPUT_MODE output_mode = get_display_mode();
    getPosition(output_mode, &mode_x, &mode_y, &mode_width, &mode_height);
    
    if((nRatio != 255) && (amsysfs_get_sysfs_int("/sys/class/video/disable_video") == 1))
        amsysfs_set_sysfs_int("/sys/class/video/disable_video", 2);
    if(nRatio == 1) {	 //Full screen
        new_x = mode_x;
        new_y = mode_y;
        new_width = mode_width;
        new_height = mode_height;
        sprintf(writedata, "%d %d %d %d", new_x, new_y, new_x +new_width - 1, new_y+new_height - 1);
        amsysfs_set_sysfs_str("/sys/class/video/axis", writedata);
        return true;
    } else if(nRatio == 2) {	//Fit by width
        new_width = mode_width;
        new_height = int(mode_width*height/width);
        new_x = mode_x;
        new_y = mode_y + int((mode_height-new_height)/2);
        LOGI("SetRatio new_x: %d, new_y: %d, new_width: %d, new_height: %d\n"
                , new_x, new_y, new_width, new_height);
        sprintf(writedata, "%d %d %d %d", new_x, new_y, new_x+new_width-1, new_y+new_height-1);
        amsysfs_set_sysfs_str("/sys/class/video/axis",writedata);
        return true;
    } else if(nRatio == 3) {	//Fit by height
        new_width = int(mode_height*width/height);
        new_height = mode_height;
        new_x = mode_x + int((mode_width - new_width)/2);
        new_y = mode_y;
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

    lp_lock(&mutex);
    codec_audio_automute(pcodec->adec_priv, 1);
    codec_close_audio(pcodec);
    pcodec->audio_pid = 0xffff;

    if(codec_set_audio_pid(pcodec)) {
        LOGE("set invalid audio pid failed\n");
        lp_unlock(&mutex);
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
        lp_unlock(&mutex);
        return;
    }

    if(codec_reset_audio(pcodec)) {
        LOGE("reset audio failed\n");
        lp_unlock(&mutex);
        return;
    }
    codec_resume_audio(pcodec, 1);
    codec_audio_automute(pcodec->adec_priv, 0);
    lp_unlock(&mutex);
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

int64_t CTsPlayer::GetCurrentPlayTime() 
{
    int64_t video_pts = 0;
    unsigned long audiopts = 0;
    unsigned long videopts = 0;
    unsigned long pcrscr = 0;
    unsigned long checkin_vpts = 0;
    
    unsigned int tmppts = 0;
    if (m_bIsPlay){
    	if ((pcodec->video_type == VFORMAT_HEVC) &&(m_bFast == true)) {
            tmppts = amsysfs_get_sysfs_int("/sys/module/amvdec_h265/parameters/h265_lookup_vpts");
            //LOGI("Fast: i only getvpts by h265_lookup_vpts :%d\n",tmppts);
    	}
    	else{
            tmppts = codec_get_vpts(pcodec);
    	}
    	
    }
    video_pts = tmppts;
    if(m_bFast && (pcodec->video_type != VFORMAT_HEVC)){
        sysfs_get_long("/sys/class/tsync/pts_audio",&audiopts);
        sysfs_get_long("/sys/class/tsync/pts_video",&videopts);
        sysfs_get_long("/sys/class/tsync/pts_pcrscr",&pcrscr);	
        LOGI("apts:0x%x,vpts=0x%x,pcrscr=0x%x\n",audiopts,videopts,pcrscr);
        sysfs_get_long("/sys/class/tsync/checkin_vpts",&checkin_vpts);
        LOGI("In Fast last checkin_vpts=0x%x\n",checkin_vpts);
        video_pts = (int64_t)checkin_vpts;
    }
    return video_pts;
}

void CTsPlayer::leaveChannel()
{
    LOGI("leaveChannel be call\n");
    iStop();
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

void CTsPlayer::checkBuffLevel()
{
	int audio_delay=0, video_delay=0;
    float audio_buf_level = 0.00f, video_buf_level = 0.00f;
    buf_status audio_buf;
    buf_status video_buf;
    
    if(m_bIsPlay) {
	#if 0
        codec_get_abuf_state(pcodec, &audio_buf);
        codec_get_vbuf_state(pcodec, &video_buf);
        if(audio_buf.size != 0)
            audio_buf_level = (float)audio_buf.data_len / audio_buf.size;
        if(video_buf.size != 0)
            video_buf_level = (float)video_buf.data_len / video_buf.size;
	#else
		codec_get_audio_cur_delay_ms(pcodec, &audio_delay);
		codec_get_video_cur_delay_ms(pcodec, &video_delay);
	#endif			
		
        if(!m_bFast && m_StartPlayTimePoint > 0 && (((av_gettime() - m_StartPlayTimePoint)/1000 >= prop_buffertime)
                || (audio_delay >= prop_audiobuftime || video_delay >= prop_videobuftime))) {
            LOGI("av_gettime()=%lld, m_StartPlayTimePoint=%lld, prop_buffertime=%d\n", av_gettime(), m_StartPlayTimePoint, prop_buffertime);
            LOGI("audio_delay=%d, prop_audiobuftime=%d, video_delay=%d, prop_videobuftime=%d\n", audio_delay, prop_audiobuftime, video_delay, prop_videobuftime);
            LOGI("WriteData: resume play now!\n");
            codec_resume(pcodec);
            m_StartPlayTimePoint = 0;
        }
    }
}

void CTsPlayer::checkBuffstate()
{
    char filter_mode[PROPERTY_VALUE_MAX] = {0};
    struct vdec_status video_buf;
    if(m_bIsPlay) {
        codec_get_vdec_state(pcodec, &video_buf);
        if (video_buf.status & DECODER_ERROR_MASK) {
            LOGI("decoder error vdec.status: %x\n", video_buf.status);
            int is_decoder_fatal_error = video_buf.status & DECODER_FATAL_ERROR_SIZE_OVERFLOW;
            if(is_decoder_fatal_error && (pcodec->video_type == VFORMAT_H264)) {
                //change format  h264--> h264 4K
                keep_vdec_mem = 1;
                iStop();
                usleep(500*1000);
                if(property_get("ro.platform.filter.modes",filter_mode,NULL) ==  0){
                    vPara.vFmt = VFORMAT_H264_4K2K;
                    iStartPlay();
                    LOGI("start play vh264_4k2k");
                }
            }
        }
    }	
}

void CTsPlayer::checkAbend() 
{
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

void CTsPlayer::checkVdecstate()
{
    float audio_buf_level = 0.00f, video_buf_level = 0.00f;
    buf_status audio_buf;
    buf_status video_buf;
    struct vdec_status video_status;
    int64_t  last_time, now_time;
    if(m_bIsPlay) {
        codec_get_vdec_state(pcodec, &video_status);
        if (video_status.status & DECODER_ERROR_MASK) {
            LOGI("decoder vdec.status: %x width : %d height: %d\n", video_status.status, video_status.width, video_status.height);

            if((video_status.status & DECODER_FATAL_ERROR_SIZE_OVERFLOW) &&
               (pcodec->video_type == VFORMAT_H264) &&
               (video_status.width > 1920 || video_status.height > 1080)) {
                now_time = av_gettime();
                m_bchangeH264to4k = true;
                //change format  h264--> h264 4K
                LOGI("change format  h264--> h264 4K: %x width : %d height: %d\n", 
                       video_status.status, video_status.width, video_status.height);

                LOGI("Begin start!!!! before rp:0x%x  wp:0x%x start:0x%x end:0x%x\n", 
                      lpbuffer_st.rp, lpbuffer_st.wp, lpbuffer_st.buffer, lpbuffer_st.bufferend);              
                codec_close(pcodec);
                vPara.vFmt = VFORMAT_H264_4K2K;
                pcodec->video_type = VFORMAT_H264_4K2K;
                pcodec->am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K; 
                codec_init(pcodec);				
                while(lpbuffer_st.valid_can_read > 0 && lpbuffer_st.enlpflag) {
                    int ret, temp_size = 0, can_write = READ_SIZE;
                    if(lpbuffer_st.valid_can_read < can_write) {
                        can_write = lpbuffer_st.valid_can_read;
                    }
                    for(int retry_count=0; retry_count<10; retry_count++) {
                        ret = codec_write(pcodec, lpbuffer_st.rp + temp_size, can_write - temp_size);
                        if((ret < 0) || (ret > can_write)) {
                            if(ret == EAGAIN) {
                                usleep(10);
                                LOGI("WriteData: codec_write return EAGAIN!\n");
                                continue;
                            }
                        } else {
                            temp_size += ret;
                            if(temp_size >= can_write) {
                                lp_lock(&mutex_lp);
                                lpbuffer_st.valid_can_read -= can_write;
                                lpbuffer_st.rp += can_write;
                                if(lpbuffer_st.rp >= lpbuffer_st.bufferend) {
                                    lpbuffer_st.enlpflag = false;
                                    lpbuffer_st.rp = lpbuffer_st.buffer;
                                }
                                lp_unlock(&mutex_lp);
                                break;
                            }
                        }
                    }
                    //LOGI("valid_can_read : %d\n", lpbuffer_st.valid_can_read);
                }
				
                m_bchangeH264to4k = false;
                lpbuffer_st.enlpflag = false;
                last_time = av_gettime();
                LOGI("consume time: %lld us %lld ms\n", (last_time - now_time), (last_time - now_time)/1000);
            }
        }

        if((video_status.status & DECODER_FATAL_ERROR_UNKNOW) &&
            (pcodec->video_type == VFORMAT_H264) ) {
            int app_reset_support  = amsysfs_get_sysfs_int("/sys/module/amvdec_h264/parameters/fatal_error_reset");
            if(app_reset_support){
                LOGI("fatal_error_reset=1,DECODER_FATAL_ERROR_UNKNOW happened force reset decoder\n ");
                amsysfs_set_sysfs_int("/sys/module/amvdec_h264/parameters/decoder_force_reset", 1);
            }
        }
        //monitor buffer staus ,overflow more than 2s reset player,if support 
        if (prop_playerwatchdog_support && !m_bIsPause){
            codec_get_abuf_state(pcodec, &audio_buf);
            codec_get_vbuf_state(pcodec, &video_buf);
            if (audio_buf.size != 0)
            audio_buf_level = (float)audio_buf.data_len / audio_buf.size;
            if (video_buf.size != 0)
            video_buf_level = (float)video_buf.data_len / video_buf.size;
            if ((audio_buf_level >= MAX_WRITE_ALEVEL) || (video_buf_level >= MAX_WRITE_VLEVEL)) {
                LOGI("checkVdecstate : audio_buf_level= %.5f, video_buf_level=%.5f\n", audio_buf_level, video_buf_level);
                if (m_PreviousOverflowTime == 0)
                    m_PreviousOverflowTime  = av_gettime();
                if ((av_gettime()-m_PreviousOverflowTime) >= 2000000){
                    LOGI("buffer  overflow more than 2s ,reset  player\n ");
                    iStop();
                    usleep(500*1000);
                    iStartPlay();
                }
            }else{
                m_PreviousOverflowTime = 0;
            }
        }
    }
}

void *CTsPlayer::threadCheckAbend(void *pthis) {
    LOGV("threadCheckAbend start pthis: %p\n", pthis);
    CTsPlayer *tsplayer = static_cast<CTsPlayer *>(pthis);
    do {
        usleep(50 * 1000);
        //sleep(2);
        //tsplayer->checkBuffLevel();
        if (tsplayer->m_bIsPlay)
            tsplayer->checkVdecstate();
        checkcount++;
        if(checkcount >= 40) {
            tsplayer->checkAbend();
            checkcount = 0;
        }
    }
    while(!m_StopThread);
    LOGV("threadCheckAbend end\n");
    return NULL;
}

int CTsPlayer::GetRealTimeFrameRate()
{
    int nRTfps = 0;

    nRTfps = amsysfs_get_sysfs_int("/sys/class/video/current_fps");
    if (nRTfps > 0)
        LOGI( "realtime fps:%d\n", nRTfps);

    return nRTfps;
}

int CTsPlayer::GetVideoFrameRate()
{
    int nVideoFrameRate = 0;
    struct vdec_status video_status;

    if (NULL != pcodec)
    {
        codec_get_vdec_state(pcodec, &video_status);
        nVideoFrameRate = video_status.fps;
        LOGI("video frame rate:%d\n", nVideoFrameRate);
    }

    return nVideoFrameRate;
}
int CTsPlayer::GetVideoDropNumber()
{
	int drop_number = 0;
	drop_number = amsysfs_get_sysfs_int("/sys/class/video/video_drop_number");
	LOGI("video drop number = %d\n",drop_number);

	return drop_number;
}
