#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>
#include <sys/types.h>

#include "wav_parser.h"

snd_pcm_t *playback_handle;
//int32_t buf[4096];
//int32_t buf2[4096];
//int32_t buf[8192];
//int32_t buf2[8192];
uint8_t bbuf[1058400];//[32768];
uint8_t bbuf2[1058400];  //[32768];
static volatile int exit_flag = 0;

typedef union frame_u {
    uint8_t raw[24];
    struct {
        uint32_t l11;
        uint16_t l12;
        uint32_t r11;
        uint16_t r12;
        uint32_t l21;
        uint16_t l22;
        uint32_t r21;
        uint16_t r22;
    } v1;
} frame_t;

static int interleaved = 0;
static int mmap = 0;

snd_pcm_sframes_t
playback_callback (void* bf, snd_pcm_sframes_t nframes)
{
    snd_pcm_sframes_t ret_res = 0;
    static int counter = 0;
    // printf ("playback callback called with %ld frames cnt[%d]\n", nframes,
    // ++counter);

    /* ... fill buf with data ... */

    if (interleaved) {
        if (mmap) {
            if ((ret_res = snd_pcm_mmap_writei(playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "mmap_writei failed [%ld](%s)\n", ret_res, snd_strerror(ret_res));
            }
        }
        else {
            if ((ret_res = snd_pcm_writei(playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "writei failed [%ld](%s)\n", ret_res, snd_strerror(ret_res));
            }
        }
    }
    else {
        if (mmap) {
            if ((ret_res = snd_pcm_mmap_writen(playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "mmap_writen failed [%ld](%s)\n", ret_res, snd_strerror(ret_res));
            }
        }
        else {
            if ((ret_res = snd_pcm_writen(playback_handle, bf, nframes)) < 0) {
                fprintf (stderr, "writen failed [%ld](%s)\n", ret_res, snd_strerror(ret_res));
            }
        }
    }

    //////////////////////////////////
#define NUMSTAT 10
    static int stat_arr[NUMSTAT] = {0};
    static int stat_idx = 0;
    int tmmsec = 0;
    long avgmsec = 0;
    static struct timespec tm1={0};
    static unsigned char has_history = 0;
    struct timespec tm2={0}, tmres={0};
    //////////////////////////////////
    //////////////////////////////////
    if (!has_history)
    {
        clock_gettime(CLOCK_MONOTONIC,&tm1);
        has_history = 1;
    }
    else
    {
        clock_gettime(CLOCK_MONOTONIC,&tm2);

        if (tm2.tv_nsec < tm1.tv_nsec)
        {
            tm2.tv_sec--;
            tm2.tv_nsec+=1000000000;
        }
        tmres.tv_nsec = tm2.tv_nsec - tm1.tv_nsec;
        tmres.tv_sec = tm2.tv_sec - tm1.tv_sec;
        tm1 = tm2;
        tmmsec = tmres.tv_sec * 1000 + tmres.tv_nsec / 1000000;

        //fprintf (stderr, "tm2[%01ld.%03ld] tm1[%01ld.%03ld] tmmsec[%03d]\n",
        //        tm2.tv_sec,tm2.tv_nsec,tm1.tv_sec,tm1.tv_nsec,tmmsec);

        stat_arr[stat_idx] = tmmsec;
        stat_idx++;
        if (stat_idx >= NUMSTAT)
        {
            avgmsec = (stat_arr[0]+stat_arr[1]+stat_arr[2]+stat_arr[3]+stat_arr[4]+
                                stat_arr[5]+stat_arr[6]+stat_arr[7]+stat_arr[8]+stat_arr[9])/NUMSTAT;

            fprintf(stderr, "stat[%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d][%3.ld]\n",
                    stat_arr[0],stat_arr[1],stat_arr[2],stat_arr[3],stat_arr[4],
                    stat_arr[5],stat_arr[6],stat_arr[7],stat_arr[8],stat_arr[9],
                    avgmsec);
            stat_idx = 0;
        }
    }
    //////////////////////////////////

    return ret_res;
}

size_t fmt_size(snd_pcm_format_t fmt)
{
    switch (fmt)
    {
        case SND_PCM_FORMAT_S32_LE:
            return 4;
            break;

        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_3LE:
            return 3;
            break;

        case SND_PCM_FORMAT_S16_LE:
        default:
            return 2;
            break;
    }
}

void signal_handler(int sig)
{
    exit_flag = 1;
    fprintf (stderr, "\n signal_handler[%d]. exiting ...\n", sig);
}

//typedef struct  WAV_HEADER
//{
//    /* RIFF Chunk Descriptor */
//    uint8_t         RIFF[4];        // RIFF Header Magic header
//    uint32_t        ChunkSize;      // RIFF Chunk Size
//    uint8_t         WAVE[4];        // WAVE Header
//    /* "fmt" sub-chunk */
//    uint8_t         fmt[4];         // FMT header
//    uint32_t        Subchunk1Size;  // Size of the fmt chunk
//    uint16_t        AudioFormat;    // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM
//    uint16_t        NumOfChan;      // Number of channels 1=Mono 2=Sterio
//    uint32_t        SamplesPerSec;  // Sampling Frequency in Hz
//    uint32_t        bytesPerSec;    // bytes per second
//    uint16_t        blockAlign;     // 2=16-bit mono, 4=16-bit stereo
//    uint16_t        bitsPerSample;  // Number of bits per sample
//    /* "data" sub-chunk */
//    uint8_t         Subchunk2ID[4]; // "data"  string
//    uint32_t        Subchunk2Size;  // Sampled data length
//} wav_hdr;
//
//void parse_wave_header(FILE* file)
//{
//#define BUFFSIZE    44
//    unsigned char buff[BUFFSIZE];
//    size_t bytes_read = fread(buff, 1, BUFFSIZE, file);
//    fprintf (stderr, "wave_header [%lu]\n", bytes_read);
//
//    if (bytes_read == BUFFSIZE)
//    {
//        wav_hdr* hdr = (wav_hdr*)buff;
//        fprintf (stderr, "RIFF [0x%x%x%x%x]<%c%c%c%c>\n",hdr->RIFF[0],hdr->RIFF[1],hdr->RIFF[2],hdr->RIFF[3], hdr->RIFF[0],hdr->RIFF[1],hdr->RIFF[2],hdr->RIFF[3]);
//        fprintf (stderr, "ChunkSize [0x%08x][%u]\n",hdr->ChunkSize,hdr->ChunkSize);
//        fprintf (stderr, "WAVE [0x%x%x%x%x]<%c%c%c%c>\n",hdr->WAVE[0],hdr->WAVE[1],hdr->WAVE[2],hdr->WAVE[3], hdr->WAVE[0],hdr->WAVE[1],hdr->WAVE[2],hdr->WAVE[3]);
//        fprintf (stderr, "fmt [0x%x%x%x%x]<%c%c%c%c>\n",hdr->fmt[0],hdr->fmt[1],hdr->fmt[2],hdr->fmt[3], hdr->fmt[0],hdr->fmt[1],hdr->fmt[2],hdr->fmt[3]);
//        fprintf (stderr, "Subchunk1Size [0x%08x][%u]\n",hdr->Subchunk1Size,hdr->Subchunk1Size);
//        fprintf (stderr, "AudioFormat [0x%04x][%u]\n",hdr->AudioFormat,hdr->AudioFormat);
//        fprintf (stderr, "NumOfChan [0x%04x][%u]\n",hdr->NumOfChan,hdr->NumOfChan);
//        fprintf (stderr, "SamplesPerSec [0x%08x][%u]\n",hdr->SamplesPerSec,hdr->SamplesPerSec);
//        fprintf (stderr, "bytesPerSec [0x%08x][%u]\n",hdr->bytesPerSec,hdr->bytesPerSec);
//        fprintf (stderr, "blockAlign [0x%04x][%u]\n",hdr->blockAlign,hdr->blockAlign);
//        fprintf (stderr, "bitsPerSample [0x%04x][%u]\n",hdr->bitsPerSample,hdr->bitsPerSample);
//        fprintf (stderr, "Subchunk2ID [0x%x%x%x%x]<%c%c%c%c>\n",hdr->Subchunk2ID[0],hdr->Subchunk2ID[1],hdr->Subchunk2ID[2],hdr->Subchunk2ID[3], hdr->Subchunk2ID[0],hdr->Subchunk2ID[1],hdr->Subchunk2ID[2],hdr->Subchunk2ID[3]);
//        fprintf (stderr, "Subchunk2Size [0x%08x][%u]\n",hdr->Subchunk2Size,hdr->Subchunk2Size);
//    }
//}



int
main (int argc, char *argv[])
{

    signal(SIGINT, signal_handler);

    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;
    snd_pcm_sframes_t frames_to_deliver;
    snd_pcm_info_t *pcm_info = NULL;
    int card_id = -1;
    unsigned int device_id = -1;
    unsigned int subdev_id = -1;
    unsigned int subdev_avail = -1;
    unsigned int subdev_count = -1;
    int nfds;
    int err;
    struct pollfd *pfds;
    int fd = -1;
    FILE* file = NULL;
  //unsigned int rate = 44100;
    unsigned int rate = 96000;
    unsigned int chan = 2;
    snd_pcm_format_t format =  SND_PCM_FORMAT_U8 ; //SND_PCM_FORMAT_S32 ;// SND_PCM_FORMAT_S16; // SND_PCM_FORMAT_S32
    snd_pcm_format_t devfmt = SND_PCM_FORMAT_UNKNOWN;
    snd_pcm_access_t devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    fmt_chunk_t format_data;
    int ready_to_break = 0;

    if ((argc >= 2) && (argv[2] != NULL))
    {
        file = fopen(argv[2], "rb");
        if (file == NULL) {
            fprintf (stderr, "cannot open file <%s> (%s)\n", argv[2], strerror (err));
        }
        else
        {
            parse_wave_header1(file, &format_data);
            //exit_flag = 1;

            rate = (unsigned int)format_data.SamplesPerSec;
            chan = (unsigned int)format_data.NumOfChan;

            switch (format_data.bitsPerSample)
            {
            case 32:
                format = SND_PCM_FORMAT_S32;
                break;
            case 24:
                format = SND_PCM_FORMAT_S24;
                format = SND_PCM_FORMAT_S24_3LE;
                break;
            case 16:
                format = SND_PCM_FORMAT_S16;
                break;
            case 8:
            default:
                format = SND_PCM_FORMAT_U8;
                break;
            }
        }
    }
    else
    {
        fprintf(stderr, "No valid args supplied\n");
        goto exit_point;
    }

    static int open_mode = 0 ;//| SND_PCM_NONBLOCK;
    static snd_output_t *logstd;

    if ((err = snd_output_stdio_attach(&logstd, stderr, 0)) < 0) {
        fprintf (stderr, "cannot attach stdio [%d] (%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_open (&playback_handle, argv[1], SND_PCM_STREAM_PLAYBACK, open_mode)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n", argv[1], snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_any (playback_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    //snd_pcm_dump_hw_setup(pcm, out)
    snd_pcm_hw_params_dump(hw_params, logstd);

    unsigned int supp_rate = -1;
    unsigned int supp_rate_min = -1;
    unsigned int supp_rate_max = -1;

    if ((err = snd_pcm_hw_params_get_rate(hw_params, &supp_rate, 0)) < 0) {
        fprintf (stderr, "cannot get rate [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate [%u]\n", supp_rate);
    }

    if ((err = snd_pcm_hw_params_get_rate_min(hw_params, &supp_rate_min, 0)) < 0) {
        fprintf (stderr, "cannot get rate min [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate min[%u]\n", supp_rate_min);
    }

    if ((err = snd_pcm_hw_params_get_rate_max(hw_params, &supp_rate_max, 0)) < 0) {
        fprintf (stderr, "cannot get rate max [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get rate max[%u]\n", supp_rate_max);
    }

    if ((err = snd_pcm_hw_params_test_rate(playback_handle, hw_params, rate, 0)) < 0) {
        fprintf (stderr, "rate [%d] test failed [%d](%s)\n", rate, err, snd_strerror (err));

        if (supp_rate > 0) {
            rate = supp_rate;
        }
    }
    else {
        fprintf (stderr, "Rate [%u] tested successfully.\n", rate);
    }

    unsigned int chan_min=0, chan_max=0;

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &chan)) < 0) {
        fprintf (stderr, "cannot get channels number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "get channels [%u]\n", chan);
    }

    if ((err = snd_pcm_hw_params_test_channels(playback_handle, hw_params, chan)) < 0) {
        fprintf (stderr, "channels [%d] test failed [%d](%s)\n", chan, err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Chan number [%u] tested successfully.\n", chan);
    }

    if ((err = snd_pcm_hw_params_get_channels_min(hw_params, &chan_min)) < 0) {
        fprintf (stderr, "cannot get chan_min number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get chan min[%u]\n", chan_min);
    }

    if ((err = snd_pcm_hw_params_get_channels_max(hw_params, &chan_max)) < 0) {
        fprintf (stderr, "cannot get chan_max number [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get chan max[%u]\n", chan_max);
    }

    if ((err = snd_pcm_hw_params_test_format(playback_handle, hw_params, format)) < 0) {
        fprintf (stderr, "Format [%d](%s) test failed [%d](%s)\n",
                 format, snd_pcm_format_name(format), err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Format [%d](%s)<%s> tested successfully.\n",
                 format, snd_pcm_format_name(format), snd_pcm_format_description(format));
    }

    devfmt = format;
    if ((err = snd_pcm_hw_params_get_format(hw_params, &devfmt)) < 0) {
        fprintf (stderr, "cannot get format [%d](%s)\n", err, snd_strerror (err));
        devfmt = format;
    }
    else {
        fprintf (stderr, "Get format[%d]\n", (int)devfmt);
    }

    if ((err = snd_pcm_hw_params_get_access(hw_params, &devaccss)) < 0) {
        fprintf (stderr, "cannot get access type [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get access type[%d]\n", (int)devaccss);
    }

    // SND_PCM_ACCESS_MMAP_INTERLEAVED
    if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) == 0) {
        devaccss = SND_PCM_ACCESS_RW_INTERLEAVED;
        interleaved = 1;
        mmap = 0;
        fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
    }
    else {
        if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED)) == 0) {
            devaccss = SND_PCM_ACCESS_RW_NONINTERLEAVED;
            interleaved = 0;
            mmap = 0;
            fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
        }
        else {
            if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED)) == 0) {
                devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
                interleaved = 1;
                mmap = 1;
                fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
            }
            else {
                if ((err = snd_pcm_hw_params_test_access (playback_handle, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED)) == 0) {
                    devaccss = SND_PCM_ACCESS_MMAP_NONINTERLEAVED;
                    interleaved = 0;
                    mmap = 1;
                    fprintf (stderr, "Access [%d] tested successfully.\n", devaccss);
                }
            }
        }
    }
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    ///
    if (((err = snd_pcm_info_malloc(&pcm_info)) < 0) || (pcm_info == NULL)) {
        fprintf (stderr, "cannot malloc pcm info [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Malloc pcm_info \n");
        /////////////////////////////
        if ((err = snd_pcm_info(playback_handle, pcm_info)) < 0) {
            fprintf (stderr, "cannot get pcm info [%d](%s)\n", err, snd_strerror (err));
        }
        else {
            card_id = snd_pcm_info_get_card(pcm_info);
            device_id = snd_pcm_info_get_device(pcm_info);
            subdev_id = snd_pcm_info_get_subdevice(pcm_info);
            subdev_count = snd_pcm_info_get_subdevices_count(pcm_info);
            subdev_avail = snd_pcm_info_get_subdevices_avail(pcm_info);

            fprintf (stderr, "Get pcm_info \n");
            fprintf (stderr, "Get pcm_info card[%d][%u][%u]<%s><%s><%s> c[%u] a[%u]\n",
                    card_id,
                    device_id,
                    subdev_id,
                    snd_pcm_info_get_id(pcm_info),
                    snd_pcm_info_get_name(pcm_info),
                    snd_pcm_info_get_subdevice_name(pcm_info),
                    subdev_count, subdev_avail);
        }

//        unsigned int snd_pcm_info_get_subdevices_count(const snd_pcm_info_t *obj);
//        unsigned int snd_pcm_info_get_subdevices_avail(const snd_pcm_info_t *obj);
//        unsigned int snd_pcm_info_get_device(const snd_pcm_info_t *obj);
//        unsigned int snd_pcm_info_get_subdevice(const snd_pcm_info_t *obj);
//        snd_pcm_stream_t snd_pcm_info_get_stream(const snd_pcm_info_t *obj);
//        int snd_pcm_info_get_card(const snd_pcm_info_t *obj);
//        const char *snd_pcm_info_get_id(const snd_pcm_info_t *obj);
//        const char *snd_pcm_info_get_name(const snd_pcm_info_t *obj);
//        const char *snd_pcm_info_get_subdevice_name(const snd_pcm_info_t *obj);

        snd_pcm_info_free(pcm_info);
        /////////////////////////////
    }
    ///
    {
        snd_pcm_chmap_t *chmap = NULL;
        if (NULL == (chmap = snd_pcm_get_chmap(playback_handle))) {
            fprintf (stderr, "cannot get channel map\n");
        }
        else {
            //////////////////////////
            fprintf (stderr, "Get chmap [%u][%u]\n", chmap->channels, chmap->pos[0]);
            //////////////////////////
        }
    }
    if (card_id >= 0)
    {
        snd_pcm_chmap_query_t **chmap = NULL;
        // chmap = snd_pcm_query_chmaps(playback_handle);
        chmap = snd_pcm_query_chmaps_from_hw(card_id, device_id, subdev_id, SND_PCM_STREAM_PLAYBACK);
        if ((NULL == chmap) || (*chmap == NULL)) {
            fprintf (stderr, "cannot run channel map query\n");
        }
        else {
            //////////////////////////
            fprintf (stderr, "Preparing to print.\n");
            fprintf (stderr, "Get type [%d]<%s> chmap [%u][%u]\n",
                    (*chmap)->type, snd_pcm_chmap_type_name((*chmap)->type),
                    (*chmap)->map.channels, (*chmap)->map.pos[0]);
            //////////////////////////
            snd_pcm_free_chmaps(chmap);
        }
    }
