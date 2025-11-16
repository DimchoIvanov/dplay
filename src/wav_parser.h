/*
 * wav_parser.h
 *
 *  Created on: Oct 27, 2022
 *      Author: dimciva
 */

#ifndef WAV_PARSER_H_
#define WAV_PARSER_H_

typedef struct fmt_chunk_s {
    // "fmt" sub-chunk properties
    uint16_t        AudioFormat;    // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM
    uint16_t        NumOfChan;      // Number of channels 1=Mono 2=Sterio
    uint32_t        SamplesPerSec;  // Sampling Frequency in Hz
    uint32_t        bytesPerSec;    // bytes per second
    uint16_t        blockAlign;     // 2=16-bit mono, 4=16-bit stereo
    uint16_t        bitsPerSample;  // Number of bits per sample
} fmt_chunk_t;

void parse_wave_header1(FILE* file, fmt_chunk_t* format);

#endif /* WAV_PARSER_H_ */
