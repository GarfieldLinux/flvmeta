/*
    $Id: flvmeta.c 87 2009-05-07 23:23:18Z marc.noirot $

    FLV Metadata updater

    Copyright (C) 2007, 2008, 2009 Marc Noirot <marc.noirot AT gmail.com>

    This file is part of FLVMeta.

    FLVMeta is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    FLVMeta is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FLVMeta; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include "flvmeta.h"
#include "flv.h"
#include "amf.h"
#include "avc.h"
#include "update.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct __flv_info {
    flv_header header;
    uint8 have_video;
    uint8 have_audio;
    uint32 video_width;
    uint32 video_height;
    uint8 video_codec;
    uint32 video_frames_number;
    uint8 audio_codec;
    uint8 audio_size;
    uint8 audio_rate;
    uint8 audio_stereo;
    file_offset_t video_data_size;
    file_offset_t audio_data_size;
    file_offset_t meta_data_size;
    file_offset_t real_video_data_size;
    file_offset_t real_audio_data_size;
    uint32 video_first_timestamp;
    uint32 audio_first_timestamp;
    uint8 can_seek_to_end;
    uint8 have_keyframes;
    uint32 last_keyframe_timestamp;
    uint32 on_metadata_size;
    file_offset_t on_metadata_offset;
    uint32 biggest_tag_body_size;
    uint32 last_timestamp;
    uint32 video_frame_duration;
    uint32 audio_frame_duration;
    file_offset_t total_prev_tags_size;
    uint8 have_on_last_second;
    amf_data * keyframes;
    amf_data * times;
    amf_data * filepositions;
} flv_info;

typedef struct __flv_metadata {
    amf_data * on_last_second_name;
    amf_data * on_last_second;
    amf_data * on_metadata_name;
    amf_data * on_metadata;
} flv_metadata;


/*
    compute Sorensen H.263 video size
*/
static size_t compute_h263_size(FILE * flv_in, flv_info * info) {
    byte header[9];
    size_t bytes_read = fread(header, 1, 9, flv_in);
    if (bytes_read == 9) {
        uint32 psc = uint24_be_to_uint32(*(uint24_be *)(header)) >> 7;
        if (psc == 1) {
            uint32 psize = ((header[3] & 0x03) << 1) + ((header[4] >> 7) & 0x01);
            switch (psize) {
                case 0:
                    info->video_width  = ((header[4] & 0x7f) << 1) + ((header[5] >> 7) & 0x01);
                    info->video_height = ((header[5] & 0x7f) << 1) + ((header[6] >> 7) & 0x01);
                    break;
                case 1:
                    info->video_width  = ((header[4] & 0x7f) << 9) + (header[5] << 1) + ((header[6] >> 7) & 0x01);
                    info->video_height = ((header[6] & 0x7f) << 9) + (header[7] << 1) + ((header[8] >> 7) & 0x01);
                    break;
                case 2:
                    info->video_width  = 352;
                    info->video_height = 288;
                    break;
                case 3:
                    info->video_width  = 176;
                    info->video_height = 144;
                    break;
                case 4:
                    info->video_width  = 128;
                    info->video_height = 96;
                    break;
                case 5:
                    info->video_width  = 320;
                    info->video_height = 240;
                    break;
                case 6:
                    info->video_width  = 160;
                    info->video_height = 120;
                    break;
                default:
                    break;
            }
        }
    }
    return bytes_read;
}

/*
    compute Screen video size
*/
static size_t compute_screen_size(FILE * flv_in, flv_info * info) {
    byte header[4];
    size_t bytes_read = fread(header, 1, 4, flv_in);
    if (bytes_read == 4) {
        info->video_width  = ((header[0] & 0x0f) << 8) + header[1];
        info->video_height = ((header[2] & 0x0f) << 8) + header[3];
    }
    return bytes_read;
}

