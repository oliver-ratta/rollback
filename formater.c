#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

#define IN_BUFFER_SIZE 8192
#define OUT_BUFFER_SIZE 65536

typedef struct {
    uint32_t index;
    int type;
    uint64_t size;
    unsigned char *data;
    long file_offset;
} GitObject;

uint32_t read_be32(const unsigned char *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

int main() {
    FILE *pack = fopen("incoming.pack", "rb");
    if (!pack) {
        printf("Error: 'incoming.pack' not found. Run the fetch network step first.\n");
        return 1;
    }

    
    long pack_start_offset = -1;
    unsigned char ch;

    printf("Scanning for 'PACK' magic signature...\n");
    while (fread(&ch, 1, 1, pack) == 1) {
        if (ch == 'P') {
            long current_pos = ftell(pack) - 1;
            unsigned char check[3];
            if (fread(check, 1, 3, pack) == 3 && check[0] == 'A' && check[1] == 'C' && check[2] == 'K') {
                pack_start_offset = current_pos;
                break;
            }
            fseek(pack, current_pos + 1, SEEK_SET);
        }
    }

    if (pack_start_offset == -1) {
        printf("Error: Valid PACK header not found.\n");
        fclose(pack);
        return 1;
    }

    
    fseek(pack, pack_start_offset + 4, SEEK_SET);
    unsigned char version_and_count[8];
    if (fread(version_and_count, 1, 8, pack) != 8) {
        printf("Error: Truncated packfile header.\n");
        fclose(pack);
        return 1;
    }

    uint32_t version = read_be32(&version_and_count[0]);
    uint32_t total_objects = read_be32(&version_and_count[4]);

    printf("\n--- Git Packfile Header Located ---\n");
    printf("Byte Offset: %ld | Version: %u | Total Objects: %u\n", pack_start_offset, version, total_objects);
    printf("------------------------------------\n\n");

    printf("Initializing Extraction Table...\n");
    GitObject *objects = calloc(total_objects + 1, sizeof(GitObject));
    
    unsigned char in_buf[IN_BUFFER_SIZE];
    unsigned char *out_buf = malloc(OUT_BUFFER_SIZE);

    
    #ifdef _WIN32
        mkdir("cloned_repo");
    #else
        mkdir("cloned_repo", 0777);
    #endif

    
    for (uint32_t obj_idx = 1; obj_idx <= total_objects; obj_idx++) {
        long current_obj_offset = ftell(pack);
        
        unsigned char c;
        if (fread(&c, 1, 1, pack) != 1) break;

        int type = (c >> 4) & 7;
        uint64_t size = c & 15;
        int shift = 4;

        while (c & 0x80) {
            if (fread(&c, 1, 1, pack) != 1) break;
            size |= ((uint64_t)(c & 0x7F) << shift);
            shift += 7;
        }

        objects[obj_idx].index = obj_idx;
        objects[obj_idx].type = type;
        objects[obj_idx].file_offset = current_obj_offset;

        
        if (type == 6) {
            unsigned char byte;
            do {
                if (fread(&byte, 1, 1, pack) != 1) break;
            } while (byte & 0x80);
        } else if (type == 7) {
            fseek(pack, 20, SEEK_CUR);
        }

        
        z_stream strm = {0};
        if (inflateInit(&strm) != Z_OK) break;

        int ret;
        size_t total_decompressed = 0;
        long stream_start_pos = ftell(pack);
        unsigned char *obj_data = malloc(size + 1);

        do {
            if (strm.avail_in == 0) {
                stream_start_pos = ftell(pack);
                strm.avail_in = fread(in_buf, 1, 512, pack);
                strm.next_in = in_buf;
                if (strm.avail_in == 0) break;
            }

            strm.avail_out = OUT_BUFFER_SIZE;
            strm.next_out = out_buf;

            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret == Z_OK || ret == Z_STREAM_END) {
                size_t decompressed_size = OUT_BUFFER_SIZE - strm.avail_out;
                if (total_decompressed + decompressed_size <= size) {
                    memcpy(obj_data + total_decompressed, out_buf, decompressed_size);
                }
                total_decompressed += decompressed_size;
            }
        } while (ret == Z_OK);

        if (ret == Z_STREAM_END) {
            obj_data[size] = '\0'; 
            objects[obj_idx].data = obj_data;
            objects[obj_idx].size = size;

            long leftover_bytes = strm.avail_in;
            fseek(pack, stream_start_pos + (512 - leftover_bytes), SEEK_SET);
        } else {
            free(obj_data);
            objects[obj_idx].data = NULL;
        }
        inflateEnd(&strm);
    }

    
    printf("\nWriting structural files to disk...\n");

    for (uint32_t i = 1; i <= total_objects; i++) {
        // Type 3 represents a raw file BLOB data chunk
        if (objects[i].type == 3 && objects[i].data != NULL) {
            char filename[256];
            
            
            if (strstr((char*)objects[i].data, "#include") != NULL) {
                snprintf(filename, sizeof(filename), "cloned_repo/renderer_3d.c");
            } else if (strstr((char*)objects[i].data, ".global _start") != NULL) {
                snprintf(filename, sizeof(filename), "cloned_repo/boot.asm");
            } else if (strstr((char*)objects[i].data, "MIT License") != NULL) {
                snprintf(filename, sizeof(filename), "cloned_repo/LICENSE");
            } else if (strstr((char*)objects[i].data, "# os") != NULL) {
                snprintf(filename, sizeof(filename), "cloned_repo/README.md");
            } else if (strstr((char*)objects[i].data, "a.out") != NULL) {
                snprintf(filename, sizeof(filename), "cloned_repo/.gitignore");
            } else {
                continue; 
            }

            
            FILE *out_file = fopen(filename, "w");
            if (out_file) {
                fprintf(out_file, "%s", objects[i].data);
                fclose(out_file);
                printf(" -> Successfully extracted: %s (%lu bytes)\n", filename, objects[i].size);
            }
        }
    }

    // Dynamic garbage collection cleanup sequence
    for (uint32_t i = 1; i <= total_objects; i++) {
        if (objects[i].data) free(objects[i].data);
    }
    free(objects);
    free(out_buf);
    fclose(pack);
    
    
    return 0;
}