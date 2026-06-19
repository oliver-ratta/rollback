#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <zlib.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

char pd[1024] = "";
char logstr[4096] = "";
char cwd[1024];
char repo_url[1024] = "";
char token[1024] = "";
char username[1024] = "";
char useremail[1024] = "";

static void trim_line(char *text) {
    text[strcspn(text, "\r\n")] = '\0';
}

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
   
    return size * nmemb;
}
#define BUFFER_SIZE 512000
#define MAX_FILES 256

typedef struct {
    char name[256];
    unsigned char sha1[20];
    char mode[7];
    int is_dir;
} FileEntry;

typedef struct {
    char type[10];
    unsigned char *raw_content;
    uint64_t raw_size;
    unsigned char *compressed_data;
    ulong compressed_size;
    unsigned char sha1[20];
    char sha1_hex[41];
} GitPayloadObject;

typedef struct {
    char data[BUFFER_SIZE];
    size_t size;
} MemoryBuffer;

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

GitPayloadObject g_objects[MAX_FILES + 5];
int g_obj_count = 0;

void write_be32(unsigned char *buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

size_t discovery_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userp;
    if (mem->size + realsize < BUFFER_SIZE - 1) {
        memcpy(&(mem->data[mem->size]), contents, realsize);
        mem->size += realsize;
        mem->data[mem->size] = 0;
    }
    return realsize;
}

size_t push_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    printf("%.*s", (int)(size * nmemb), (char *)ptr);
    return size * nmemb;
}

void calculate_git_sha1(GitPayloadObject *obj) {
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %lu", obj->type, obj->raw_size);
    header[header_len++] = '\0'; 

    unsigned int md_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, header, header_len);
    EVP_DigestUpdate(ctx, obj->raw_content, obj->raw_size);
    EVP_DigestFinal_ex(ctx, obj->sha1, &md_len);
    EVP_MD_CTX_free(ctx);

    for (int i = 0; i < 20; i++) {
        sprintf(&obj->sha1_hex[i * 2], "%02x", obj->sha1[i]);
    }
    obj->sha1_hex[40] = '\0';
}

void compress_object(GitPayloadObject *obj) {
    ulong bound = compressBound(obj->raw_size);
    obj->compressed_data = malloc(bound);
    if (compress(obj->compressed_data, &bound, obj->raw_content, obj->raw_size) == Z_OK) {
        obj->compressed_size = bound;
    }
}

int write_pack_varint(unsigned char *buf, int type, uint64_t size) {
    int pos = 0;
    unsigned char c = (type << 4) | (size & 15);
    size >>= 4;
    while (size > 0) {
        buf[pos++] = c | 0x80;
        c = size & 0x7F;
        size >>= 7;
    }
    buf[pos++] = c;
    return pos;
}

int compare_entries(const void *a, const void *b) {
    FileEntry *fa = (FileEntry *)a;
    FileEntry *fb = (FileEntry *)b;
    return strcmp(fa->name, fb->name);
}

void trim_input(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') str[len - 1] = '\0';
}


