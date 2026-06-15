#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <zlib.h>
#include <stdint.h>
#include <time.h>
#include <openssl/evp.h>

#define BUFFER_SIZE 65536

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

// Strictly hashes uncompressed Git objects: "type size\0content"
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

void trim_input(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') str[len - 1] = '\0';
}

int main() {
    char repo[1024] = "https://github.com/oliver-ratta/os";
    char username[256], token[256], commit_msg[512];
    char old_hash[41] = {0};

    printf("==================================================\n");
    printf(" 👑 FULLY FUNCTIONAL BARE-METAL GIT ENGINE 👑 \n");
    printf("==================================================\n");
    
    printf("Enter GitHub Username: ");
    if (fgets(username, sizeof(username), stdin)) trim_input(username);
    printf("Enter Personal Access Token (PAT): ");
    if (fgets(token, sizeof(token), stdin)) trim_input(token);
    printf("Enter Commit Message: ");
    if (fgets(commit_msg, sizeof(commit_msg), stdin)) trim_input(commit_msg);

    // ========================================================
    // PHASE 1: DYNAMIC REMOTE REFERENCE DISCOVERY
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
    if (!target_ref) {
        target_ref = strstr(discovery_buf.data, "refs/heads/master");
    }

    if (target_ref) {
        char *hash_start = target_ref - 41;
        if (hash_start[0] == ' ' || hash_start[0] == '\n') hash_start++;
        memcpy(old_hash, hash_start, 40);
        old_hash[40] = '\0';
        printf(" -> Found live remote head hash: %s\n", old_hash);
    } else {
        printf("Error: Could not parse remote branch layouts.\n");
        curl_easy_cleanup(curl);
        return 1;
    }

    // ========================================================
    // PHASE 2: GENERATE CRYPTO OBJECTS (BLOB, TREE, COMMIT)
    // ========================================================
    printf("[2/5] Staging file content changes into a BLOB ('git add .')...\n");
    const char *file_content = "#include <math.h>\n// Bare-metal C production code upload run\n";
    uint64_t file_len = strlen(file_content);

    GitPayloadObject blob_obj;
    strcpy(blob_obj.type, "blob");
    blob_obj.raw_size = file_len;
    blob_obj.raw_content = malloc(file_len);
    memcpy(blob_obj.raw_content, file_content, file_len);
    calculate_git_sha1(&blob_obj);
    compress_object(&blob_obj);

    printf("[3/5] Binding workspace topology into directory TREE...\n");
    unsigned char tree_raw[256];
    int tree_len = snprintf((char*)tree_raw, sizeof(tree_raw), "100644 renderer_3d.c");
    tree_raw[tree_len++] = '\0';
    memcpy(tree_raw + tree_len, blob_obj.sha1, 20); 
    tree_len += 20;

    GitPayloadObject tree_obj;
    strcpy(tree_obj.type, "tree");
    tree_obj.raw_size = tree_len;
    tree_obj.raw_content = malloc(tree_len);
    memcpy(tree_obj.raw_content, tree_raw, tree_len);
    calculate_git_sha1(&tree_obj);
    compress_object(&tree_obj);

    printf("[4/5] Staging and signing timeline records ('git commit')...\n");
    unsigned char commit_raw[1024];
    long now = (long)time(NULL);
    int commit_len = snprintf((char*)commit_raw, sizeof(commit_raw),
                               "tree %s\nparent %s\nauthor %s <%s@gmail.com> %ld -0400\ncommitter %s <%s@gmail.com> %ld -0400\n\n%s\n",
                               tree_obj.sha1_hex, old_hash, username, username, now, username, username, now, commit_msg);

    GitPayloadObject commit_obj;
    strcpy(commit_obj.type, "commit"); 
    commit_obj.raw_size = commit_len;
    commit_obj.raw_content = malloc(commit_len);
    memcpy(commit_obj.raw_content, commit_raw, commit_len);
    calculate_git_sha1(&commit_obj);
    compress_object(&commit_obj);
    printf(" -> Target commit signature successfully finalized: %s\n", commit_obj.sha1_hex);

    // ========================================================
    // PHASE 3: COMPILE PACKSTREAM BINARY DATA
    // ========================================================
    unsigned char *pack_stream = malloc(BUFFER_SIZE);
    int stream_pos = 0;

    memcpy(pack_stream, "PACK", 4);
    write_be32(pack_stream + 4, 2);
    write_be32(pack_stream + 8, 3);
    stream_pos += 12;

    // Append Object 1: Blob
    stream_pos += write_pack_varint(pack_stream + stream_pos, 3, blob_obj.raw_size);
    memcpy(pack_stream + stream_pos, blob_obj.compressed_data, blob_obj.compressed_size);
    stream_pos += blob_obj.compressed_size;

    // Append Object 2: Tree
    stream_pos += write_pack_varint(pack_stream + stream_pos, 2, tree_obj.raw_size);
    memcpy(pack_stream + stream_pos, tree_obj.compressed_data, tree_obj.compressed_size);
    stream_pos += tree_obj.compressed_size;

    // Append Object 3: Commit
    stream_pos += write_pack_varint(pack_stream + stream_pos, 1, commit_obj.raw_size);
    memcpy(pack_stream + stream_pos, commit_obj.compressed_data, commit_obj.compressed_size);
    stream_pos += commit_obj.compressed_size;

    // Compute final packfile validation footer over packed bytes using modern OpenSSL 3.0 EVP
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
    // PHASE 4: TRANSMIT DATA FRAME PAYLOAD
    // ========================================================
    printf("[5/5] Packaging wire command frame and executing remote commit transmission...\n");
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

    // Safe multi-step generation of the packet string to prevent C string truncation bugs
    char pkt_body[512];
    int body_len = snprintf(pkt_body, sizeof(pkt_body), "%s %s refs/heads/main report-status\n", old_hash, commit_obj.sha1_hex);
    
    // Manually substitute the space boundary right after the refname with a protocol null byte
    char *ref_ptr = strstr(pkt_body, "refs/heads/main");
    if (ref_ptr) {
        ref_ptr[15] = '\0'; 
    }

    // Compute the dynamic hex-length protocol line header (body size + 4 bytes for the header itself)
    int total_pkt_len = body_len + 4;
    char pkt_header[5];
    snprintf(pkt_header, sizeof(pkt_header), "%04x", total_pkt_len);

    // Build the final complete network packet line block layout
    int pkt_len = 4 + body_len + 4;
    char *pkt_line = malloc(pkt_len);
    memcpy(pkt_line, pkt_header, 4);
    memcpy(pkt_line + 4, pkt_body, body_len);
    memcpy(pkt_line + 4 + body_len, "0000", 4); // Append the flush delimiter packet

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

    if (res != CURLE_OK) printf("Upload Stream Interrupted: %s\n", curl_easy_strerror(res));

    // Cleanup allocations
    free(pkt_line);
    free(final_payload); free(blob_obj.raw_content); free(blob_obj.compressed_data);
    free(tree_obj.raw_content); free(tree_obj.compressed_data);
    free(commit_obj.raw_content); free(commit_obj.compressed_data);
    free(pack_stream);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}