/*
    compute On2 VP6 video size
*/
static size_t compute_vp6_size(FILE * flv_in, flv_info * info) {
    byte header[7], offset;
    size_t bytes_read = fread(header, 1, 7, flv_in);
    if (bytes_read == 7) {
        /* two bytes offset if VP6 0 */
        offset = (header[1] & 0x01 || !(header[2] & 0x06)) << 1;
        info->video_width  = (header[4 + offset] << 4) - (header[0] >> 4);
        info->video_height = (header[3 + offset] << 4) - (header[0] & 0x0f);
    }
    return bytes_read;
}

/*
    compute On2 VP6 with Alpha video size
*/
static size_t compute_vp6_alpha_size(FILE * flv_in, flv_info * info) {
    byte header[10], offset;
    size_t bytes_read = fread(header, 1, 10, flv_in);
    if (bytes_read == 10) {
        /* two bytes offset if VP6 0 */
        offset = (header[4] & 0x01 || !(header[5] & 0x06)) << 1;
        info->video_width  = (header[7 + offset] << 4) - (header[0] >> 4);
        info->video_height = (header[6 + offset] << 4) - (header[0] & 0x0f);
    }
    return bytes_read;
}

/*
    compute AVC (H.264) video size (experimental)
*/
static size_t compute_avc_size(FILE * flv_in, flv_info * info) {
    size_t bytes_read = read_avc_resolution(flv_in, &(info->video_width), &(info->video_height));
    return bytes_read;
}

/*
    compute video width and height from the first video frame
*/
static size_t compute_video_size(FILE * flv_in, flv_info * info) {
    size_t bytes_read = 0;
    switch (info->video_codec) {
        case FLV_VIDEO_TAG_CODEC_SORENSEN_H263:
            bytes_read = compute_h263_size(flv_in, info);
            break;
        case FLV_VIDEO_TAG_CODEC_SCREEN_VIDEO:
        case FLV_VIDEO_TAG_CODEC_SCREEN_VIDEO_V2:
            bytes_read = compute_screen_size(flv_in, info);
            break;
        case FLV_VIDEO_TAG_CODEC_ON2_VP6:
            bytes_read = compute_vp6_size(flv_in, info);
            break;
        case FLV_VIDEO_TAG_CODEC_ON2_VP6_ALPHA:
            bytes_read = compute_vp6_alpha_size(flv_in, info);
            break;
        case FLV_VIDEO_TAG_CODEC_AVC:
            bytes_read = compute_avc_size(flv_in, info);
    }
    return bytes_read;
}