//    {
//        snd_pcm_chmap_query_t **chmap = NULL;
//        // chmap = snd_pcm_query_chmaps(playback_handle);
//        chmap = snd_pcm_query_chmaps_from_hw(1, 0, 0, SND_PCM_STREAM_PLAYBACK);
//        if ((NULL == chmap) || (*chmap == NULL)) {
//            fprintf (stderr, "cannot run channel map query\n");
//        }
//        else {
//            //////////////////////////
//            fprintf (stderr, "Preparing to print.\n");
//            fprintf (stderr, "Get type [%d]<%s> chmap [%u][%u]\n",
//                    (*chmap)->type, snd_pcm_chmap_type_name((*chmap)->type),
//                    (*chmap)->map.channels, (*chmap)->map.pos[0]);
//            //////////////////////////
//            snd_pcm_free_chmaps(chmap);
//        }
//    }
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    ///
    if ((err = snd_pcm_hw_params_get_sbits(hw_params)) < 0) {
        fprintf (stderr, "cannot get hw_sbits [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get sbits[%d]\n", err);
    }

    if ((err = snd_pcm_hw_params_set_access (playback_handle, hw_params, devaccss)) < 0) {
        fprintf (stderr, "cannot set access type [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_format (playback_handle, hw_params, /*format*/ devfmt)) < 0) {
        fprintf (stderr, "cannot set sample format [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near (playback_handle, hw_params, &rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params_set_channels (playback_handle, hw_params, chan)) < 0) {
        fprintf (stderr, "cannot set channel count [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    if ((err = snd_pcm_hw_params (playback_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    snd_pcm_hw_params_free (hw_params);

    /* tell ALSA to wake us up whenever 4096 or more frames
       of playback data can be delivered. Also, tell
       ALSA that we'll start the device ourselves.
    */

#define FRMLIMIT    4096 // 4096


    if ((err = snd_pcm_sw_params_malloc (&sw_params)) < 0) {
        fprintf (stderr, "cannot allocate software parameters structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_current (playback_handle, sw_params)) < 0) {
        fprintf (stderr, "cannot initialize software parameters structure [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_set_avail_min (playback_handle, sw_params, FRMLIMIT)) < 0) {
        fprintf (stderr, "cannot set minimum available count [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params_set_start_threshold (playback_handle, sw_params, 0U)) < 0) {
        fprintf (stderr, "cannot set start mode [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_sw_params (playback_handle, sw_params)) < 0) {
        fprintf (stderr, "cannot set software parameters [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    /* the interface will interrupt the kernel every FRMLIMIT frames, and ALSA
       will wake up this program very soon after that.
    */

    if ((err = snd_pcm_prepare (playback_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use [%d](%s)\n", err, snd_strerror (err));
        exit (1);
    }

    while (!exit_flag) {

        frame_t f1;
        f1.raw[0] = 0x11;
        f1.raw[1] = 0x22;
        f1.raw[2] = 0x33;
        f1.raw[3] = 0x44;
        f1.raw[4] = 0x55;
        f1.raw[5] = 0x66;
        f1.raw[6] = 0x77;
        f1.raw[7] = 0x88;
        f1.raw[8] = 0x99;
        f1.raw[9] = 0xaa;
        f1.raw[11] = 0xbb;
        f1.raw[12] = 0xcc;
        f1.raw[13] = 0xdd;
        f1.raw[14] = 0xee;
        f1.raw[15] = 0xff;
        f1.raw[16] = 0xa1;
        f1.raw[17] = 0xb1;
        f1.raw[18] = 0xc1;
        f1.raw[19] = 0xd1;
        f1.raw[20] = 0xe1;
        f1.raw[21] = 0xf1;
        f1.raw[22] = 0xa2;
        f1.raw[23] = 0xb2;

        /* wait till the interface is ready for data, or 1 second
           has elapsed.
        */

        if ((err = snd_pcm_wait (playback_handle, 1000)) < 0) {
                fprintf (stderr, "poll failed (%s)\n", strerror (errno));
                break;
        }

        /* find out how much space is available for playback data */

        if ((frames_to_deliver = snd_pcm_avail_update (playback_handle)) < 0) {
            if (frames_to_deliver == -EPIPE) {
                fprintf (stderr, "an xrun occured\n");
                break;
            }
            else {
                fprintf (stderr, "unknown ALSA avail update return value (%ld)\n", frames_to_deliver);
                break;
            }
        }
        else
        {
            fprintf (stderr, "frames_to_deliver obtained [%ld]\n", frames_to_deliver);
            frames_to_deliver = frames_to_deliver > FRMLIMIT ? FRMLIMIT : frames_to_deliver;//4096 // 2048
            size_t frames_read = fread((void*)bbuf2, format_data.blockAlign, (size_t)frames_to_deliver, file);
            //fprintf (stderr, "fread [%lu] frames of [%lu]\n", frames_read, frames_to_deliver);

            frames_to_deliver = frames_read;

            if (frames_read == 0)
            {
                fprintf (stderr, "fread [%lu] - EOL?\n", frames_read);
                if (!ready_to_break) {
                    snd_pcm_format_set_silence(devfmt, bbuf2, FRMLIMIT);
                    ready_to_break = 1;
                }
                else
                    break;
            }
        }

        memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));

        switch (format_data.bitsPerSample)
        {
        case 32:
        {
            if (fmt_size(devfmt)*8 == 32)
            {
                // Copy data
                // 32-to-32
                memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
            }
            else if (fmt_size(devfmt)*8 == 24)
            {
                // Convert data
                // 32-to-24
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=8)
                {
                    bbuf[j]   = bbuf2[i+1];  //
                    bbuf[j+1] = bbuf2[i+2];  // msb?
                    bbuf[j+2] = bbuf2[i+3];  // lsb?
                    bbuf[j+3] = bbuf2[i+5];  //
                    bbuf[j+4] = bbuf2[i+6];  // right
                    bbuf[j+5] = bbuf2[i+7];  //
                    j+=6;
                }
            }
            // Copy data
            // 32-to-32
//            for (int i=0; i < (size_t)frames_to_deliver; i++)
//            {
//                buf[i]   = buf2[i];
//            }
        } break;

        case 24:
        {
            memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
//            memcpy(buf2, f1.raw, sizeof(f1.raw));
//
//            int j=0;
//            int max_i = frames_to_deliver * format_data.blockAlign / 4;
//            // 24-to-32 ?
//            for (int i=0; i < (size_t)max_i; i+=3)
//            {
//                buf[j]   =  (buf2[i] << 8);
//                buf[j+1] = ((buf2[i] >> 16) & 0x0000FF00) | ((buf2[i+1] << 16) & 0xFFFF0000);
//                buf[j+2] = 0;//(buf2[i+1] << 16) | ((buf2[i+2] >> 16) & 0x0000FF00);
//                buf[j+3] = 0;//(buf2[i+2] ) & 0xFFFFFF00;  //; << 8// & 0xFFFFFF00;
//                j+=4;
//            }
//            for (int i=0; i < (size_t)max_i; i+=3)
//            {
//                buf[j]   =  buf2[i] & 0xFFFFFF00;
//                buf[j+1] = 0;//(buf2[i]   << 24) | ((buf2[i+1] >> 8) & 0x00FFFF00);
//                buf[j+2] = (buf2[i+1] << 16) | ((buf2[i+2] >> 16) & 0x0000FF00);
//                buf[j+3] = 0;// buf2[i+2] << 8;
//                j+=4;
//            }
        } break;

        case 16:
        {
            if (fmt_size(devfmt)*8 == 16)
            {
                // fprintf (stderr, "No conversion.\n");
                memcpy(bbuf, bbuf2, sizeof(frames_to_deliver * format_data.blockAlign));
            }
            else if (fmt_size(devfmt)*8 == 24) {
                // 16-to-24
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=4)
                {
                    bbuf[j]   = 0;           // ??bbuf2[i];
                    bbuf[j+1] = bbuf2[i];    // msb?
                    bbuf[j+2] = bbuf2[i+1];  // lsb?
                    bbuf[j+3] = 0;           // ??bbuf2[i+2];
                    bbuf[j+4] = bbuf2[i+2];  // right
                    bbuf[j+5] = bbuf2[i+3];  //
                    j+=6;
                }
            }
            else
            {
                // Convert data
                // 16-to-32
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=4)
                {
    //                buf[i*2]   = buf2[i] & 0xFFFF0000;//buf2[i];//(buf2[i] << 16);
    //                buf[i*2+1] = (buf2[i] << 16) & 0xFFFF0000;
                    bbuf[j]   = 0;//--//bbuf2[i];
                    bbuf[j+1] = 0;//--//bbuf2[i];
                    bbuf[j+2] = bbuf2[i];    // msb?
                    bbuf[j+3] = bbuf2[i+1];  //0;// lsb?
                    bbuf[j+4] = 0;//--//bbuf2[i+2];
                    bbuf[j+5] = 0;//--//bbuf2[i+2];
                    bbuf[j+6] = bbuf2[i+2];    // right
                    bbuf[j+7] = bbuf2[i+3]; //0; //
                    j+=8;
                }
            }
        } break;

        default:
            fprintf (stderr, "Unsupported bit map!\n");
            break;
        }

        static bool print_once = false;

        if (print_once)
        {
//            fprintf (stderr, "[%08x][%04x]\n", f1.v1.l11, f1.v1.l12);
//            fprintf (stderr, "[%08x][%04x]\n", f1.v1.r11, f1.v1.r12);
//            fprintf (stderr, "[%08x][%04x]\n", f1.v1.l21, f1.v1.l22);
//            fprintf (stderr, "[%08x][%04x]\n", f1.v1.r21, f1.v1.r22);

            fprintf (stderr, "BB:uf2 | BB:uf\n");
            for (int i=0; i < 100; i+=4)
            {
                //fprintf (stderr, "[%08x][%08x]\n", buf2[i], buf2[i+1]);
                fprintf (stderr, "[%02x][%02x]:[%02x][%02x] | ",
                        bbuf2[i], bbuf2[i+1], bbuf2[i+2], bbuf2[i+3]);
                fprintf (stderr, "[%02x][%02x][%02x][%02x]:[%02x][%02x][%02x][%02x]\n",
                                                        bbuf[i*2], bbuf[i*2+1], bbuf[i*2+2], bbuf[i*2+3],
                                                        bbuf[i*2+4], bbuf[i*2+5], bbuf[i*2+6], bbuf[i*2+7]);
            }
            fprintf (stderr, "-----:\n");

//            fprintf (stderr, "BBuf:\n");
//            for (int i=0; i < 100; i+=2)
//            {
//                //fprintf (stderr, "[%08x][%08x]\n", buf[i], buf[i+1]);
//                fprintf (stderr, "[%01x][%01x][%01x][%01x]\n",
//                                        bbuf2[i], bbuf2[i+1], bbuf2[i+2], bbuf2[i+3]);
//            }
//            fprintf (stderr, "-----:\n");

            print_once = false;
        }

        /* deliver the data */

        snd_pcm_sframes_t consumed_frames = 0;
        //
        if ((consumed_frames = playback_callback(bbuf, frames_to_deliver)) != frames_to_deliver)
        {
            fprintf (stderr, "playback callback failed : [%ld]vs[%ld]\n", consumed_frames, frames_to_deliver);
            break;
        }
    } // while

    if (!exit_flag) {
        snd_pcm_nonblock(playback_handle, 0);
        if ((err = snd_pcm_drain (playback_handle)) < 0) {
            fprintf (stderr, "cannot drain pcm. [%d](%s)\n", err, snd_strerror (err));
        }
        else {
            fprintf (stderr, "pcm drawn.\n");
        }
        snd_pcm_nonblock(playback_handle, 0);
    }

    snd_pcm_close (playback_handle);
    snd_output_close(logstd);
exit_point:
    if (file)
        fclose(file);
    exit (0);
}