int push() {
    char repo[1024] = "https://github.com/oliver-ratta/os";
    char username[256], token[256], commit_msg[512];
    char old_hash[41] = {0};


    
    printf("Enter GitHub Username: ");
    if (fgets(username, sizeof(username), stdin)) trim_input(username);
    printf("Enter Personal Access Token (PAT): ");
    if (fgets(token, sizeof(token), stdin)) trim_input(token);
    printf("Enter Commit Message: ");
    if (fgets(commit_msg, sizeof(commit_msg), stdin)) trim_input(commit_msg);


    CURL *curl;
    CURLcode res;
    char auth_string[1024], refs_url[2048];
    MemoryBuffer discovery_buf = { .size = 0 };

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) return 1;

    snprintf(auth_string, sizeof(auth_string), "%s:%s", username, token);
    snprintf(refs_url, sizeof(refs_url), "%s/info/refs?service=git-receive-pack", repo);

    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth_string);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "git/1.9.1");
    curl_easy_setopt(curl, CURLOPT_URL, refs_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discovery_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&discovery_buf);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("Discovery Network Fail: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return 1;
    }

    char *target_ref = strstr(discovery_buf.data, "refs/heads/main");
    if (!target_ref) target_ref = strstr(discovery_buf.data, "refs/heads/master");

    if (target_ref) {
        char *hash_start = target_ref - 41;
        if (hash_start[0] == ' ' || hash_start[0] == '\n') hash_start++;
        memcpy(old_hash, hash_start, 40);
        old_hash[40] = '\0';
    } else {
        printf("Error: Could not parse remote branch tracker.\n");
        curl_easy_cleanup(curl);
        return 1;
    }

    FileEntry root_entries[MAX_FILES];
    int root_entry_count = 0;
    
    FileEntry sub_entries[MAX_FILES];
    int sub_entry_count = 0;

    DIR *d = opendir(".");
    struct dirent *dir;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 || 
                strcmp(dir->d_name, ".git") == 0 || strcmp(dir->d_name, "pusher") == 0 || 
                strcmp(dir->d_name, "pusher.c") == 0) {
                continue;
            }

            struct stat st;
            stat(dir->d_name, &st);

            if (S_ISDIR(st.st_mode)) {
                if (strcmp(dir->d_name, "cloned_repo") == 0) {
                    DIR *sub_d = opendir("cloned_repo");
                    struct dirent *sub_dir;
                    if (sub_d) {
                        while ((sub_dir = readdir(sub_d)) != NULL) {
                            if (strcmp(sub_dir->d_name, ".") == 0 || strcmp(sub_dir->d_name, "..") == 0) continue;
                            
                            char full_path[512];
                            snprintf(full_path, sizeof(full_path), "cloned_repo/%s", sub_dir->d_name);
                            
                            FILE *f = fopen(full_path, "rb");
                            if (f) {
                                fseek(f, 0, SEEK_END);
                                long fsize = ftell(f);
                                fseek(f, 0, SEEK_SET);

                                GitPayloadObject *obj = &g_objects[g_obj_count++];
                                strcpy(obj->type, "blob");
                                obj->raw_size = fsize;
                                obj->raw_content = malloc(fsize);
                                fread(obj->raw_content, 1, fsize, f);
                                fclose(f);

                                calculate_git_sha1(obj);
                                compress_object(obj);

                                strcpy(sub_entries[sub_entry_count].name, sub_dir->d_name);
                                strcpy(sub_entries[sub_entry_count].mode, "100644");
                                memcpy(sub_entries[sub_entry_count].sha1, obj->sha1, 20);
                                sub_entries[sub_entry_count].is_dir = 0;
                                sub_entry_count++;
                            }
                        }
                        closedir(sub_d);
                    }
                    strcpy(root_entries[root_entry_count].name, "cloned_repo");
                    strcpy(root_entries[root_entry_count].mode, "40000");
                    root_entries[root_entry_count].is_dir = 1;
                    root_entry_count++;
                }
            } else {
                FILE *f = fopen(dir->d_name, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fseek(f, 0, SEEK_SET);

                    GitPayloadObject *obj = &g_objects[g_obj_count++];
                    strcpy(obj->type, "blob");
                    obj->raw_size = fsize;
                    obj->raw_content = malloc(fsize);
                    fread(obj->raw_content, 1, fsize, f);
                    fclose(f);

                    calculate_git_sha1(obj);
                    compress_object(obj);

                    strcpy(root_entries[root_entry_count].name, dir->d_name);
                    strcpy(root_entries[root_entry_count].mode, "100644");
                    memcpy(root_entries[root_entry_count].sha1, obj->sha1, 20);
                    root_entries[root_entry_count].is_dir = 0;
                    root_entry_count++;
                }
            }
        }
        closedir(d);
    }git-clone

    GitPayloadObject sub_tree_obj;
    if (sub_entry_count > 0) {
        qsort(sub_entries, sub_entry_count, sizeof(FileEntry), compare_entries);
        unsigned char sub_raw[4096];
        int sub_len = 0;
        for (int i = 0; i < sub_entry_count; i++) {
            int l = snprintf((char*)sub_raw + sub_len, sizeof(sub_raw) - sub_len, "%s %s", sub_entries[i].mode, sub_entries[i].name);
            sub_len += l; sub_raw[sub_len++] = '\0';
            memcpy(sub_raw + sub_len, sub_entries[i].sha1, 20); sub_len += 20;
        }
        strcpy(sub_tree_obj.type, "tree");
        sub_tree_obj.raw_size = sub_len;
        sub_tree_obj.raw_content = malloc(sub_len);
        memcpy(sub_tree_obj.raw_content, sub_raw, sub_len);
        calculate_git_sha1(&sub_tree_obj);
        compress_object(&sub_tree_obj);
        
        for (int i = 0; i < root_entry_count; i++) {
            if (strcmp(root_entries[i].name, "cloned_repo") == 0) {
                memcpy(root_entries[i].sha1, sub_tree_obj.sha1, 20);
                break;
            }
        }
    }

    qsort(root_entries, root_entry_count, sizeof(FileEntry), compare_entries);
    unsigned char root_raw[8192];
    int root_len = 0;
    for (int i = 0; i < root_entry_count; i++) {
        int l = snprintf((char*)root_raw + root_len, sizeof(root_raw) - root_len, "%s %s", root_entries[i].mode, root_entries[i].name);
        root_len += l; root_raw[root_len++] = '\0';
        memcpy(root_raw + root_len, root_entries[i].sha1, 20); root_len += 20;
    }

    GitPayloadObject root_tree_obj;
    strcpy(root_tree_obj.type, "tree");
    root_tree_obj.raw_size = root_len;
    root_tree_obj.raw_content = malloc(root_len);
    memcpy(root_tree_obj.raw_content, root_raw, root_len);
    calculate_git_sha1(&root_tree_obj);
    compress_object(&root_tree_obj);

    unsigned char commit_raw[1024];
    long now = (long)time(NULL);
    int commit_len = snprintf((char*)commit_raw, sizeof(commit_raw),
                               "tree %s\nparent %s\nauthor %s <%s@gmail.com> %ld -0400\ncommitter %s <%s@gmail.com> %ld -0400\n\n%s\n",
                               root_tree_obj.sha1_hex, old_hash, username, username, now, username, username, now, commit_msg);

    GitPayloadObject commit_obj;
    strcpy(commit_obj.type, "commit"); 
    commit_obj.raw_size = commit_len;
    commit_obj.raw_content = malloc(commit_len);
    memcpy(commit_obj.raw_content, commit_raw, commit_len);
    calculate_git_sha1(&commit_obj);
    compress_object(&commit_obj);

    unsigned char *pack_stream = malloc(BUFFER_SIZE);
    int stream_pos = 0;

    int total_objects = g_obj_count + 1 + (sub_entry_count > 0 ? 1 : 0);
    int object_count_header = total_objects + 1; 

    memcpy(pack_stream, "PACK", 4);
    write_be32(pack_stream + 4, 2);  
    write_be32(pack_stream + 8, object_count_header);  
    stream_pos += 12;

    for (int i = 0; i < g_obj_count; i++) {
        stream_pos += write_pack_varint(pack_stream + stream_pos, 3, g_objects[i].raw_size);
        memcpy(pack_stream + stream_pos, g_objects[i].compressed_data, g_objects[i].compressed_size);
        stream_pos += g_objects[i].compressed_size;
    }

    if (sub_entry_count > 0) {
        stream_pos += write_pack_varint(pack_stream + stream_pos, 2, sub_tree_obj.raw_size);
        memcpy(pack_stream + stream_pos, sub_tree_obj.compressed_data, sub_tree_obj.compressed_size);
        stream_pos += sub_tree_obj.compressed_size;
    }

    stream_pos += write_pack_varint(pack_stream + stream_pos, 2, root_tree_obj.raw_size);
    memcpy(pack_stream + stream_pos, root_tree_obj.compressed_data, root_tree_obj.compressed_size);
    stream_pos += root_tree_obj.compressed_size;

    stream_pos += write_pack_varint(pack_stream + stream_pos, 1, commit_obj.raw_size);
    memcpy(pack_stream + stream_pos, commit_obj.compressed_data, commit_obj.compressed_size);
    stream_pos += commit_obj.compressed_size;

    unsigned char pack_sha1[20];
    unsigned int pack_sha1_len = 0;
    EVP_MD_CTX *pack_ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(pack_ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(pack_ctx, pack_stream, stream_pos);
    EVP_DigestFinal_ex(pack_ctx, pack_sha1, &pack_sha1_len);
    EVP_MD_CTX_free(pack_ctx);

    memcpy(pack_stream + stream_pos, pack_sha1, 20);
    stream_pos += 20;

    char push_url[2048];
    snprintf(push_url, sizeof(push_url), "%s/git-receive-pack", repo);

    curl_easy_reset(curl); 
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERPWD, auth_string);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "git/1.9.1");
    curl_easy_setopt(curl, CURLOPT_URL, push_url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-git-receive-pack-request");
    headers = curl_slist_append(headers, "Accept: application/x-git-receive-pack-result");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    char pkt_body[512];
    int body_len = snprintf(pkt_body, sizeof(pkt_body), "%s %s refs/heads/main report-status\n", old_hash, commit_obj.sha1_hex);
    
    char *ref_ptr = strstr(pkt_body, "refs/heads/main");
    if (ref_ptr) ref_ptr[15] = '\0'; 

    int total_pkt_len = body_len + 4;
    char pkt_header[5];
    snprintf(pkt_header, sizeof(pkt_header), "%04x", total_pkt_len);

    int pkt_len = 4 + body_len + 4;
    char *pkt_line = malloc(pkt_len);
    memcpy(pkt_line, pkt_header, 4);
    memcpy(pkt_line + 4, pkt_body, body_len);
    memcpy(pkt_line + 4 + body_len, "0000", 4); 

    int total_send_len = pkt_len + stream_pos;
    unsigned char *final_payload = malloc(total_send_len);
    memcpy(final_payload, pkt_line, pkt_len);
    memcpy(final_payload + pkt_len, pack_stream, stream_pos);

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (char *)final_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)total_send_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, push_callback);

    res = curl_easy_perform(curl);

    free(pkt_line); free(final_payload);
    for(int i=0; i<g_obj_count; i++) { free(g_objects[i].raw_content); free(g_objects[i].compressed_data); }
    if(sub_entry_count > 0) { free(sub_tree_obj.raw_content); free(sub_tree_obj.compressed_data); }
    free(root_tree_obj.raw_content); free(root_tree_obj.compressed_data);
    free(commit_obj.raw_content); free(commit_obj.compressed_data);
    free(pack_stream);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}














































































































