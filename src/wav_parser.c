/*
 * wav_parser.c
 *
 *  Created on: Oct 27, 2022
 *      Author: dimciva
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

//#include <errno.h>
//#include <poll.h>
//#include <signal.h>

#include "wav_parser.h"

#define CHUNKSIZE       8
#define BUFFSIZE        24

const uint8_t chRIFF[4] = {'R','I','F','F'};
//const uint8_t chRIFF[4] = {'F','F','I','R'};
const uint8_t chWAVE[4] = {'W','A','V','E'};
const uint8_t chFMT_[4] = {'f','m','t',' '};
const uint8_t chDATA[4] = {'d','a','t','a'};

typedef union tag_u {
    uint8_t     Sig[4];    // Chunk type identifier
    uint32_t    Num;
} tag_t;

typedef struct {            // Chunk structure
    tag_t       ckID;
    uint32_t    ckSize;     // Chunk size field (size of ckData)
    uint8_t*    data;       // Chunk data
} CK;

typedef union fmt_data_u {
    fmt_chunk_t chunk;
    uint8_t     raw[BUFFSIZE];
} fmt_data_t;


bool find_RIFF(FILE* file, uint32_t* size)
{
    unsigned char buff[BUFFSIZE];
    size_t bytes_to_read = CHUNKSIZE;

    tag_t tagRIFF;
    memcpy(tagRIFF.Sig, chRIFF, sizeof(chRIFF));
    tag_t tagWAVE;
    memcpy(tagWAVE.Sig, chWAVE, sizeof(chWAVE));

    if ((file == NULL) || (size == NULL))
        return false;

    size_t bytes_read = fread(buff, 1, bytes_to_read, file);

    if (bytes_read == bytes_to_read)
    {
        CK* ck = (CK*) buff;
        if (ck->ckID.Num == tagRIFF.Num) // 'R''I''F''F'
        //if (memcmp(chRIFF, buff, 4))
        {
            fprintf (stderr, "ChunkSize(riff) [0x%08x][%u]\n", ck->ckSize, ck->ckSize);

            tag_t format;
            bytes_read = fread(format.Sig, 1, 4, file);

            if (format.Num == tagWAVE.Num)
            {
                *size = ck->ckSize;
                return true;
            }
        }
        else
        {
            fprintf (stderr, "ChunkID   [0x%x%x%x%x]<%c%c%c%c>\n", ck->ckID.Sig[0], ck->ckID.Sig[1], ck->ckID.Sig[2], ck->ckID.Sig[3], ck->ckID.Sig[0], ck->ckID.Sig[1], ck->ckID.Sig[2], ck->ckID.Sig[3]);
            fprintf (stderr, "ChunkSize [0x%08x][%u]\n", ck->ckSize, ck->ckSize);
        }
    }

    return false;
}

bool find_FMT(FILE* file, uint32_t* size, fmt_chunk_t* format)
{
    unsigned char buff[BUFFSIZE*2];
    size_t bytes_to_read = CHUNKSIZE;

    tag_t tagFMT;
    memcpy(tagFMT.Sig, chFMT_, sizeof(chFMT_));

    if ((file == NULL) || (size == NULL) || (format == NULL))
        return false;

    size_t bytes_read = fread(buff, 1, bytes_to_read, file);

    if (bytes_read == bytes_to_read)
    {
        CK* ck = (CK*) buff;
        if (ck->ckID.Num == tagFMT.Num) // 'f''m''t'' '
        {
            fprintf (stderr, "ChunkSize(fmt) [0x%08x][%u]\n", ck->ckSize, ck->ckSize);
            *size = ck->ckSize;

            bytes_to_read = (ck->ckSize < BUFFSIZE*2) ? ck->ckSize : BUFFSIZE*2;

            bytes_read = fread(buff, 1, bytes_to_read, file);

            fmt_data_t* fmtChunk = (fmt_data_t*)buff;

            if (bytes_read == bytes_to_read)
            {
                format->AudioFormat     = fmtChunk->chunk.AudioFormat;
                format->NumOfChan       = fmtChunk->chunk.NumOfChan;      // Number of channels 1=Mono 2=Sterio
                format->SamplesPerSec   = fmtChunk->chunk.SamplesPerSec;  // Sampling Frequency in Hz
                format->bytesPerSec     = fmtChunk->chunk.bytesPerSec;    // bytes per second
                format->blockAlign      = fmtChunk->chunk.blockAlign;     // 2=16-bit mono, 4=16-bit stereo
                format->bitsPerSample   = fmtChunk->chunk.bitsPerSample;  // Number of bits per sample

                return true;
            }
        }
        else
        {
            fprintf (stderr, "ChunkID   [0x%x%x%x%x]<%c%c%c%c>\n", ck->ckID.Sig[0], ck->ckID.Sig[1], ck->ckID.Sig[2], ck->ckID.Sig[3], ck->ckID.Sig[0], ck->ckID.Sig[1], ck->ckID.Sig[2], ck->ckID.Sig[3]);
            fprintf (stderr, "ChunkSize [0x%08x][%u]\n", ck->ckSize, ck->ckSize);
        }
    }

    return false;
}

bool find_DATA(FILE* file, uint32_t* size)
{
    unsigned char buff[BUFFSIZE*2];
    size_t bytes_to_read = CHUNKSIZE;

    tag_t tagData;
    memcpy(tagData.Sig, chDATA, sizeof(chDATA));

    if ((file == NULL) || (size == NULL))
        return false;

    size_t bytes_read = fread(buff, 1, bytes_to_read, file);

    if (bytes_read == bytes_to_read)
    {
        CK* ck = (CK*) buff;
        if (ck->ckID.Num == tagData.Num) // 'd''a''t''a'
        {
            fprintf (stderr, "ChunkSize(data) [0x%08x][%u]\n", ck->ckSize, ck->ckSize);

            *size = ck->ckSize;
            return true;
        }
    }
    return false;
}

void parse_wave_header1(FILE* file, fmt_chunk_t* format)
{
    uint32_t chunk_size = 0;

    if (find_RIFF(file, &chunk_size))
    {
        fprintf (stderr, "find_RIFF [0x%08x][%u]\n", chunk_size, chunk_size);

        if (find_FMT(file, &chunk_size, format))
        {
            fprintf (stderr, "find_FMT [0x%08x][%u]\n", chunk_size, chunk_size);

            fprintf (stderr, "AudioFormat [0x%04x][%u]\n",format->AudioFormat,format->AudioFormat);
            fprintf (stderr, "NumOfChan [0x%04x][%u]\n",format->NumOfChan,format->NumOfChan);
            fprintf (stderr, "SamplesPerSec [0x%08x][%u]\n",format->SamplesPerSec,format->SamplesPerSec);
            fprintf (stderr, "bytesPerSec [0x%08x][%u]\n",format->bytesPerSec,format->bytesPerSec);
            fprintf (stderr, "blockAlign [0x%04x][%u]\n",format->blockAlign,format->blockAlign);
            fprintf (stderr, "bitsPerSample [0x%04x][%u]\n",format->bitsPerSample,format->bitsPerSample);

            if (find_DATA(file, &chunk_size))
            {
                fprintf (stderr, "find_Data [0x%08x][%u] bytes of data.\n", chunk_size, chunk_size);
            }
        }
    }
    else
    {
        fprintf (stderr, "find_RIFF [false]\n");
    }
}
