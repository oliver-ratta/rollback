#include <stdio.h>
#include <string.h>




void tag() {
    printf("Tracking changes...\n");
}

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

void log() {
    printf("Displaying commit log...\n");
}



















































int main(int argc, char *argv[]) {




    if (strcmp(argv[1], "tag") == 0) {
        char *user_input = argv[2];
        printf("Tagging: %s\n", user_input);
        tag();
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