int unpack() {
        FILE *pack = fopen("incoming.pack", "rb");
    if (!pack) {
        printf("Error: 'incoming.pack' not found. Run the fetch network step first.\n");
        return 1;
    }

    
    long pack_start_offset = -1;
    unsigned char ch;

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

    
    for (uint32_t i = 1; i <= total_objects; i++) {
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
            }
        }
    }

    for (uint32_t i = 1; i <= total_objects; i++) {
        if (objects[i].data) free(objects[i].data);
    }
    free(objects);
    free(out_buf);
    fclose(pack);
    
    
    return 0;
}












void upload_to_github(const char *repo_url, const char *token) {
    CURL *curl;
    CURLcode res;
    char full_url[2048];
    struct curl_slist *headers = NULL;

    if (repo_url[0] == '\0' || token[0] == '\0') {
        
        return;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        snprintf(full_url, sizeof(full_url), "%s/info/refs?service=git-receive-pack", repo_url);
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        curl_easy_setopt(curl, CURLOPT_USERPWD, token);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return;
        }

        snprintf(full_url, sizeof(full_url), "%s/git-receive-pack", repo_url);
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        headers = curl_slist_append(headers, "Content-Type: application/x-git-receive-pack-request");
        headers = curl_slist_append(headers, "Accept: application/x-git-receive-pack-result");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        const char *mock_git_pkt_line = "0000"; 
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, mock_git_pkt_line);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(mock_git_pkt_line));
        res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

