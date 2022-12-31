#include <threads.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>


struct node
{
    cnd_t wait;
    char dir_name[PATH_MAX];
    struct node* next; 
};

struct queue
{
  struct node* first;
  struct node* last;
  int length;
    
};


int create_new_dir_node(struct node* new, char *name){
    strcpy(new->dir_name,name);
    new->next = NULL;
    return 0;
}

int create_new_thread_node(struct node* new){
    new->next = NULL;
    cnd_init(&new->wait);
    return 0;
}

int create_new_queue(struct queue* queue){
    queue->first = NULL;
    queue->last = NULL;
    queue->length = 0;
    return 0;
}

int enqueue(struct queue* queue, struct node* new){
    if (queue->length == 0){
        queue->last = new;
        queue->first = new;
    }
    else{
        queue->last->next = new;
        queue->last = new;
    }
    ++queue->length;
    return 0;
}

char *dir_dequeue(struct queue* queue, char name[]){
    struct node* tmp = queue->first;
    if (queue->length == 0) return NULL;
    strcpy(name, tmp->dir_name);

    /*if there is one node in the queue*/
    if (queue->length == 1){
        queue->last = NULL;
    }

    queue->first = queue->first->next;
    --queue->length;

    free(tmp);
    return name;
}

int thread_dequeue(struct queue *queue){
    struct node* tmp = queue->first;
    if (queue->length == 0) return -1;

    /*if there is one node in the queue*/
    if (queue->length == 1){
        queue->last = NULL;
    }

    queue->first = queue->first->next;
    --queue->length;

    cnd_signal(&tmp->wait);
    cnd_destroy(&tmp->wait);
    free(tmp);
    return 0;
}


int isEmpty(struct queue* queue){
    if (queue->first != NULL){
        return 0;
    } 
    return 1;
}


int num_threads;
char *file_name;
atomic_int running_threads = 0;
atomic_int files_counter = 0;
int finished = 0;
int start_search = 0;
static struct queue *dir_queue;
static struct queue *threads_queue;
mtx_t tlock;

int sync_threads_start(struct node *thread_node){
    running_threads++;
    if (running_threads == num_threads){
        free(thread_node);
        return 0;
    }
    else{
        create_new_thread_node(thread_node);
        enqueue(threads_queue, thread_node);
        cnd_wait(&thread_node->wait, &tlock);
    }
    return 0;
}



int wake_up_all_threads(){
    struct node *curr;
    if (threads_queue->length == 0){
        return 0;
    }
    else{
        curr = threads_queue->first;
        while(curr != NULL){
            curr = curr->next;
            thread_dequeue(threads_queue);
            }
    }
    return 0;
    
}



int search_in_dir(char *dir_name){
    char new_dir_name[PATH_MAX];
    DIR *dir;
    struct dirent* entry;
    struct stat path_stat;

    // printf("~~~~~~~~~~~~~~~~~~~dir name = %s~~~~~~~~~~~~~~~~~~~~~~~~~\n", dir_name);

    dir = opendir(dir_name);

    if (dir == NULL){
        fprintf(stderr, "ERROR: opendir()\n"); /*not suppose to happen*/
        return 1;
    }


    while((entry = readdir(dir)) != NULL){
        // printf("-------entry_name = %s----------\n", entry->d_name);
        sprintf(new_dir_name, "%s/%s", dir_name, entry->d_name);
        
        if (stat(new_dir_name, &path_stat) != 0) {
            fprintf(stderr, "ERROR stat = %d on dir = %s !\n", stat(new_dir_name, &path_stat), new_dir_name);
        };

        if (S_ISDIR(path_stat.st_mode) != 0){
            // printf("directory\n");
            if (access(new_dir_name, R_OK | X_OK) != 0){
                printf("Directory %s: Permission denied.\n", new_dir_name);
            }
            else{
                if ((strcmp(entry->d_name, ".") != 0) & (strcmp(entry->d_name, "..") !=0)){
                    mtx_lock(&tlock);
                    struct node* new = (struct node*) malloc(sizeof(struct node));
                    if (new == NULL){
                        fprintf(stderr, "ERROR: allocation failed.\n");
                    }
                    // printf("add dir = %s\n", new_dir_name);
                    create_new_dir_node(new, new_dir_name);
                    enqueue(dir_queue, new);
                    thread_dequeue(threads_queue);
                    mtx_unlock(&tlock);

                }
            }
        }
        else{
            if ((strstr(entry->d_name, file_name) != NULL)){
                printf("%s\n", new_dir_name);
                files_counter++;
            }    
        }
    }    

    closedir(dir);
    return 0;
}



