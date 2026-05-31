#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

#include "wav_parser.h"

uint8_t bbuf[32768];
uint8_t bbuf2[32768];
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

typedef struct alsa_ctx_s {
    snd_pcm_t* pcm_hndl;
    snd_pcm_format_t devfmt;
    snd_output_t* logstd;
    unsigned int needed_rate;
    unsigned int needed_chan;
    snd_pcm_format_t format;
    snd_pcm_uframes_t frm_limit;
    int interleaved;
    int mmap;
} alsa_ctx_t;

int playback_callback(void* bf, alsa_ctx_t* actx, snd_pcm_sframes_t nframes)
{
    int err;
    // static int counter = 0;
    // printf ("playback callback called with %ld frames cnt[%d]\n", nframes, ++counter);

    /* ... fill buf with data ... */

    if (actx->interleaved) {
        if (actx->mmap) {
            if ((err = snd_pcm_mmap_writei(actx->pcm_hndl, bf, nframes)) < 0) {
                fprintf (stderr, "mmap_writei failed [%d](%s)\n", err, snd_strerror (err));
            }
        } else {
            if ((err = snd_pcm_writei(actx->pcm_hndl, bf, nframes)) < 0) {
                fprintf (stderr, "writei failed [%d](%s)\n", err, snd_strerror (err));
            }
        }
    }

    //////////////////////////////////
#define NUMSTAT 10
    static int stat_arr[NUMSTAT] = {0};
    static int stat_idx = 0;
    int tmmsec = 0;
    long avgmsec = 0;
    static struct timespec tm1 = {0};
    static unsigned char has_history = 0;
    struct timespec tm2 = {0}, tmres = {0};
    //////////////////////////////////
    //////////////////////////////////
    if (!has_history) {
        clock_gettime(CLOCK_MONOTONIC, &tm1);
        has_history = 1;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &tm2);

        if (tm2.tv_nsec < tm1.tv_nsec) {
            tm2.tv_sec--;
            tm2.tv_nsec += 1000000000;
        }
        tmres.tv_nsec = tm2.tv_nsec - tm1.tv_nsec;
        tmres.tv_sec = tm2.tv_sec - tm1.tv_sec;
        tm1 = tm2;
        tmmsec = tmres.tv_sec * 1000 + tmres.tv_nsec / 1000000;

        // fprintf (stderr, "tm2[%01ld.%03ld] tm1[%01ld.%03ld] tmmsec[%03d]\n",
        //         tm2.tv_sec,tm2.tv_nsec,tm1.tv_sec,tm1.tv_nsec,tmmsec);

        stat_arr[stat_idx] = tmmsec;
        stat_idx++;
        if (stat_idx >= NUMSTAT) {
            avgmsec = (stat_arr[0] + stat_arr[1] + stat_arr[2] + stat_arr[3] + stat_arr[4]
                       + stat_arr[5] + stat_arr[6] + stat_arr[7] + stat_arr[8] + stat_arr[9])
                / NUMSTAT;

            fprintf(
                stderr,
                "stat[%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d,%02d][%3.ld]\n",
                stat_arr[0],
                stat_arr[1],
                stat_arr[2],
                stat_arr[3],
                stat_arr[4],
                stat_arr[5],
                stat_arr[6],
                stat_arr[7],
                stat_arr[8],
                stat_arr[9],
                avgmsec);
            stat_idx = 0;
        }
    }
    //////////////////////////////////

    return err;
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