int create_dir(const char *path) {
    #ifdef _WIN32
        return mkdir(path);
    #else
        return mkdir(path, 0777);
    #endif
}

size_t protocol_v1_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    FILE *fp = (FILE *)stream;
    return fwrite(ptr, size, nmemb, fp);
}

void pull(const char *repo_url, const char *destination) {
    CURL *curl;
    CURLcode res;
    char full_url[2048];
    const char *commit_hash = "cff7e863a5b46a89f7e7f8743aa2c7c11bb0be6b";

    if (repo_url[0] == '\0') {
        
        return;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        snprintf(full_url, sizeof(full_url), "%s/git-upload-pack", repo_url);
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "git/1.9.1");
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/x-git-upload-pack-request");
        headers = curl_slist_append(headers, "Accept: application/x-git-upload-pack-result");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        char post_body[256];
        int body_len = snprintf(post_body, sizeof(post_body),
                                "0032want %s\n"
                                "0000"
                                "0009done\n", 
                                commit_hash);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        FILE *fp = fopen("incoming.pack", "wb");
        if (fp == NULL) {
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, protocol_v1_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        res = curl_easy_perform(curl);
        fclose(fp);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) {
            
            curl_global_cleanup();
            return;
        }

        
        FILE *pack_reader = fopen("incoming.pack", "rb");
        if (pack_reader) {
            unsigned char buffer[4096];
            size_t bytes_read = fread(buffer, 1, sizeof(buffer), pack_reader);
            fclose(pack_reader);
            int found_offset = -1;
            if (bytes_read >= 4) {
                for (size_t i = 0; i <= bytes_read - 4; i++) {
                    if (buffer[i] == 'P' && buffer[i+1] == 'A' && buffer[i+2] == 'C' && buffer[i+3] == 'K') {
                        found_offset = (int)i;
                        break;
                    }
                }
            }
            if (found_offset != -1) {
                
            }
        }
    }
    curl_global_cleanup();
}

