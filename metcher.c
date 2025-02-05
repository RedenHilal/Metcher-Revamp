#include <curl/curl.h>

#include <curl/easy.h>
#include <regex.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ncurses.h>

#define MAX_EVENTS 1

int getStatus(char*);
void replaceSpace(char*);
int fetchRaw(char*,char*,CURL*);
size_t write_callback(void *ptr, size_t size, size_t nmemb, char *data);
int crawlLyric(CURL*, char *);
void killChild();
void formatBuffer(char*);
void iterateLyric(char*,int);
int countRow(char*);

pid_t pid;

int main(void){
    atexit(killChild);
    int pipes[2];
    int statusPipes[2];
    struct epoll_event events[MAX_EVENTS], command_event;
    char buffer[16384],statusBuffer[256], lastStatus[256];
    int availableRow=0,scrollRow=0;

    if (pipe(pipes)<0){
        perror("Error on Pipe\n");
        exit(1);
    }
    pid = fork();

    if(pid <0){
        perror("Fork Failed\n");
        exit(1);
    }
    else if(pid == 0){
        close(pipes[0]);
        dup2(pipes[1],STDOUT_FILENO);
        close(pipes[1]);

        execlp("mpc","mpc","idleloop", "player",NULL);

        perror("Command Process\n");
        exit(1);
    }

    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL * handler = curl_easy_init();

    if(!handler){
        printf("Error curl init\n");
        exit(1);
    }

    int epfd = epoll_create(1);
    command_event.events = EPOLLIN | EPOLLET;
    command_event.data.fd = pipes[0];
    epoll_ctl(epfd,EPOLL_CTL_ADD , pipes[0], &command_event);
    
    while (1) {
        int ready = epoll_wait(epfd, events, MAX_EVENTS, 100);

        for(int i = 0;i<ready;i++){
            int bytes_read = read(events->data.fd,buffer,1024-1);
            if(getStatus(statusBuffer) == 0 ){
                if(strcmp(statusBuffer, lastStatus)==0) continue;
                strcpy(lastStatus, statusBuffer);
                memset(buffer, 0, sizeof(buffer));
                int fetchStatus = fetchRaw(buffer, statusBuffer, handler);
                if(fetchStatus < 0) {
                    printf("Bad Metadata\n");
                    continue;
                }
                if(crawlLyric(handler, buffer) != 0) continue;
                formatBuffer(buffer);
                availableRow = countRow(buffer);
                scrollRow=0;
                iterateLyric(buffer, scrollRow);
                refresh();
                //printf("%s\n",buffer);
            }
            
        }

        int input = getch();

        if (input != ERR){
            if (input == KEY_DOWN){
                int maxRow = getmaxy(stdscr);
                if((availableRow - scrollRow) <= maxRow)continue;
                scrollRow++;
                clear();
                iterateLyric(buffer,scrollRow);
                refresh();
            }
            else if(input == KEY_UP){
                if(scrollRow <= 0)continue;
                scrollRow--;
                clear();
                iterateLyric(buffer,scrollRow);
                refresh();
            }
        }

    }
    


    return 0;
}

int getStatus(char* statusBuffer){
    //printf("tes1\n");
    int pipes[2];
    if(pipe(pipes) < 0) perror("status pipe err\n");
    int pid = fork();
    if(pid<0) perror("status fork err\n");
    else if (pid == 0){
        close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO);
        close(pipes[1]);

        execlp("mpc","mpc","status",NULL);
        perror("Exec status err\n");
    }
    else {
        close(pipes[1]);
        wait(NULL);

        memset(statusBuffer, 0, 256);
        int bytes_read = read(pipes[0], statusBuffer, 256-1);
        int leftover = strcspn(statusBuffer, "\n");
        memset(statusBuffer + leftover, 0, 256- leftover);
        replaceSpace(statusBuffer);
        close(pipes[0]);
        return 0;
    }

    return -1;
}

