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

// Globals to keep tracking clean
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

// Git strict alpha sorting algorithm
int compare_entries(const void *a, const void *b) {
    FileEntry *fa = (FileEntry *)a;
    FileEntry *fb = (FileEntry *)b;
    return strcmp(fa->name, fb->name);
}

void trim_input(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') str[len - 1] = '\0';
}

int main() {
    char repo[1024] = "https://github.com/oliver-ratta/os";
    char username[256], token[256], commit_msg[512];
    char old_hash[41] = {0};

    printf("====================================================\n");
    printf(" 👑 AUTONOMOUS ENGINE: MULTI-FILE WORKSPACE PUSHER 👑 \n");
    printf("====================================================\n");
    
    printf("Enter GitHub Username: ");
    if (fgets(username, sizeof(username), stdin)) trim_input(username);
    printf("Enter Personal Access Token (PAT): ");
    if (fgets(token, sizeof(token), stdin)) trim_input(token);
    printf("Enter Commit Message: ");
    if (fgets(commit_msg, sizeof(commit_msg), stdin)) trim_input(commit_msg);

    // ========================================================
    // PHASE 1: QUERY HEAD
    // ========================================================
    printf("\n[1/5] Querying GitHub for live branch head pointer...\n");
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
        printf(" -> Found live remote head hash: %s\n", old_hash);
    } else {
        printf("Error: Could not parse remote branch tracker.\n");
        curl_easy_cleanup(curl);
        return 1;
    }

    // ========================================================
    // PHASE 2: CRAWL LOCAL DIRECTORY AUTOMATICALLY
    // ========================================================
    printf("[2/5] Crawling local directory files dynamically...\n");
    FileEntry root_entries[MAX_FILES];
    int root_entry_count = 0;
    
    FileEntry sub_entries[MAX_FILES];
    int sub_entry_count = 0;

    DIR *d = opendir(".");
    struct dirent *dir;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            // Ignore Git trackers and self-executable outputs
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 || 
                strcmp(dir->d_name, ".git") == 0 || strcmp(dir->d_name, "pusher") == 0 || 
                strcmp(dir->d_name, "pusher.c") == 0) {
                continue;
            }

            struct stat st;
            stat(dir->d_name, &st);

            if (S_ISDIR(st.st_mode)) {
                // Handle the subfolder "cloned_repo" dynamically
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
                    // Mark the folder entry for the root tree compilation later
                    strcpy(root_entries[root_entry_count].name, "cloned_repo");
                    strcpy(root_entries[root_entry_count].mode, "40000");
                    root_entries[root_entry_count].is_dir = 1;
                    root_entry_count++;
                }
            } else {
                // Top-level files (README.md, LICENSE, boot.asm, etc.)
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
    }

    // ========================================================
    // PHASE 3: BUILD BINARY TREES WITH STRICT ALPHABETICAL SORTING
    // ========================================================
    printf("[3/5] Compiling and ordering cryptographic directory trees...\n");
    
    // 1. Build Subfolder Tree if cloned_repo has contents
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
        
        // Inject the freshly built subfolder hash into the root entries array
        for (int i = 0; i < root_entry_count; i++) {
            if (strcmp(root_entries[i].name, "cloned_repo") == 0) {
                memcpy(root_entries[i].sha1, sub_tree_obj.sha1, 20);
                break;
            }
        }
    }

    // 2. Build Root Tree (Sorting all detected top-level files dynamically)
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

    // 3. Build Commit Object
    printf("[4/5] Finalizing timeline commit block signatures...\n");
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

    // ========================================================
    // PHASE 4: PACKSTREAM ASSEMBLY
    // ========================================================
    unsigned char *pack_stream = malloc(BUFFER_SIZE);
    int stream_pos = 0;

    int total_objects = g_obj_count + 1 + (sub_entry_count > 0 ? 1 : 0); // Blobs + Root Tree + Optional Sub Tree + Commit
    int object_count_header = total_objects + 1; 

    memcpy(pack_stream, "PACK", 4);
    write_be32(pack_stream + 4, 2);  
    write_be32(pack_stream + 8, object_count_header);  
    stream_pos += 12;

    // Pack all workspace blobs dynamically
    for (int i = 0; i < g_obj_count; i++) {
        stream_pos += write_pack_varint(pack_stream + stream_pos, 3, g_objects[i].raw_size);
        memcpy(pack_stream + stream_pos, g_objects[i].compressed_data, g_objects[i].compressed_size);
        stream_pos += g_objects[i].compressed_size;
    }

    // Pack Subfolder Tree if created
    if (sub_entry_count > 0) {
        stream_pos += write_pack_varint(pack_stream + stream_pos, 2, sub_tree_obj.raw_size);
        memcpy(pack_stream + stream_pos, sub_tree_obj.compressed_data, sub_tree_obj.compressed_size);
        stream_pos += sub_tree_obj.compressed_size;
    }

    // Pack Root Tree
    stream_pos += write_pack_varint(pack_stream + stream_pos, 2, root_tree_obj.raw_size);
    memcpy(pack_stream + stream_pos, root_tree_obj.compressed_data, root_tree_obj.compressed_size);
    stream_pos += root_tree_obj.compressed_size;

    // Pack Commit Object
    stream_pos += write_pack_varint(pack_stream + stream_pos, 1, commit_obj.raw_size);
    memcpy(pack_stream + stream_pos, commit_obj.compressed_data, commit_obj.compressed_size);
    stream_pos += commit_obj.compressed_size;

    // Compute packfile validation checksum footer
    unsigned char pack_sha1[20];
    unsigned int pack_sha1_len = 0;
    EVP_MD_CTX *pack_ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(pack_ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(pack_ctx, pack_stream, stream_pos);
    EVP_DigestFinal_ex(pack_ctx, pack_sha1, &pack_sha1_len);
    EVP_MD_CTX_free(pack_ctx);

    memcpy(pack_stream + stream_pos, pack_sha1, 20);
    stream_pos += 20;

    // ========================================================
    // PHASE 5: NET DISPATCH TO GITHUB VIA RECEIVE-PACK
    // ========================================================
    printf("[5/5] Dispatched payload over wire boundaries...\n");
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

    printf("\n--- GitHub Cloud Infrastructure Response Wire Logs ---\n");
    res = curl_easy_perform(curl);
    printf("------------------------------------------------------\n");

    // Comprehensive memory cleanup
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