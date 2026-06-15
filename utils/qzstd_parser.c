/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2025 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * file qzstd_parser.c
 * This utility parses ZSTD compressed data and displays:
 * - Frame header information (magic number, descriptor, dict ID, frame size)
 * - Dictionary ID
 * - Entropy tables (Huffman and FSE tables)
 * - Block information (type, size, compressed/uncompressed)
 * - Checksum information
 ***************************************************************************/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>

#define ZSTD_MAGICNUMBER            0xFD2FB528
#define ZSTD_MAGIC_DICTIONARY       0xEC30A437
#define ZSTD_MAGIC_SKIPPABLE_START  0x184D2A50
#define ZSTD_MAGIC_SKIPPABLE_MASK   0xFFFFFFF0

/* Frame descriptor flags */
#define ZSTD_FRAME_CONTENT_SIZE_FLAG   0x03
#define ZSTD_FRAME_SINGLE_SEGMENT_FLAG 0x20
#define ZSTD_FRAME_CHECKSUM_FLAG       0x04
#define ZSTD_FRAME_DICT_ID_FLAG        0x03

/* Block types */
#define ZSTD_BLOCK_RAW        0
#define ZSTD_BLOCK_RLE        1
#define ZSTD_BLOCK_COMPRESSED 2
#define ZSTD_BLOCK_RESERVED   3

typedef struct {
    uint32_t magic_number;
    uint8_t frame_descriptor;
    uint32_t dict_id;
    uint64_t frame_content_size;
    uint8_t window_descriptor;
    int has_dict_id;
    int has_content_size;
    int has_checksum;
    int single_segment;
    uint8_t content_size_flag;
    uint8_t dict_id_flag;
} ZstdFrameHeader_t;

typedef struct {
    int is_last;
    int block_type;
    uint32_t block_size;
    const uint8_t* block_data;
} ZstdBlock_t;

typedef struct {
    uint32_t num_sequences;
    int has_literals;
    int compression_mode;
    /* FSE table types for sequences */
    int ll_mode;  /* Literal Lengths */
    int of_mode;  /* Offsets */
    int ml_mode;  /* Match Lengths */
} ZstdSequencesInfo_t;

static uint32_t read_le32(const uint8_t* ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) |
           ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

static uint64_t read_le64(const uint8_t* ptr) {
    return (uint64_t)ptr[0] | ((uint64_t)ptr[1] << 8) |
           ((uint64_t)ptr[2] << 16) | ((uint64_t)ptr[3] << 24) |
           ((uint64_t)ptr[4] << 32) | ((uint64_t)ptr[5] << 40) |
           ((uint64_t)ptr[6] << 48) | ((uint64_t)ptr[7] << 56);
}

static const char* get_block_type_name(int block_type) {
    switch (block_type) {
        case ZSTD_BLOCK_RAW: return "Raw (uncompressed)";
        case ZSTD_BLOCK_RLE: return "RLE (single byte repeated)";
        case ZSTD_BLOCK_COMPRESSED: return "Compressed";
        case ZSTD_BLOCK_RESERVED: return "Reserved (invalid)";
        default: return "Unknown";
    }
}

static const char* get_compression_mode_name(int mode) {
    switch (mode) {
        case 0: return "Predefined";
        case 1: return "RLE";
        case 2: return "FSE Compressed";
        case 3: return "Repeat";
        default: return "Unknown";
    }
}