/*
    read the flv file thoroughly to get all necessary information.

    we need to check :
    - timestamp of first audio for audio delay
    - whether we have audio and video
    - first frames codecs (audio, video)
    - total audio and video data sizes
    - keyframe offsets and timestamps
    - whether the last video frame is a keyframe
    - last keyframe timestamp
    - onMetaData tag total size
    - total tags size
    - first tag after onMetaData offset
    - last timestamp
    - real video data size, number of frames, duration to compute framerate and video data rate
    - real audio data size, duration to compute audio data rate
    - video headers to find width and height. (depends on the encoding)
*/
static int get_flv_info(FILE * flv_in, flv_info * info, const flvmeta_opts * opts) {
    uint32 prev_timestamp_video;
    uint32 prev_timestamp_audio;
    uint32 prev_timestamp_meta;
    uint8 timestamp_extended;
    uint32 tag_number;

    info->have_video = 0;
    info->have_audio = 0;
    info->video_width = 0;
    info->video_height = 0;
    info->video_codec = 0;
    info->video_frames_number = 0;
    info->audio_codec = 0;
    info->audio_size = 0;
    info->audio_rate = 0;
    info->audio_stereo = 0;
    info->video_data_size = 0;
    info->audio_data_size = 0;
    info->meta_data_size = 0;
    info->real_video_data_size = 0;
    info->real_audio_data_size = 0;
    info->video_first_timestamp = 0;
    info->audio_first_timestamp = 0;
    info->can_seek_to_end = 0;
    info->have_keyframes = 0;
    info->last_keyframe_timestamp = 0;
    info->on_metadata_size = 0;
    info->on_metadata_offset = 0;
    info->biggest_tag_body_size = 0;
    info->last_timestamp = 0;
    info->video_frame_duration = 0;
    info->audio_frame_duration = 0;
    info->total_prev_tags_size = 0;
    info->have_on_last_second = 0;
    info->keyframes = NULL;
    info->times = NULL;
    info->filepositions = NULL;

    if (opts->verbose) {
        fprintf(stdout, "Parsing %s...\n", opts->input_file);
    }

    /*
        read FLV header
    */

    if (fread(&(info->header), sizeof(flv_header), 1, flv_in) < 1) {
        return ERROR_NO_FLV;
    }

    if (info->header.signature[0] != 'F' || info->header.signature[1] != 'L' || info->header.signature[2] != 'V') {
        return ERROR_NO_FLV;
    }

    info->keyframes = amf_object_new();
    info->times = amf_array_new();
    info->filepositions = amf_array_new();
    amf_object_add(info->keyframes, amf_str("times"), info->times);
    amf_object_add(info->keyframes, amf_str("filepositions"), info->filepositions);

    /* skip first empty previous tag size */
    flvmeta_fseek(flv_in, sizeof(uint32_be), SEEK_CUR);
    info->total_prev_tags_size += sizeof(uint32_be);

    /* extended timestamp initialization */
    prev_timestamp_video = 0;
    prev_timestamp_audio = 0;
    prev_timestamp_meta = 0;
    timestamp_extended = 0;
    tag_number = 0;

    while (!feof(flv_in)) {
        flv_tag ft;
        file_offset_t offset;
        uint32 body_length;
        uint32 timestamp;

        offset = flvmeta_ftell(flv_in);
        if (fread(&ft, sizeof(flv_tag), 1, flv_in) == 0) {
            break;
        }

        body_length = uint24_be_to_uint32(ft.body_length);
        timestamp = flv_tag_get_timestamp(ft);

        /* extended timestamp fixing */
        if (ft.type == FLV_TAG_TYPE_META) {
            if (timestamp < prev_timestamp_meta) {
                ++timestamp_extended;
            }
            prev_timestamp_meta = timestamp;
        }
        else if (ft.type == FLV_TAG_TYPE_AUDIO) {
            if (timestamp < prev_timestamp_audio) {
                ++timestamp_extended;
            }
            prev_timestamp_audio = timestamp;
        }
        else if (ft.type == FLV_TAG_TYPE_VIDEO) {
            if (timestamp < prev_timestamp_video) {
                ++timestamp_extended;
            }
            prev_timestamp_video = timestamp;
        }

        if (timestamp_extended > 0) {
            timestamp += timestamp_extended << 24;
        }

        if (info->biggest_tag_body_size < body_length) {
            info->biggest_tag_body_size = body_length;
        }
        info->last_timestamp = timestamp;

        if (ft.type == FLV_TAG_TYPE_META) {
            amf_data * tag_name = amf_data_file_read(flv_in);
            if (tag_name == NULL) {
                return ERROR_EOF;
            }

            /* just ignore metadata that don't have a proper name */
            if (amf_data_get_type(tag_name) == AMF_TYPE_STRING) {
                char * name = (char *)amf_string_get_bytes(tag_name);
                size_t len = (size_t)amf_string_get_size(tag_name);

                /* get info only on the first onMetaData we read */
                if (info->on_metadata_size == 0 && !strncmp(name, "onMetaData", len)) {
                    info->on_metadata_size = body_length + sizeof(flv_tag) + sizeof(uint32_be);
                    info->on_metadata_offset = offset;
                }
                else {
                    if (!strncmp(name, "onLastSecond", len)) {
                        info->have_on_last_second = 1;
                    }
                    info->meta_data_size += (body_length + sizeof(flv_tag));
                    info->total_prev_tags_size += sizeof(uint32_be);
                }
            }
            else {
                info->meta_data_size += (body_length + sizeof(flv_tag));
                info->total_prev_tags_size += sizeof(uint32_be);
            }

            body_length -= (uint32)amf_data_size(tag_name);
            amf_data_free(tag_name);
        }
        else if (ft.type == FLV_TAG_TYPE_VIDEO) {
            flv_video_tag vt;
            size_t bytes_read = 0;

            if (fread(&vt, sizeof(flv_video_tag), 1, flv_in) == 0) {
                return ERROR_EOF;
            }

            if (info->have_video != 1) {
                info->have_video = 1;
                info->video_codec = flv_video_tag_codec_id(vt);
                info->video_first_timestamp = timestamp;

                /* read first video frame to get critical info */
                bytes_read = compute_video_size(flv_in, info);
            }

            /* add keyframe to list */
            if (flv_video_tag_frame_type(vt) == FLV_VIDEO_TAG_FRAME_TYPE_KEYFRAME) {
                if (!info->have_keyframes) {
                    info->have_keyframes = 1;
                }
                info->last_keyframe_timestamp = timestamp;
                amf_array_push(info->times, amf_number_new(timestamp / 1000.0));
                amf_array_push(info->filepositions, amf_number_new((number64)offset));

                /* is last frame a key frame ? if so, we can seek to end */
                info->can_seek_to_end = 1;
            }
            else {
                info->can_seek_to_end = 0;
            }

            info->video_frames_number++;

            /*
                we assume all video frames have the same size as the first one:
                probably bogus but only used in case there's no audio in the file
            */
            if (info->video_frame_duration == 0 && timestamp != 0) {
                info->video_frame_duration = timestamp;
            }

            info->video_data_size += (body_length + sizeof(flv_tag));
            info->real_video_data_size += (body_length - 1);

            body_length -= (uint32)(sizeof(flv_video_tag) + bytes_read);
            info->total_prev_tags_size += sizeof(uint32_be);
        }
        else if (ft.type == FLV_TAG_TYPE_AUDIO) {
            flv_audio_tag at;
            
            if (fread(&at, sizeof(flv_audio_tag), 1, flv_in) == 0) {
                return ERROR_EOF;
            }
            
            if (info->have_audio != 1) {
                info->have_audio = 1;
                info->audio_codec = flv_audio_tag_sound_format(at);
                info->audio_rate = flv_audio_tag_sound_rate(at);
                info->audio_size = flv_audio_tag_sound_size(at);
                info->audio_stereo = flv_audio_tag_sound_type(at);
                info->audio_first_timestamp = timestamp;
            }
            /* we assume all audio frames have the same size as the first one */
            if (info->audio_frame_duration == 0 && timestamp != 0) {
                info->audio_frame_duration = timestamp;
            }

            info->audio_data_size += (body_length + sizeof(flv_tag));
            info->real_audio_data_size += (body_length - 1);

            body_length -= sizeof(flv_audio_tag);
            info->total_prev_tags_size += sizeof(uint32_be);
        }
        else {
            if (opts->error_handling == FLVMETA_FIX_ERRORS) {
                /* TODO : fix errors if possible */
            }
            else if (opts->error_handling == FLVMETA_IGNORE_ERRORS) {
                /* let's continue the parsing */
                if (opts->verbose) {
                    fprintf(stdout, "Warning: invalid tag at 0x"
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
                        "%llX"
#else
                        "%lX"
#endif
                        "\n", offset);
                }
                info->total_prev_tags_size += sizeof(uint32_be);
            }
            else {
                return ERROR_INVALID_TAG;
            }
        }

        body_length += sizeof(uint32_be); /* skip tag size */
        flvmeta_fseek(flv_in, body_length, SEEK_CUR);

        ++tag_number;
    }

    if (opts->verbose) {
        fprintf(stdout, "Found %d tags\n", tag_number);
    }

    return OK;
}