void clone() {
    
    printf("Enter repository URL: ");
    fgets(repo_url, sizeof(repo_url), stdin);
    trim_line(repo_url);
    printf("Enter destination folder (leave blank for current directory): ");
    fgets(username, sizeof(username), stdin);
    trim_line(username);
    pull(repo_url, username);
}

int read_log() {
    char *home = getenv("HOME");
    if (home == NULL) {
        return 1;
    }
    char full_path[512]; 
    snprintf(full_path, sizeof(full_path), "%s/.local/share/rollback/gitlog.txt", home);
    FILE *fptr = fopen(full_path, "r");
    if (fptr == NULL) {
        return 1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), fptr)) {
        printf("%s", line);
    }
    fclose(fptr);
    return 1;
}

void get_cwd() {
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strcpy(pd, cwd);
    } else {
        
    }
}

void tag() {
    printf("Tagged\n");
}

void update() {
    
}

void upload() {
    
}

int track() {
    
    DIR *dir;
    struct dirent *entry;
    char *home = getenv("HOME");
    if (home == NULL) {
        return 1;
    }
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/.local/share/rollback", home);
    dir = opendir(full_path);
    if (dir == NULL) {
        return 1;
    }
    while ((entry = readdir(dir)) != NULL) {
    }
    closedir(dir);
    return 1;
}

int logg(const char *logstr) {
    FILE *fptr = fopen("/home/oliver/.local/share/rollback/gitlog.txt", "a");
    if (fptr == NULL) {
        return 0;
    }
    fprintf(fptr, "%s\n", logstr);
    fclose(fptr);
    return 1; 
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("uses: \n clone \n tag \n update \n")
        
        return 1;
    }
    system("mkdir -p ~/.local/share/rollback");
    track();
    get_cwd();
    time_t t;
    struct tm *tm_info;
    char buffer[20];
    time(&t);
    tm_info = localtime(&t);
    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", tm_info);

    if (strcmp(argv[1], "tag") == 0) {
        char *user_input = argv[2];
        printf("Tagging: %s\n", user_input);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, user_input, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, buffer, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, username, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, useremail, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "\n", sizeof(logstr) - strlen(logstr) - 1);
        tag();
        logg(logstr);
    } else if (strcmp(argv[1], "update") == 0) {
        if (argc > 2 && strcmp(argv[2], "main") == 0) {
            
        } else if (argc > 2 && strcmp(argv[2], "dev") == 0) {
            
        }
    } else if (strcmp(argv[1], "upload") == 0) {
        push();

    } else if (strcmp(argv[1], "track") == 0) {
        
    } else if (strcmp(argv[1], "log") == 0) {
        read_log();
    } else if (strcmp(argv[1], "clone") == 0) {
        clone();
        strncat(logstr, "[cloned repo]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, buffer, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, username, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "[", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, useremail, sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "]", sizeof(logstr) - strlen(logstr) - 1);
        strncat(logstr, "\n", sizeof(logstr) - strlen(logstr) - 1);
        tag();
        logg(logstr);
        unpack();
    } else {
        
    }
    return 0;
}