void replaceSpace(char*statusBuffer){
    char buffer[256];
    int buffer_iter = 0;
    memset(buffer, 0, 256);
    for (int i = 0;statusBuffer[i] != '\0';i++) {
        
        if(statusBuffer[i] != ' '){
            buffer[buffer_iter] = statusBuffer[i];
            buffer_iter++;
        } else {
            strcpy(buffer+buffer_iter, "%20");
            buffer_iter+=3;
        }   
    }
    strcpy(statusBuffer, buffer);
}

int fetchRaw(char *buffer,char*key,CURL *handler){
    char link[256];
    memset(link, 0, 256);
    snprintf(link, 256, "http://localhost:5555/search?q=%s",key );
    CURLcode res;
    curl_easy_setopt(handler, CURLOPT_URL, link);
    curl_easy_setopt(handler, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handler, CURLOPT_WRITEDATA, buffer);

    //printf("tes1\n");
    res = curl_easy_perform(handler);
    curl_easy_reset(handler);
    //printf("tes2\n");
    if(res != CURLE_OK ){
        printf("Failed Curl\n");
        //curl_easy_cleanup(handler);
        //curl_easy_reset(handler);
        return -1; 
    }
    return 0;
}

size_t write_callback(void *ptr, size_t size, size_t nmemb, char *data) {
    size_t total_size = size * nmemb;
    strncat(data, ptr, total_size); 
    return total_size; 
}

int crawlLyric(CURL*handler, char*buffer){
    char log[128];
    char url[128];
    CURLcode res;
    memset(url, 0,128);
    int stop = 0;
    char * temp;
    char * find = strstr(buffer,"search-item");
    if(find == NULL) return -1;
    while(!stop){
        char * front = strstr(find,"href=\"/");
        if (front == NULL) return 1;
        if(strncmp(front + 7, "Genius", 6) != 0){
            stop=1;
            temp = front;
        }
        find = front+1;
    }
    temp += 7;
    int back = strcspn(temp, "\"");
    strncpy(log, temp, back);
    log[back] = '\0';
    snprintf(url, 128, "http://localhost:5555/%s",log);

    curl_easy_setopt(handler, CURLOPT_URL, url);
    curl_easy_setopt(handler, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handler, CURLOPT_WRITEDATA, buffer);

    res = curl_easy_perform(handler);
    if(res!=CURLE_OK){
        curl_easy_reset(handler);
        return -1;
    }
    curl_easy_reset(handler);

    return 0;
}

void formatBuffer(char*buffer){
    char * temp = (char*)malloc(16384);
    memset(temp,0 , 16384);
    char *iter = temp;
    char *start = strstr(buffer, "metadata-info") + 15;
    int abandonFlag=0;
    int length = strlen(buffer);
    while(*start!='\0'){
        if(*start == '<'){
            abandonFlag +=1;
            start++;
            continue;
        } 
        else if(*start == '>'){
            abandonFlag -=1;
            start++;
            continue;
        }
        if(abandonFlag){
            if (*start == '/'){
                *iter = '\n';
                iter ++;
            }
        } 
        else{
            *iter = *start;
            iter++;
        } 
        start++;
    }
    int tempLength = strlen(temp);
    memset(buffer, 0, 16384);
    strncpy(buffer, temp,tempLength);
    buffer[tempLength] = '\0';
    
    free(temp);
}

void iterateLyric(char* buffer, int row){
    char *buffdup = strdup(buffer);
    char * lyricLine = strtok(buffdup,"\n");
    while(row--){
        lyricLine = strtok(NULL, "\n");
    }
    int rowStart = 2;
    while (lyricLine!=NULL) {
        mvprintw(rowStart, 3, "%s\n", lyricLine);
        lyricLine = strtok(NULL, "\n");
        rowStart++;
    }
    free(buffdup);
}

int countRow(char*buffer){
    int i = 0;
    while (*buffer!='\0') {
        i+= *buffer=='\n';
        buffer++;
    }
    return i;
}

void killChild(){
    if(pid > 0){
        kill(pid,SIGTERM);
    }
}