int thread_search(void *arg){
    struct node *thread_node; 
    char dir_name[PATH_MAX];
    
    mtx_lock(&tlock);
    thread_node = (struct node*) malloc(sizeof(struct node));
    sync_threads_start(thread_node);
    mtx_unlock(&tlock);

    // printf("before while %ld\n", thrd_current());

    
    while (1)
    {            
        mtx_lock(&tlock);
        while (isEmpty(dir_queue))
        {
            // printf("number of waiting threads %d, num_threads %d\n", threads_queue->length, num_threads);
            // printf("dir queue length = %d , first = %p \n", dir_queue->length, dir_queue->first);
            if ((threads_queue->length >= (num_threads - 1)) & isEmpty(dir_queue) & !finished){
                wake_up_all_threads();
                finished = 1;
                // printf("---------finished search ----------\n");
            }
            if (finished){
                mtx_unlock(&tlock);
                thrd_exit(0);
            }
            thread_node = (struct node*) malloc(sizeof(struct node));
            create_new_thread_node(thread_node);
            enqueue(threads_queue, thread_node);
            // printf("add to queue thread %ld\n", thrd_current());
            cnd_wait(&thread_node->wait, &tlock);
        }
        
                
        // printf("%ld\n", thrd_current());
        start_search = 1;
        // printf("after dequeue of dir\n");
        dir_dequeue(dir_queue, dir_name);
        mtx_unlock(&tlock);
       

        // printf("waiting threads = %d\n", threads_queue->length);

        search_in_dir(dir_name);
        
        
    }

}






int main(int argc, char *argv[]){
    int status, all_success, rc, t;

    all_success = 0;

    if (argc != 4){
        fprintf(stderr, "wrong amount of arguments\n");
        exit(1);
    }

    if (access(argv[1], R_OK | X_OK) != 0){
        printf("Directory %s: Permission denied.\n", argv[1]);
        exit(1);
    }


    num_threads = atoi(argv[3]);
    file_name = argv[2];
    thrd_t thread_id[num_threads];


    dir_queue = (struct queue*) malloc(sizeof(struct queue));
    threads_queue = (struct queue*) malloc(sizeof(struct queue));
    if ((dir_queue == NULL) | (threads_queue == NULL)){
        fprintf(stderr, "Error mem Allocation");
        exit(1);
    }


    create_new_queue(dir_queue);
    create_new_queue(threads_queue);

    mtx_init(&tlock, mtx_plain);
    
    struct node* new = (struct node*) malloc(sizeof(struct node));
    create_new_dir_node(new, argv[1]);
    if (enqueue(dir_queue, new) != 0){
        fprintf(stderr, "ERROR enqueue\n");
    }


    for (t = 0; t < num_threads; ++t) {
        rc = thrd_create(&thread_id[t], thread_search, NULL);
        if (rc != thrd_success) {
            printf("ERROR in thrd_create()\n");
            exit(1);
        }
    }

    

    for (t = 0; t < num_threads; ++t) {
        rc = thrd_join(thread_id[t], &status);
        if (rc != thrd_success) {
            printf("ERROR in thrd_join()\n");
            exit(1);
        }
        if (status != 0){
            all_success = 1;
        }
    }

    printf("Done searching, found %d files\n", files_counter);


    mtx_destroy(&tlock);
    free(dir_queue);
    free(threads_queue);


    thrd_exit(all_success);


}