/*
    compute the metadata
*/
static void compute_metadata(flv_info * info, flv_metadata * meta, const flvmeta_opts * opts) {
    uint32 new_on_metadata_size, on_last_second_size;
    file_offset_t total_data_size, total_filesize;
    number64 duration, video_data_rate, framerate;
    amf_data * amf_total_filesize;
    amf_data * amf_total_data_size;
    amf_node * node_t;
    amf_node * node_f;

    if (opts->verbose) {
        fprintf(stdout, "Computing metadata...\n");
    }

    meta->on_last_second_name = amf_str("onLastSecond");
    meta->on_last_second = amf_associative_array_new();
    meta->on_metadata_name = amf_str("onMetaData");

    if (opts->metadata == NULL) {
        meta->on_metadata = amf_associative_array_new();
    }
    else {
        meta->on_metadata = opts->metadata;
    }

    amf_associative_array_add(meta->on_metadata, amf_str("hasMetadata"), amf_boolean_new(1));
    amf_associative_array_add(meta->on_metadata, amf_str("hasVideo"), amf_boolean_new(info->have_video));
    amf_associative_array_add(meta->on_metadata, amf_str("hasAudio"), amf_boolean_new(info->have_audio));
    
    if (info->have_audio) {
        duration = (info->last_timestamp + info->audio_frame_duration) / 1000.0;
    }
    else {
        duration = (info->last_timestamp + info->video_frame_duration) / 1000.0;
    }
    amf_associative_array_add(meta->on_metadata, amf_str("duration"), amf_number_new(duration));

    amf_associative_array_add(meta->on_metadata, amf_str("lasttimestamp"), amf_number_new(info->last_timestamp / 1000.0));
    amf_associative_array_add(meta->on_metadata, amf_str("lastkeyframetimestamp"), amf_number_new(info->last_keyframe_timestamp / 1000.0));
    
    if (info->video_width > 0)
        amf_associative_array_add(meta->on_metadata, amf_str("width"), amf_number_new(info->video_width));
    if (info->video_height > 0)
        amf_associative_array_add(meta->on_metadata, amf_str("height"), amf_number_new(info->video_height));

    video_data_rate = ((info->real_video_data_size / 1024.0) * 8.0) / duration;
    amf_associative_array_add(meta->on_metadata, amf_str("videodatarate"), amf_number_new(video_data_rate));

    framerate = info->video_frames_number / duration;
    amf_associative_array_add(meta->on_metadata, amf_str("framerate"), amf_number_new(framerate));

    if (info->have_audio) {
        number64 audio_khz, audio_sample_rate;
        number64 audio_data_rate = ((info->real_audio_data_size / 1024.0) * 8.0) / duration;
        amf_associative_array_add(meta->on_metadata, amf_str("audiodatarate"), amf_number_new(audio_data_rate));

        audio_khz = 0.0;
        switch (info->audio_rate) {
            case FLV_AUDIO_TAG_SOUND_RATE_5_5: audio_khz = 5500.0; break;
            case FLV_AUDIO_TAG_SOUND_RATE_11:  audio_khz = 11000.0; break;
            case FLV_AUDIO_TAG_SOUND_RATE_22:  audio_khz = 22000.0; break;
            case FLV_AUDIO_TAG_SOUND_RATE_44:  audio_khz = 44000.0; break;
        }
        amf_associative_array_add(meta->on_metadata, amf_str("audiosamplerate"), amf_number_new(audio_khz));
        audio_sample_rate = 0.0;
        switch (info->audio_size) {
            case FLV_AUDIO_TAG_SOUND_SIZE_8:  audio_sample_rate = 8.0; break;
            case FLV_AUDIO_TAG_SOUND_SIZE_16: audio_sample_rate = 16.0; break;
        }
        amf_associative_array_add(meta->on_metadata, amf_str("audiosamplesize"), amf_number_new(audio_sample_rate));
        amf_associative_array_add(meta->on_metadata, amf_str("stereo"), amf_boolean_new(info->audio_stereo == FLV_AUDIO_TAG_SOUND_TYPE_STEREO));
    }

    /* to be computed later */
    amf_total_filesize = amf_number_new(0);
    amf_associative_array_add(meta->on_metadata, amf_str("filesize"), amf_total_filesize);

    if (info->have_video) {
        amf_associative_array_add(meta->on_metadata, amf_str("videosize"), amf_number_new((number64)info->video_data_size));
    }
    if (info->have_audio) {
        amf_associative_array_add(meta->on_metadata, amf_str("audiosize"), amf_number_new((number64)info->audio_data_size));
    }

    /* to be computed later */
    amf_total_data_size = amf_number_new(0);
    amf_associative_array_add(meta->on_metadata, amf_str("datasize"), amf_total_data_size);

    amf_associative_array_add(meta->on_metadata, amf_str("metadatacreator"), amf_str(PACKAGE_STRING));

    amf_associative_array_add(meta->on_metadata, amf_str("metadatadate"), amf_date_new((number64)time(NULL)*1000, 0));
    if (info->have_audio) {
        amf_associative_array_add(meta->on_metadata, amf_str("audiocodecid"), amf_number_new((number64)info->audio_codec));
    }
    if (info->have_video) {
        amf_associative_array_add(meta->on_metadata, amf_str("videocodecid"), amf_number_new((number64)info->video_codec));
    }
    if (info->have_audio && info->have_video) {
        number64 audio_delay = ((sint32)info->audio_first_timestamp - (sint32)info->video_first_timestamp) / 1000.0;
        amf_associative_array_add(meta->on_metadata, amf_str("audiodelay"), amf_number_new((number64)audio_delay));
    }
    amf_associative_array_add(meta->on_metadata, amf_str("canSeekToEnd"), amf_boolean_new(info->can_seek_to_end));
    amf_associative_array_add(meta->on_metadata, amf_str("hasCuePoints"), amf_boolean_new(0));
    amf_associative_array_add(meta->on_metadata, amf_str("cuePoints"), amf_array_new());
    amf_associative_array_add(meta->on_metadata, amf_str("hasKeyframes"), amf_boolean_new(info->have_keyframes));
    amf_associative_array_add(meta->on_metadata, amf_str("keyframes"), info->keyframes);

    /*
        When we know the final size, we can recompute te offsets for the filepositions, and the final datasize.
    */
    new_on_metadata_size = sizeof(flv_tag) + sizeof(uint32_be) +
        (uint32)(amf_data_size(meta->on_metadata_name) + amf_data_size(meta->on_metadata));
    on_last_second_size = (uint32)(amf_data_size(meta->on_last_second_name) + amf_data_size(meta->on_last_second));

    node_t = amf_array_first(info->times);
    node_f = amf_array_first(info->filepositions);
    while (node_t != NULL || node_f != NULL) {
        amf_data * amf_filepos = amf_array_get(node_f);
        number64 offset = amf_number_get_value(amf_filepos) + new_on_metadata_size - info->on_metadata_size;
        number64 timestamp = amf_number_get_value(amf_array_get(node_t));

        /* after the onLastSecond event we need to take in account the tag size */
        if (!info->have_on_last_second && (duration - timestamp) <= 1.0) {
            offset += (sizeof(flv_tag) + on_last_second_size + sizeof(uint32_be));
        }

        amf_number_set_value(amf_filepos, offset);
        node_t = amf_array_next(node_t);
        node_f = amf_array_next(node_f);
    }

    total_data_size = info->video_data_size + info->audio_data_size + info->meta_data_size + new_on_metadata_size;
    if (!info->have_on_last_second) {
        total_data_size += (uint32)on_last_second_size;
    }
    amf_number_set_value(amf_total_data_size, (number64)total_data_size);

    total_filesize = sizeof(flv_header) + total_data_size + info->total_prev_tags_size;
    if (!info->have_on_last_second) {
        /* if we have to add onLastSecond, we must count the header and new prevTagSize we add */
        total_filesize += (sizeof(flv_tag) + sizeof(uint32_be));
    }
    amf_number_set_value(amf_total_filesize, (number64)total_filesize);
}