bool init_hwdev(const char* dev_name, alsa_ctx_t* actx)
{
    // snd_pcm_t* pcm_hndl, snd_pcm_format_t* devfmt

    int open_mode = 0; //| SND_PCM_NONBLOCK;
    int err = 0;
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_sw_params_t* sw_params;
    snd_pcm_access_t devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    // unsigned int rate = 44100;

    if ((err = snd_output_stdio_attach(&actx->logstd, stderr, 0)) < 0) {
        fprintf(stderr, "cannot attach stdio [%d] (%s)\n", err, snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_open(&actx->pcm_hndl, dev_name, SND_PCM_STREAM_PLAYBACK, open_mode)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n", dev_name, snd_strerror(err));
        return false;
    }

    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_any(actx->pcm_hndl, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    snd_pcm_dump_hw_setup(actx->pcm_hndl, actx->logstd);
    snd_pcm_hw_params_dump(hw_params, actx->logstd);

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

    if ((err = snd_pcm_hw_params_test_rate(actx->pcm_hndl, hw_params, actx->needed_rate, 0)) < 0) {
        fprintf(
            stderr, "rate [%d] test failed [%d](%s)\n", actx->needed_rate, err, snd_strerror(err));

        if (supp_rate > 0) {
            actx->needed_rate = supp_rate;
        }
    } else {
        fprintf(stderr, "Rate [%u] tested successfully.\n", actx->needed_rate);
    }

    unsigned int chan = 0, chan_min = 0, chan_max = 0;

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &chan)) < 0) {
        fprintf (stderr, "cannot get channels number [%d](%s)\n", err, snd_strerror (err));
    } else {
        fprintf(stderr, "get channels [%u]\n", chan);
    }

    if ((err = snd_pcm_hw_params_test_channels(actx->pcm_hndl, hw_params, actx->needed_chan)) < 0) {
        fprintf(
            stderr,
            "channels [%d] test failed [%d](%s)\n",
            actx->needed_chan,
            err,
            snd_strerror(err));
    } else {
        fprintf(stderr, "Chan number [%u] tested successfully.\n", actx->needed_chan);
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

    // ToDo:

    if ((err = snd_pcm_hw_params_get_format(hw_params, &actx->devfmt)) < 0) {
        fprintf (stderr, "Cannot get format [%d](%s)\n", err, snd_strerror (err));

    } else {
        fprintf(
            stderr, "Got format[%d]<%s>\n", (int)actx->devfmt, snd_pcm_format_name(actx->devfmt));
    }

    snd_pcm_format_mask_t* devfmt_mask = NULL;
    snd_pcm_format_mask_malloc(&devfmt_mask);
    
    snd_pcm_hw_params_get_format_mask(hw_params, devfmt_mask);

    if ((err = snd_pcm_format_mask_test(devfmt_mask, SND_PCM_FORMAT_S32) > 0)) {
        actx->devfmt = SND_PCM_FORMAT_S32;
        fprintf (stderr, "SND_PCM_FORMAT_S32=[%d]\n", (int)err);
    }
    // else
    if ((err = snd_pcm_format_mask_test(devfmt_mask, SND_PCM_FORMAT_S24) > 0)) {
        actx->devfmt = SND_PCM_FORMAT_S24;
        fprintf (stderr, "SND_PCM_FORMAT_S24=[%d]\n", (int)err);
    }
    // else
    if ((err = snd_pcm_format_mask_test(devfmt_mask, SND_PCM_FORMAT_S24_3LE) > 0)) {
        actx->devfmt = SND_PCM_FORMAT_S24_3LE;
        fprintf (stderr, "SND_PCM_FORMAT_S24_3LE=[%d]\n", (int)err);
    } 
    // else 
    if ((err = snd_pcm_format_mask_test(devfmt_mask, SND_PCM_FORMAT_S16) > 0)) {
        actx->devfmt = SND_PCM_FORMAT_S16;
        fprintf (stderr, "SND_PCM_FORMAT_S16=[%d]\n", (int)err);
    } 
    //else 
    if ((err = snd_pcm_format_mask_test(devfmt_mask, SND_PCM_FORMAT_U8) > 0)) {
        actx->devfmt = SND_PCM_FORMAT_U8;
        fprintf (stderr, "SND_PCM_FORMAT_U8=[%d]\n", (int)err);
    }
    snd_pcm_format_mask_free(devfmt_mask);

    if ((err = snd_pcm_hw_params_test_format(actx->pcm_hndl, hw_params, actx->format)) < 0) {
        fprintf(
            stderr,
            "Format [%d]<%s> test failed [%d](%s)\n",
            actx->format,
            snd_pcm_format_name(actx->format),
            err,
            snd_strerror(err));
    } else {
        fprintf(
            stderr,
            "Format [%d]<%s> tested successfully.\n",
            actx->format,
            snd_pcm_format_name(actx->format));
        actx->devfmt = actx->format;
    }

    if ((err = snd_pcm_hw_params_get_access(hw_params, &devaccss)) < 0) {
        fprintf (stderr, "cannot get access type [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get access type[%d]\n", (int)devaccss);
    }

    // SND_PCM_ACCESS_MMAP_INTERLEAVED
    if ((err
         = snd_pcm_hw_params_test_access(actx->pcm_hndl, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED))
        == 0) {
        devaccss = SND_PCM_ACCESS_RW_INTERLEAVED;
        actx->interleaved = 1;
        actx->mmap = 0;
        fprintf(
            stderr,
            "Access [%d]<%s> tested successfully.\n",
            devaccss,
            snd_pcm_access_name(devaccss));
    } else {
        if ((err = snd_pcm_hw_params_test_access(
                 actx->pcm_hndl, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED))
            == 0) {
            devaccss = SND_PCM_ACCESS_MMAP_INTERLEAVED;
            actx->interleaved = 0;
            actx->mmap = 1;
            fprintf(
                stderr,
                "Access [%d]<%s> tested successfully.\n",
                devaccss,
                snd_pcm_access_name(devaccss));
        } else {
            if ((err = snd_pcm_hw_params_test_access(
                     actx->pcm_hndl, hw_params, SND_PCM_ACCESS_RW_NONINTERLEAVED))
                == 0) {
                devaccss = SND_PCM_ACCESS_RW_NONINTERLEAVED;
                actx->interleaved = 1;
                actx->mmap = 0;
                fprintf(
                    stderr,
                    "Access [%d]<%s> tested successfully.\n",
                    devaccss,
                    snd_pcm_access_name(devaccss));
            } else {
                if ((err = snd_pcm_hw_params_test_access(
                         actx->pcm_hndl, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED))
                    == 0) {
                    devaccss = SND_PCM_ACCESS_MMAP_NONINTERLEAVED;
                    actx->interleaved = 0;
                    actx->mmap = 1;
                    fprintf(
                        stderr,
                        "Access [%d]<%s> tested successfully.\n",
                        devaccss,
                        snd_pcm_access_name(devaccss));
                }
            }
        }
    }
    ///////////

    if ((err = snd_pcm_hw_params_get_sbits(hw_params)) < 0) {
        fprintf (stderr, "cannot get sbits [%d](%s)\n", err, snd_strerror (err));
    }
    else {
        fprintf (stderr, "Get sbits[%d]\n", err);
    }

    if ((err = snd_pcm_hw_params_set_access(actx->pcm_hndl, hw_params, devaccss)) < 0) {
        fprintf (stderr, "cannot set access type [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_format(actx->pcm_hndl, hw_params, actx->devfmt)) < 0) {
        fprintf (stderr, "cannot set sample format [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_rate_near(actx->pcm_hndl, hw_params, &actx->needed_rate, 0))
        < 0) {
        fprintf (stderr, "cannot set sample rate [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params_set_channels(actx->pcm_hndl, hw_params, actx->needed_chan)) < 0) {
        fprintf (stderr, "cannot set channel count [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    if ((err = snd_pcm_hw_params(actx->pcm_hndl, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_sw_params_malloc (&sw_params)) < 0) {
        fprintf (stderr, "cannot allocate software parameters structure [%d](%s)\n", err, snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_sw_params_current(actx->pcm_hndl, sw_params)) < 0) {
        fprintf (stderr, "cannot initialize software parameters structure [%d](%s)\n", err, snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_sw_params_set_avail_min(actx->pcm_hndl, sw_params, actx->frm_limit)) < 0) {
        fprintf (stderr, "cannot set minimum available count [%d](%s)\n", err, snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_sw_params_set_start_threshold(actx->pcm_hndl, sw_params, 0U)) < 0) {
        fprintf (stderr, "cannot set start mode [%d](%s)\n", err, snd_strerror (err));
        return false;
    }
    if ((err = snd_pcm_sw_params(actx->pcm_hndl, sw_params)) < 0) {
        fprintf (stderr, "cannot set software parameters [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    /* the interface will interrupt the kernel every 4096 frames, and ALSA
       will wake up this program very soon after that.
    */

    if ((err = snd_pcm_prepare(actx->pcm_hndl)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use [%d](%s)\n", err, snd_strerror (err));
        return false;
    }

    return true;
}

bool uninit_hwdev(alsa_ctx_t* actx)
{
    if (!actx)
        return false;

    int err = 0;
    bool res = true;

    if (actx->pcm_hndl)
        if ((err = snd_pcm_close(actx->pcm_hndl)) < 0)
            res = false;
    if (actx->logstd)
        if ((err = snd_output_close(actx->logstd)) < 0)
            res = false;

    return res;
}

int main(int argc, char* argv[])
{
    /* tell ALSA to wake us up whenever 4096 or more frames
       of playback data can be delivered. Also, tell
       ALSA that we'll start the device ourselves.
    */

#define FRMLIMIT 4096 // 4096

    signal(SIGINT, signal_handler);

    snd_pcm_sframes_t frames_to_deliver;
    int nfds;
    int err;
    struct pollfd* pfds;
    int fd = -1;
    FILE* file = NULL;

    alsa_ctx_t alsa_dev;
    alsa_dev.pcm_hndl = NULL;
    alsa_dev.logstd = NULL;
    alsa_dev.devfmt = SND_PCM_FORMAT_UNKNOWN;
    alsa_dev.needed_rate = 44100;
    alsa_dev.needed_chan = 2;
    alsa_dev.frm_limit = FRMLIMIT;
    alsa_dev.interleaved = 0;
    alsa_dev.mmap = 0;

    fmt_chunk_t format_data;
    int ready_to_break = 0;

    if ((argc >= 2) && (argv[2] != NULL)) {
        file = fopen(argv[2], "rb");
        if (file == NULL) {
            fprintf(stderr, "cannot open file <%s> (%s)\n", argv[2], strerror(err));
        } else {
            snd_pcm_format_t format = SND_PCM_FORMAT_U8;

            parse_wave_header1(file, &format_data);

            alsa_dev.needed_rate = (unsigned int)format_data.SamplesPerSec;
            alsa_dev.needed_chan = (unsigned int)format_data.NumOfChan;

            switch (format_data.bitsPerSample) {
            case 32:
                format = SND_PCM_FORMAT_S32;
                break;
            case 24:
                format = SND_PCM_FORMAT_S24;
                // format = SND_PCM_FORMAT_S24_3LE;
                break;
            case 16:
                format = SND_PCM_FORMAT_S16;
                break;
            case 8:
            default:
                format = SND_PCM_FORMAT_U8;
                break;
            }

            fprintf(stderr, "File-format [%d]<%s> \n", (int)format, snd_pcm_format_name(format));

            alsa_dev.format = format;
        }
    } else {
        fprintf(stderr, "arguments are needed : <output_device> <file> \n");
        goto exit_point;
    }

    ///////////////////////////
    if (!init_hwdev(argv[1], &alsa_dev)) {
        fprintf(stderr, "init_hwdev failed. \n");
        goto exit_point;
    }
    ///////////////////////////

    while (!exit_flag) {

        /*frame_t f1;
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
        f1.raw[23] = 0xb2;*/

        /* wait till the interface is ready for data, or 1 second
           has elapsed.
        */

        if ((err = snd_pcm_wait(alsa_dev.pcm_hndl, 1000)) < 0) {
            fprintf(stderr, "poll failed (%s)\n", strerror(errno));
            break;
        }

        /* find out how much space is available for playback data */

        if ((frames_to_deliver = snd_pcm_avail_update(alsa_dev.pcm_hndl)) < 0) {
            if (frames_to_deliver == -EPIPE) {
                fprintf (stderr, "an xrun occured\n");
                break;
            }
            else {
                fprintf (stderr, "unknown ALSA avail update return value (%ld)\n", frames_to_deliver);
                break;
            }
        } else {
            fprintf (stderr, "frames_to_deliver obtained [%ld]\n", frames_to_deliver);
            frames_to_deliver = frames_to_deliver > FRMLIMIT ? FRMLIMIT : frames_to_deliver;//4096 // 2048

            size_t frames_read = fread((void*)bbuf2, format_data.blockAlign, (size_t)frames_to_deliver, file);

            frames_to_deliver = frames_read;

            if (frames_read == 0)
            {
                fprintf (stderr, "fread [%lu] - EOL?\n", frames_read);
                if (!ready_to_break) {
                    snd_pcm_format_set_silence(alsa_dev.devfmt, bbuf2, FRMLIMIT);
                    ready_to_break = 1;
                }
                else
                    break;
            }
        }

        // memcpy(bbuf, bbuf2, (frames_to_deliver * format_data.blockAlign));

        switch (format_data.bitsPerSample)
        {
        case 32:
            memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
            // Copy data
            // 32-to-32
//            for (int i=0; i < (size_t)frames_to_deliver; i++)
//            {
//                buf[i]   = buf2[i];
//            }
            break;

        case 24:
        {
            //memcpy(bbuf,bbuf2,(frames_to_deliver * format_data.blockAlign));
//            memcpy(buf2, f1.raw, sizeof(f1.raw));
//
            int j=0;
            int max_i = frames_to_deliver * format_data.blockAlign / 4;
            // 24-to-32 :
            for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=6)
            {
                bbuf[j]   =  0;
                bbuf[j+1] = bbuf2[i];
                bbuf[j+2] = bbuf2[i+1];
                bbuf[j+3] = bbuf2[i+2];

                bbuf[j+4]   =  0;
                bbuf[j+5] = bbuf2[i+3];
                bbuf[j+6] = bbuf2[i+4];
                bbuf[j+7] = bbuf2[i+5];
                j+=8;
            }
        } break;

        case 16:
        {
            if (fmt_size(alsa_dev.devfmt) * 8 == 16) {
                fprintf(
                    stderr,
                    "No conversion.16.[%d]<%s>\n",
                    alsa_dev.devfmt,
                    snd_pcm_format_name(alsa_dev.devfmt));

                memcpy(bbuf, bbuf2, frames_to_deliver * format_data.blockAlign);
            } else {
                // Convert data
                // 16-to-32
                int j=0;
                for (int i=0; i < (size_t)(frames_to_deliver * format_data.blockAlign); i+=4)
                {
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

        static bool have_to_print = true;

        if (have_to_print) {
            size_t fwidth = fmt_size(alsa_dev.format);
            size_t dwidth = fmt_size(alsa_dev.devfmt);
            // fprintf (stderr, "[%08x][%04x]\n", f1.v1.l11, f1.v1.l12);
            // fprintf (stderr, "[%08x][%04x]\n", f1.v1.r11, f1.v1.r12);
            // fprintf (stderr, "[%08x][%04x]\n", f1.v1.l21, f1.v1.l22);
            // fprintf (stderr, "[%08x][%04x]\n", f1.v1.r21, f1.v1.r22);

            fprintf(
                stderr,
                "BB:uf2[%ld] | BB:uf[%ld]\n",
                (long unsigned int)fwidth,
                (long unsigned int)dwidth);

            for (int i = 0; i < 100; i += 4) {
                // fprintf (stderr, "[%08x][%08x]\n", buf2[i], buf2[i+1]);
                fprintf(
                    stderr,
                    "[%02x][%02x]:[%02x][%02x] | ",
                    bbuf2[i],
                    bbuf2[i + 1],
                    bbuf2[i + 2],
                    bbuf2[i + 3]);
                fprintf(
                    stderr,
                    "[%02x][%02x][%02x][%02x]:[%02x][%02x][%02x][%02x]\n",
                    bbuf[i * 2],
                    bbuf[i * 2 + 1],
                    bbuf[i * 2 + 2],
                    bbuf[i * 2 + 3],
                    bbuf[i * 2 + 4],
                    bbuf[i * 2 + 5],
                    bbuf[i * 2 + 6],
                    bbuf[i * 2 + 7]);
            }
/*
            for (int i=0; i < 20; i++)
            {
                int k = i * alsa_dev.needed_chan * fwidth;
                int l = i * alsa_dev.needed_chan * dwidth;
                fprintf (stderr, "[%02x][%02x][%02x]:[%02x][%02x][%02x] | ",
                        bbuf2[k], bbuf2[k+1], bbuf2[k+2], bbuf2[k+3], bbuf2[k+4], bbuf2[k+5]);
                fprintf (stderr, "[%02x][%02x][%02x][%02x]:[%02x][%02x][%02x][%02x]\n",
                                   bbuf[l], bbuf[l+1], bbuf[l+2], bbuf[l+3],
                                                        bbuf[l+4], bbuf[l+5], bbuf[l+6], bbuf[l+7]);
            }*/
            fprintf (stderr, "-----:\n");

//            fprintf (stderr, "BBuf:\n");
//            for (int i=0; i < 100; i+=2)
//            {
//                //fprintf (stderr, "[%08x][%08x]\n", buf[i], buf[i+1]);
//                fprintf (stderr, "[%01x][%01x][%01x][%01x]\n",
//                                        bbuf2[i], bbuf2[i+1], bbuf2[i+2], bbuf2[i+3]);
//            }
//            fprintf (stderr, "-----:\n");

            have_to_print = false;
        }

        /* deliver the data */

        fprintf(stderr, "playback frm_to_deliver: frm[%ld]\n", (long)frames_to_deliver);

        if (playback_callback(bbuf, &alsa_dev, frames_to_deliver) != frames_to_deliver) {
            fprintf(stderr, "playback callback failed, frm[%ld]\n", (long)frames_to_deliver);
            break;
        }
    } // while

    if (!exit_flag) {
        snd_pcm_nonblock(alsa_dev.pcm_hndl, 0);
        if ((err = snd_pcm_drain(alsa_dev.pcm_hndl)) < 0) {
            fprintf (stderr, "cannot drain pcm. [%d](%s)\n", err, snd_strerror (err));
        } else {
            fprintf (stderr, "pcm drawn.\n");
        }
        snd_pcm_nonblock(alsa_dev.pcm_hndl, 0);
    }

exit_point:

    uninit_hwdev(&alsa_dev);

    if (file)
        fclose(file);
    exit (0);
}