static void print_hexdump(const uint8_t* data, size_t size, size_t offset, size_t max_bytes) {
    size_t bytes_to_print = (size < max_bytes) ? size : max_bytes;
    size_t i;

    for (i = 0; i < bytes_to_print; i += 16) {
        printf("  %08zx  ", offset + i);

        /* Print hex values */
        size_t j;
        for (j = 0; j < 16; j++) {
            if (i + j < bytes_to_print) {
                printf("%02x ", data[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) printf(" ");
        }

        printf(" |");

        /* Print ASCII representation */
        for (j = 0; j < 16 && i + j < bytes_to_print; j++) {
            uint8_t c = data[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }

        printf("|\n");
    }

    if (size > max_bytes) {
        printf("  ... (%zu more bytes)\n", size - max_bytes);
    }
}

static int parse_frame_header(const uint8_t* data, size_t size,
                              ZstdFrameHeader_t* header, size_t* consumed) {
    const uint8_t* ptr = data;
    size_t offset = 0;

    if (size < 4) {
        fprintf(stderr, "Error: Data too small for magic number\n");
        return -1;
    }

    /* Read magic number */
    header->magic_number = read_le32(ptr);
    offset += 4;

    /* Check for skippable frame */
    if ((header->magic_number & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
        printf("Skippable frame detected (magic: 0x%08X)\n", header->magic_number);
        if (size < 8) {
            fprintf(stderr, "Error: Incomplete skippable frame\n");
            return -1;
        }
        uint32_t frame_size = read_le32(ptr + 4);
        *consumed = 8 + frame_size;
        return 0;
    }

    /* Verify ZSTD magic number */
    if (header->magic_number != ZSTD_MAGICNUMBER) {
        fprintf(stderr, "Error: Invalid ZSTD magic number: 0x%08X (expected 0x%08X)\n",
                header->magic_number, ZSTD_MAGICNUMBER);
        return -1;
    }

    if (offset >= size) {
        fprintf(stderr, "Error: Data too small for frame descriptor\n");
        return -1;
    }

    /* Read frame descriptor */
    header->frame_descriptor = ptr[offset++];

    /* Parse frame descriptor */
    header->content_size_flag = (header->frame_descriptor >> 6) & 0x03;
    header->single_segment = (header->frame_descriptor & ZSTD_FRAME_SINGLE_SEGMENT_FLAG) != 0;
    header->has_checksum = (header->frame_descriptor & ZSTD_FRAME_CHECKSUM_FLAG) != 0;
    header->dict_id_flag = header->frame_descriptor & ZSTD_FRAME_DICT_ID_FLAG;

    /* Read window descriptor (if not single segment) */
    if (!header->single_segment) {
        if (offset >= size) {
            fprintf(stderr, "Error: Data too small for window descriptor\n");
            return -1;
        }
        header->window_descriptor = ptr[offset++];
    } else {
        header->window_descriptor = 0;
    }

    /* Read dictionary ID */
    header->has_dict_id = 0;
    header->dict_id = 0;
    if (header->dict_id_flag != 0) {
        header->has_dict_id = 1;
        int dict_id_size = 1 << (header->dict_id_flag - 1);
        if (offset + dict_id_size > size) {
            fprintf(stderr, "Error: Data too small for dictionary ID\n");
            return -1;
        }

        switch (dict_id_size) {
            case 1:
                header->dict_id = ptr[offset];
                break;
            case 2:
                header->dict_id = ptr[offset] | (ptr[offset + 1] << 8);
                break;
            case 4:
                header->dict_id = read_le32(ptr + offset);
                break;
        }
        offset += dict_id_size;
    }

    /* Read frame content size */
    header->has_content_size = 0;
    header->frame_content_size = 0;

    if (header->single_segment || header->content_size_flag != 0) {
        header->has_content_size = 1;
        int fcs_size;

        if (header->single_segment) {
            fcs_size = 1 << header->content_size_flag;
        } else {
            fcs_size = 1 << header->content_size_flag;
            if (header->content_size_flag == 0) {
                fcs_size = 0;
                header->has_content_size = 0;
            }
        }

        if (fcs_size > 0) {
            if (offset + fcs_size > size) {
                fprintf(stderr, "Error: Data too small for frame content size\n");
                return -1;
            }

            switch (fcs_size) {
                case 1:
                    header->frame_content_size = ptr[offset];
                    break;
                case 2:
                    header->frame_content_size = ptr[offset] | ((uint64_t)ptr[offset + 1] << 8);
                    break;
                case 4:
                    header->frame_content_size = read_le32(ptr + offset);
                    break;
                case 8:
                    header->frame_content_size = read_le64(ptr + offset);
                    break;
            }
            offset += fcs_size;
        }
    }

    *consumed = offset;
    return 0;
}

static void print_frame_header(const ZstdFrameHeader_t* header, const uint8_t* raw_data, size_t header_size) {
    printf("==================================================\n");
    printf("ZSTD Frame Header\n");
    printf("==================================================\n");
    printf("Magic Number:        0x%08X\n", header->magic_number);
    printf("Frame Descriptor:    0x%02X\n", header->frame_descriptor);
    printf("  Content Size Flag: %d ", header->content_size_flag);
    switch (header->content_size_flag) {
        case 0: printf("(Unknown size)\n"); break;
        case 1: printf("(1 byte)\n"); break;
        case 2: printf("(2 or 4 bytes)\n"); break;
        case 3: printf("(8 bytes)\n"); break;
    }
    printf("  Single Segment:    %s\n", header->single_segment ? "Yes" : "No");
    printf("  Has Checksum:      %s\n", header->has_checksum ? "Yes" : "No");
    printf("  Dict ID Flag:      %d\n", header->dict_id_flag);

    if (!header->single_segment) {
        printf("Window Descriptor:   0x%02X\n", header->window_descriptor);
        uint8_t exponent = (header->window_descriptor >> 3) & 0x1F;
        uint8_t mantissa = header->window_descriptor & 0x07;
        uint64_t window_base = 1ULL << (10 + exponent);
        uint64_t window_add = (window_base / 8) * mantissa;
        uint64_t window_size = window_base + window_add;
        printf("  Window Size:       %llu bytes (2^%d + %llu)\n",
               (unsigned long long)window_size, 10 + exponent,
               (unsigned long long)window_add);
    }

    if (header->has_dict_id) {
        printf("Dictionary ID:       %u (0x%08X)\n", header->dict_id, header->dict_id);
    } else {
        printf("Dictionary ID:       None\n");
    }

    if (header->has_content_size) {
        printf("Frame Content Size:  %llu bytes\n",
               (unsigned long long)header->frame_content_size);
    } else {
        printf("Frame Content Size:  Unknown\n");
    }

    printf("\nFrame Header Hex Dump (%zu bytes):\n", header_size);
    print_hexdump(raw_data, header_size, 0, header_size);
    printf("\n");
}

static int parse_block_header(const uint8_t* data, size_t size,
                              ZstdBlock_t* block, size_t* consumed) {
    if (size < 3) {
        fprintf(stderr, "Error: Data too small for block header\n");
        return -1;
    }

    /* Read 3-byte block header */
    uint32_t block_header = data[0] | (data[1] << 8) | (data[2] << 16);

    block->is_last = block_header & 0x01;
    block->block_type = (block_header >> 1) & 0x03;
    block->block_size = (block_header >> 3) & 0x1FFFFF;

    *consumed = 3;

    /* Validate block size doesn't exceed remaining data */
    if (3 + block->block_size > size) {
        fprintf(stderr, "Error: Block size (%u) exceeds remaining data (%zu)\n",
                block->block_size, size - 3);
        return -1;
    }

    block->block_data = data + 3;
    return 0;
}

static int parse_literals_section(const uint8_t* data, size_t size,
                                  uint32_t* literals_size, size_t* consumed) {
    if (size < 1) {
        return -1;
    }

    const uint8_t* ptr = data;
    uint8_t header = ptr[0];
    uint8_t literals_type = (header >> 6) & 0x03;
    size_t offset = 1;

    uint32_t regenerated_size = 0;
    uint32_t compressed_size = 0;

    if (literals_type == 0 || literals_type == 1) {
        /* Raw or RLE */
        int size_format = (header >> 4) & 0x03;

        switch (size_format) {
            case 0:
            case 2:
                /* 1 byte size */
                regenerated_size = header & 0x1F;
                break;
            case 1:
                /* 2 bytes size */
                if (offset >= size) return -1;
                regenerated_size = ((header & 0x0F) << 8) | ptr[offset];
                offset++;
                break;
            case 3:
                /* 3 bytes size */
                if (offset + 1 >= size) return -1;
                regenerated_size = ((header & 0x0F) << 16) | (ptr[offset] << 8) | ptr[offset + 1];
                offset += 2;
                break;
        }

        if (literals_type == 0) {
            /* Raw literals - no compression */
            compressed_size = regenerated_size;
        } else {
            /* RLE - single byte repeated */
            compressed_size = 1;
        }

    } else {
        /* Compressed (Huffman) or Treeless */
        int size_format = (header >> 4) & 0x03;

        switch (size_format) {
            case 0:
            case 1: {
                /* 1 stream, 1-3 bytes sizes */
                if (size_format == 0) {
                    /* 10 bits for both sizes */
                    if (offset >= size) return -1;
                    regenerated_size = ((header & 0x0F) << 6) | ((ptr[offset] >> 2) & 0x3F);
                    compressed_size = ((ptr[offset] & 0x03) << 8) | ptr[offset + 1];
                    offset += 2;
                } else {
                    /* 10 + 10 bits */
                    if (offset + 1 >= size) return -1;
                    regenerated_size = ((header & 0x0F) << 6) | ((ptr[offset] >> 2) & 0x3F);
                    compressed_size = ((ptr[offset] & 0x03) << 8) | ptr[offset + 1];
                    offset += 2;
                }
                break;
            }
            case 2: {
                /* 4 streams, 3 bytes sizes */
                if (offset + 2 >= size) return -1;
                regenerated_size = ((header & 0x0F) << 10) | (ptr[offset] << 2) | ((ptr[offset + 1] >> 6) & 0x03);
                compressed_size = ((ptr[offset + 1] & 0x3F) << 8) | ptr[offset + 2];
                offset += 3;
                break;
            }
            case 3: {
                /* 4 streams, 4 bytes sizes */
                if (offset + 3 >= size) return -1;
                regenerated_size = ((header & 0x0F) << 14) | (ptr[offset] << 6) | ((ptr[offset + 1] >> 2) & 0x3F);
                compressed_size = ((ptr[offset + 1] & 0x03) << 16) | (ptr[offset + 2] << 8) | ptr[offset + 3];
                offset += 4;
                break;
            }
        }
    }

    *literals_size = regenerated_size;
    *consumed = offset + compressed_size;

    printf("    Regenerated Size: %u bytes\n", regenerated_size);
    printf("    Compressed Size:  %u bytes\n", compressed_size);

    return 0;
}

static int parse_sequences_section(const uint8_t* data, size_t size,
                                   ZstdSequencesInfo_t* seq_info, size_t* consumed) {
    if (size < 1) {
        return -1;
    }

    const uint8_t* ptr = data;
    size_t offset = 0;

    /* Read number of sequences */
    uint8_t first_byte = ptr[offset++];

    if (first_byte == 0) {
        /* No sequences */
        seq_info->num_sequences = 0;
        *consumed = offset;
        return 0;
    } else if (first_byte < 128) {
        /* 1 byte */
        seq_info->num_sequences = first_byte;
    } else if (first_byte < 255) {
        /* 2 bytes */
        if (offset >= size) return -1;
        seq_info->num_sequences = ((first_byte - 128) << 8) | ptr[offset++];
    } else {
        /* 3 bytes */
        if (offset + 1 >= size) return -1;
        seq_info->num_sequences = ptr[offset] | (ptr[offset + 1] << 8);
        offset += 2;
    }

    if (seq_info->num_sequences == 0) {
        *consumed = offset;
        return 0;
    }

    /* Read compression modes for Literal_Lengths, Offsets, Match_Lengths */
    if (offset >= size) return -1;
    uint8_t compression_modes = ptr[offset++];

    seq_info->ll_mode = (compression_modes >> 6) & 0x03;
    seq_info->of_mode = (compression_modes >> 4) & 0x03;
    seq_info->ml_mode = (compression_modes >> 2) & 0x03;

    printf("    Number of Sequences: %u\n", seq_info->num_sequences);
    printf("    Compression Modes:\n");
    printf("      Literal Lengths: %s\n", get_compression_mode_name(seq_info->ll_mode));
    printf("      Offsets:         %s\n", get_compression_mode_name(seq_info->of_mode));
    printf("      Match Lengths:   %s\n", get_compression_mode_name(seq_info->ml_mode));

    /* Parse FSE tables if present (mode 2 = FSE Compressed) */
    if (seq_info->ll_mode == 2) {
        /* Literal Lengths FSE table present - would need full FSE decoder */
        printf("      LL FSE Table:    Present (detailed decode not implemented)\n");
    }
    if (seq_info->of_mode == 2) {
        /* Offsets FSE table present */
        printf("      OF FSE Table:    Present (detailed decode not implemented)\n");
    }
    if (seq_info->ml_mode == 2) {
        /* Match Lengths FSE table present */
        printf("      ML FSE Table:    Present (detailed decode not implemented)\n");
    }

    /* We can't easily determine the exact size without fully parsing FSE tables
     * and the bitstream, so we return a conservative estimate */
    *consumed = offset;

    return 0;
}

static void print_block_info(int block_num, const ZstdBlock_t* block, size_t block_offset) {
    printf("Block #%d:\n", block_num);
    printf("  Last Block:        %s\n", block->is_last ? "Yes" : "No");
    printf("  Block Type:        %s\n", get_block_type_name(block->block_type));
    printf("  Block Size:        %u bytes\n", block->block_size);

    /* Print block header hex dump (3 bytes) */
    printf("  Block Header Hex:  ");
    const uint8_t* header_ptr = block->block_data - 3;
    printf("%02x %02x %02x\n", header_ptr[0], header_ptr[1], header_ptr[2]);

    if (block->block_type == ZSTD_BLOCK_COMPRESSED && block->block_size > 0) {
        /* Parse compressed block header for entropy information */
        const uint8_t* block_data = block->block_data;
        size_t remaining_size = block->block_size;

        /* First byte contains literals section and sequences section headers */
        uint8_t literals_header = block_data[0];
        uint8_t literals_type = (literals_header >> 6) & 0x03;

        printf("  Literals Section:\n");
        printf("    Type:            ");
        switch (literals_type) {
            case 0: printf("Raw (uncompressed)\n"); break;
            case 1: printf("RLE (single byte)\n"); break;
            case 2: printf("Compressed (Huffman)\n"); break;
            case 3: printf("Treeless (reuse Huffman table)\n"); break;
        }

        /* Size format depends on literals type */
        if (literals_type == 0 || literals_type == 1) {
            /* Raw or RLE */
            int size_format = (literals_header >> 4) & 0x03;
            printf("    Size Format:     %d ", size_format);
            switch (size_format) {
                case 0:
                case 2:
                    printf("(1 byte)\n");
                    break;
                case 1:
                    printf("(2 bytes)\n");
                    break;
                case 3:
                    printf("(3 bytes)\n");
                    break;
            }
        } else {
            /* Compressed or Treeless */
            int size_format = (literals_header >> 4) & 0x03;
            printf("    Size Format:     %d ", size_format);
            switch (size_format) {
                case 0:
                case 1:
                    printf("(Streams: 1, 1-3 bytes size)\n");
                    break;
                case 2:
                    printf("(Streams: 4, 3 bytes size)\n");
                    break;
                case 3:
                    printf("(Streams: 4, 4 bytes size)\n");
                    break;
            }
        }

        /* Parse literals section to get sizes */
        uint32_t literals_size = 0;
        size_t literals_consumed = 0;
        if (parse_literals_section(block_data, remaining_size, &literals_size, &literals_consumed) == 0) {
            /* Successfully parsed literals */
            remaining_size -= literals_consumed;

            /* Parse sequences section */
            if (remaining_size > 0) {
                printf("  Sequences Section:\n");
                ZstdSequencesInfo_t seq_info = {0};
                size_t seq_consumed = 0;

                if (parse_sequences_section(block_data + literals_consumed,
                                           remaining_size, &seq_info, &seq_consumed) == 0) {
                    if (seq_info.num_sequences == 0) {
                        printf("    Number of Sequences: 0 (literals only block)\n");
                    }
                } else {
                    printf("    Error parsing sequences section\n");
                }
            }
        }
    } else if (block->block_type == ZSTD_BLOCK_RLE) {
        printf("  RLE Byte:          0x%02X\n", block->block_data[0]);
    }

    /* Print block data hex dump (up to 128 bytes) */
    if (block->block_size > 0) {
        printf("  Block Data Hex Dump (offset: 0x%zx, showing up to 128 bytes):\n", block_offset);
        print_hexdump(block->block_data, block->block_size, block_offset, 128);
    }

    printf("\n");
}

static int parse_zstd_dict(const uint8_t* data, size_t size, size_t* consumed) {
    if (size < 8) {
        fprintf(stderr, "Error: Data too small for ZSTD dictionary header\n");
        return -1;
    }

    uint32_t magic   = read_le32(data);
    uint32_t dict_id = read_le32(data + 4);

    printf("==================================================\n");
    printf("ZSTD Dictionary File\n");
    printf("==================================================\n");
    printf("Magic Number:        0x%08X (ZSTD dictionary)\n", magic);
    printf("Dictionary ID:       %u (0x%08X)\n", dict_id, dict_id);
    printf("Content Size:        %zu bytes (excluding 8-byte header)\n", size - 8);
    printf("\nHeader Hex Dump (8 bytes):\n");
    print_hexdump(data, 8, 0, 8);
    printf("\n");

    *consumed = size; /* treat the entire file as one dictionary "frame" */
    return 0;
}

static int parse_zstd_frame(const uint8_t* data, size_t size, size_t* frame_size) {
    ZstdFrameHeader_t header = {0};
    size_t offset = 0;
    size_t consumed;
    int ret;

    /* Check for ZSTD dictionary file */
    if (size >= 4 && read_le32(data) == ZSTD_MAGIC_DICTIONARY) {
        return parse_zstd_dict(data, size, frame_size);
    }

    /* Parse frame header */
    ret = parse_frame_header(data, size, &header, &consumed);
    if (ret != 0) {
        return ret;
    }

    /* Check for skippable frame */
    if ((header.magic_number & ZSTD_MAGIC_SKIPPABLE_MASK) == ZSTD_MAGIC_SKIPPABLE_START) {
        *frame_size = consumed;
        return 0;
    }

    print_frame_header(&header, data, consumed);
    offset += consumed;

    /* Parse blocks */
    printf("==================================================\n");
    printf("Blocks\n");
    printf("==================================================\n");

    int block_num = 1;
    int last_block = 0;

    while (!last_block && offset < size) {
        ZstdBlock_t block = {0};
        size_t block_start_offset = offset;

        ret = parse_block_header(data + offset, size - offset, &block, &consumed);
        if (ret != 0) {
            return ret;
        }

        print_block_info(block_num++, &block, block_start_offset + consumed);

        last_block = block.is_last;
        offset += consumed + block.block_size;
    }

    /* Check for checksum */
    if (header.has_checksum) {
        if (offset + 4 <= size) {
            uint32_t checksum = read_le32(data + offset);
            printf("==================================================\n");
            printf("Frame Checksum:      0x%08X (XXH32)\n", checksum);
            printf("==================================================\n");
            offset += 4;
        } else {
            fprintf(stderr, "Warning: Checksum flag set but not enough data\n");
        }
    }

    *frame_size = offset;
    printf("\nFrame bytes processed: %zu\n", offset);

    return 0;
}

static void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS] <compressed_file>\n", prog_name);
    printf("\nZSTD Compressed Data Parser\n");
    printf("\nParses and displays detailed information about ZSTD compressed frames:\n");
    printf("  - Frame header (magic, descriptor, window size)\n");
    printf("  - Dictionary ID\n");
    printf("  - Frame content size\n");
    printf("  - Block information (type, size, entropy details)\n");
    printf("  - Checksum\n");
    printf("\nOptions:\n");
    printf("  -v, --verbose         Enable verbose output\n");
    printf("  -h, --help            Display this help message\n");
    printf("\nExamples:\n");
    printf("  %s compressed.zst\n", prog_name);
    printf("  %s -v data.zst\n", prog_name);
    printf("\n");
}

int main(int argc, char** argv) {
    int opt;

    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v':
                /* Verbose mode - reserved for future use */
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: Missing compressed file\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[optind];

    /* Load compressed file */
    FILE* fp = fopen(input_file, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file: %s\n", input_file);
        return 1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    size_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Allocate buffer */
    uint8_t* data = malloc(file_size);
    if (!data) {
        fprintf(stderr, "Error: Failed to allocate memory (%zu bytes)\n", file_size);
        fclose(fp);
        return 1;
    }

    /* Read file */
    if (fread(data, 1, file_size, fp) != file_size) {
        fprintf(stderr, "Error: Failed to read file\n");
        free(data);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    printf("==================================================\n");
    printf("ZSTD Compressed Data Analysis\n");
    printf("==================================================\n");
    printf("File: %s\n", input_file);
    printf("Size: %zu bytes\n\n", file_size);

    /* Parse all ZSTD frames in the file */
    size_t total_offset = 0;
    int frame_num = 1;
    int ret = 0;

    while (total_offset < file_size) {
        size_t frame_size = 0;

        printf("\n");
        printf("**************************************************\n");
        printf("Frame #%d (offset: %zu)\n", frame_num, total_offset);
        printf("**************************************************\n");

        ret = parse_zstd_frame(data + total_offset, file_size - total_offset, &frame_size);
        if (ret != 0) {
            fprintf(stderr, "\nError: Failed to parse frame #%d at offset %zu\n",
                    frame_num, total_offset);
            break;
        }

        if (frame_size == 0) {
            fprintf(stderr, "\nError: Frame size is 0, stopping parse\n");
            ret = -1;
            break;
        }

        total_offset += frame_size;
        frame_num++;

        /* Safety check to prevent infinite loop */
        if (frame_num > 10000) {
            fprintf(stderr, "\nError: Too many frames (>10000), possible infinite loop\n");
            ret = -1;
            break;
        }
    }

    printf("\n");
    printf("==================================================\n");
    printf("Summary\n");
    printf("==================================================\n");
    printf("Total frames:        %d\n", frame_num - 1);
    printf("Total bytes parsed:  %zu / %zu\n", total_offset, file_size);
    if (total_offset == file_size) {
        printf("Status:              Complete ✓\n");
    } else {
        printf("Status:              Incomplete (%.1f%% parsed)\n",
               100.0 * total_offset / file_size);
    }

    free(data);

    if (ret != 0) {
        fprintf(stderr, "\nError: Failed to parse ZSTD data\n");
        return 1;
    }

    printf("\n==================================================\n");
    printf("Analysis Complete\n");
    printf("==================================================\n");

    return 0;
}