/*
    Write the flv output file
*/
static int write_flv(FILE * flv_in, FILE * flv_out, const flv_info * info, const flv_metadata * meta, const flvmeta_opts * opts) {
    uint32_be size;
    uint32 on_metadata_name_size;
    uint32 on_metadata_size;
    uint32 prev_timestamp;
    uint8 timestamp_extended;
    byte * copy_buffer;
    uint32 duration;
    flv_tag omft;
    int have_on_last_second;

    if (opts->verbose) {
        fprintf(stdout, "Writing %s...\n", opts->output_file);
    }

    /* write the flv header */
    if (fwrite(&info->header, sizeof(flv_header), 1, flv_out) != 1) {
        return ERROR_WRITE;
    }

    /* first "previous tag size" */
    size = swap_uint32(0);
    if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
        return ERROR_WRITE;
    }

    /* create the onMetaData tag */
    on_metadata_name_size = (uint32)amf_data_size(meta->on_metadata_name);
    on_metadata_size = (uint32)amf_data_size(meta->on_metadata);

    omft.type = FLV_TAG_TYPE_META;
    omft.body_length = uint32_to_uint24_be(on_metadata_name_size + on_metadata_size);
    flv_tag_set_timestamp(&omft, 0);
    omft.stream_id = uint32_to_uint24_be(0);
    
    /* write the computed onMetaData tag first if it doesn't exist in the input file */
    if (info->on_metadata_size == 0) {
        if (fwrite(&omft, sizeof(flv_tag), 1, flv_out) != 1 ||
            amf_data_file_write(meta->on_metadata_name, flv_out) < on_metadata_name_size ||
            amf_data_file_write(meta->on_metadata, flv_out) < on_metadata_size
        ) {
            return ERROR_WRITE;
        }

        /* previous tag size */
        size = swap_uint32(sizeof(flv_tag) + on_metadata_name_size + on_metadata_size);
        if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
            return ERROR_WRITE;
        }
    }

    /* compute file duration */
    if (info->have_audio) {
        duration = info->last_timestamp + info->audio_frame_duration;
    }
    else {
        duration = info->last_timestamp + info->video_frame_duration;
    }

    /* extended timestamp initialization */
    prev_timestamp = 0;
    timestamp_extended = 0;

    /* copy the tags verbatim */
    flvmeta_fseek(flv_in, sizeof(flv_header)+sizeof(uint32_be), SEEK_SET);

    copy_buffer = (byte *)malloc(info->biggest_tag_body_size);
    have_on_last_second = 0;
    while (!feof(flv_in)) {
        file_offset_t offset;
        uint32 body_length;
        uint32 timestamp;
        flv_tag ft;

        offset = ftell(flv_in);

        if (fread(&ft, sizeof(ft), 1, flv_in) == 0) {
            break;
        }

        body_length = uint24_be_to_uint32(ft.body_length);
        timestamp = flv_tag_get_timestamp(ft);

        /* extended timestamp fixing */
        if (timestamp < prev_timestamp) {
            ++timestamp_extended;
        }
        prev_timestamp = timestamp;
        if (timestamp_extended > 0) {
            timestamp += timestamp_extended << 24;
        }
        flv_tag_set_timestamp(&ft, timestamp);

        /* if we're at the offset of the first onMetaData tag in the input file,
           we write the one we computed instead, discarding the old one */
        if (info->on_metadata_offset == offset) {
            if (fwrite(&omft, sizeof(flv_tag), 1, flv_out) != 1 ||
                amf_data_file_write(meta->on_metadata_name, flv_out) < on_metadata_name_size ||
                amf_data_file_write(meta->on_metadata, flv_out) < on_metadata_size
            ) {
                free(copy_buffer);
                return ERROR_WRITE;
            }

            /* previous tag size */
            size = swap_uint32(sizeof(flv_tag) + on_metadata_name_size + on_metadata_size);
            if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
                free(copy_buffer);
                return ERROR_WRITE;
            }

            flvmeta_fseek(flv_in, body_length + sizeof(uint32_be), SEEK_CUR);
        }
        else {
            /* insert an onLastSecond metadata tag */
            if (!have_on_last_second && !info->have_on_last_second && (duration - timestamp) <= 1000) {
                flv_tag tag;
                uint32 on_last_second_name_size = (uint32)amf_data_size(meta->on_last_second_name);
                uint32 on_last_second_size = (uint32)amf_data_size(meta->on_last_second);
                tag.type = FLV_TAG_TYPE_META;
                tag.body_length = uint32_to_uint24_be(on_last_second_name_size + on_last_second_size);
                tag.timestamp = ft.timestamp;
                tag.timestamp_extended = ft.timestamp_extended;
                tag.stream_id = uint32_to_uint24_be(0);
                if (fwrite(&tag, sizeof(flv_tag), 1, flv_out) != 1 ||
                    amf_data_file_write(meta->on_last_second_name, flv_out) < on_last_second_name_size ||
                    amf_data_file_write(meta->on_last_second, flv_out) < on_last_second_size
                ) {
                    free(copy_buffer);
                    return ERROR_WRITE;
                }

                /* previous tag size */
                size = swap_uint32(sizeof(flv_tag) + on_last_second_name_size + on_last_second_size);
                if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
                    free(copy_buffer);
                    return ERROR_WRITE;
                }

                have_on_last_second = 1;
            }

            /* copy the tag verbatim */
            if (fread(copy_buffer, 1, body_length, flv_in) < body_length) {
                free(copy_buffer);
                return ERROR_EOF;
            }
            if (fwrite(&ft, sizeof(flv_tag), 1, flv_out) != 1 ||
                fwrite(copy_buffer, 1, body_length, flv_out) < body_length) {
                free(copy_buffer);
                return ERROR_WRITE;
            }

            /* previous tag length */
            size = swap_uint32(sizeof(flv_tag) + body_length);
            if (fwrite(&size, sizeof(uint32_be), 1, flv_out) != 1) {
                free(copy_buffer);
                return ERROR_WRITE;
            }

            flvmeta_fseek(flv_in, sizeof(uint32_be), SEEK_CUR);
        }        
    }

    if (opts->verbose) {
        fprintf(stdout, "%s sucessfully written\n", opts->output_file);
    }

    free(copy_buffer);
    return OK;
}

