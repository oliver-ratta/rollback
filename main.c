#include <stdio.h>
#include <string.h>
#include <time.h>
#include "setup.h"


char commit_message[] = "";
char log_text[100] = "";

void tag() {
    printf("Tracking changes...\n");
}


void update() {
    printf("Updating repository...\n");
}

void upload() {
    printf("Uploading changes...\n");
}

void track() {
    printf("Tracking changes...\n");
}

int logg(const char *log_text) {



    
    
    
FILE *fptr = fopen("gitlog.txt", "a"); 
if (fptr == NULL) {
    printf("Critical Error: file could not be opened\n");
    return 0;
}

    
    
    fprintf(fptr, "%s\n", log_text);

    fclose(fptr);
    return 1; 

}










int main(int argc, char *argv[]) {
    
    time_t t;
    struct tm *tm_info;
    char buffer[20];

    time(&t);
    tm_info = localtime(&t);


    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", tm_info);
   
    printf("Current time: %s\n", buffer);
    




    if (strcmp(argv[1], "tag") == 0) {
        char *user_input = argv[2];
        printf("Tagging: %s\n", user_input);
        strncat(log_text, "[", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, user_input, sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "]", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "[", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, buffer, sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "]", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "[", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, USER_NAME, sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "]", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "[", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, USER_EMAIL, sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "]", sizeof(log_text) - strlen(log_text) - 1);
        strncat(log_text, "\n", sizeof(log_text) - strlen(log_text) - 1);
        tag();
        logg(log_text);
    }







    else if (strcmp(argv[1], "update") == 0) {
        if (argc > 2 && strcmp(argv[2], "main") == 0) {
            printf("Updating main branch...\n");
        }
        else if (argc > 2 && strcmp(argv[2], "dev") == 0) {
            printf("Updating dev branch...\n");
        }
    }







    
    else if (strcmp(argv[1], "upload") == 0) {
    if (argc > 2 && strcmp(argv[2], "main") == 0) {
        printf("Uploading to main branch...\n");
    }   
    else if (argc > 2 && strcmp(argv[2], "dev") == 0) {
        printf("Uploading to dev branch...\n");
    }
}
   




else if (strcmp(argv[1], "track") == 0) {

    }



else if (strcmp(argv[1], "log") == 0) {

    }
    else {
        printf("Invalid command.\n");
    }



}





