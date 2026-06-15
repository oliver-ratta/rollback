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
        // No output for success/failure
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
    
    printf("Cloning repository...\n");
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
        // No output
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
        
        printf("Enter GitHub Repository URL (e.g. https://github.com/user/repo): ");
        fgets(repo_url, sizeof(repo_url), stdin);
        trim_line(repo_url);
        printf("Enter GitHub Personal Access Token: ");
        fgets(token, sizeof(token), stdin);
        trim_line(token);
        upload_to_github(repo_url, token);
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
    } else {
        
    }
    return 0;
}