/* copy a FLV file while adding onMetaData and onLastSecond events */
int update_metadata(const flvmeta_opts * opts) {
    int res;
    struct stat in_file_info;
    FILE * flv_in;
    FILE * flv_out;
    flv_info info;
    flv_metadata meta;

    if (!strcmp(opts->input_file, opts->output_file)) {
        return ERROR_SAME_FILE;
    }
    
    if (stat(opts->input_file, &in_file_info) != 0 || !(in_file_info.st_mode & (S_IFMT | S_IREAD))) {
        return ERROR_OPEN_READ;
    }
    flv_in = fopen(opts->input_file, "rb");
    if (flv_in == NULL) {
        return ERROR_OPEN_READ;
    }

    /*
        get all necessary information from the flv file
    */
    res = get_flv_info(flv_in, &info, opts);
    if (res != OK) {
        fclose(flv_in);
        amf_data_free(info.keyframes);
        return res;
    }

    compute_metadata(&info, &meta, opts);

    /* debug */
    /* amf_data_dump(stderr, meta.on_metadata, 0); */
    
    /*
        open output file
    */
    flv_out = fopen(opts->output_file, "wb");
    if (flv_out == NULL) {
        fclose(flv_in);
        amf_data_free(meta.on_last_second_name);
        amf_data_free(meta.on_last_second);
        amf_data_free(meta.on_metadata_name);
        amf_data_free(meta.on_metadata);
        return ERROR_OPEN_WRITE;
    }

    /*
        write the output file
    */
    res = write_flv(flv_in, flv_out, &info, &meta, opts);

    fclose(flv_in);
    fclose(flv_out);
    amf_data_free(meta.on_last_second_name);
    amf_data_free(meta.on_last_second);
    amf_data_free(meta.on_metadata_name);
    amf_data_free(meta.on_metadata);

    return res;
}
