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
// #define DEBUGM

#ifdef DEBUGM
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

struct node
{
    cnd_t wait;
    thrd_t id;
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

int create_new_thread_node(struct node* new, thrd_t id){
    new->next = NULL;
    new->id = id;
    DEBUG_PRINT(("cnd when creating node = %p\n", &new->wait));
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
    if (queue->length == 0) return NULL;
    strcpy(name, queue->first->dir_name);

    /*if there is one node in the queue*/
    if (queue->length == 1){
        queue->last = NULL;
    }

    queue->first = queue->first->next;
    --queue->length;

    return name;
}

int thread_dequeue(struct queue *queue){
    struct node *tmp;
    if (queue->length == 0) return -1;

    /*if there is one node in the queue*/
    if (queue->length == 1){
        queue->last = NULL;
    }

    tmp = queue->first;

    queue->first = queue->first->next;
    --queue->length;

    tmp->next = NULL;

    
    return 0;
}


int isEmpty(struct queue* queue){
    if (queue->first != NULL){
        return 0;
    } 
    return 1;
}

void print_threads_queue(struct queue* queue){
    DEBUG_PRINT(("----------------threads queue-----------------\n"));
    struct node* curr = queue->first;
    if (queue->length == 0) return;
    while (curr != NULL)
    {
        DEBUG_PRINT(("thread id = %ld, cnd add = %p\n", curr->id, &(curr->wait)));
        curr = curr->next;
    }
    return;
}


void print_dir_queue(struct queue* queue){
    DEBUG_PRINT(("----------------dir queue-----------------\n"));
    struct node* curr = queue->first;
    if (queue->length == 0) return;
    while (curr != NULL)
    {
        DEBUG_PRINT(("dir = %s\n", curr->dir_name));
        curr = curr->next;
    }
    return;
}

int NUM_THREADS;
char *file_name;
atomic_int running_threads = 0;
atomic_int files_counter = 0;
int finished = 0;
int failed_flag = 0; /*indicates if one of the threads had error in run time*/
struct queue *dir_queue;
struct queue *threads_queue;

mtx_t qlock;
cnd_t start_searching;
cnd_t first_thread;


int wake_up_all_threads(){
    struct node *curr, *tmp;
    if (threads_queue->length == 0){
        return 0;
    }
    else{
        curr = threads_queue->first;
        while(curr != NULL){
            curr = curr->next;
            tmp = threads_queue->first;
            thread_dequeue(threads_queue);
            cnd_signal(&tmp->wait);
            }
    }
    return 0;
    
}



int search_in_dir(char *dir_name){
    char new_dir_name[PATH_MAX];
    DIR *dir;
    struct dirent* entry;
    struct stat path_stat;
    struct node *tmp;

    DEBUG_PRINT(("~~~~~~~~~~~~~~~~~~~~running thread %ld~~~~~~~~~~~~~~~~~~~~\n", thrd_current()));
    DEBUG_PRINT(("~~~~~~~~~~~~~~~~~~~~~~dir name = %s~~~~~~~~~~~~~~~~~~~~~~~~\n", dir_name));

    dir = opendir(dir_name);

    if (dir == NULL){
        fprintf(stderr, "ERROR: opendir()\n"); /*not suppose to happen since we check access() before adding to queue*/
        failed_flag = 1;
        return 1;
    }


    while((entry = readdir(dir)) != NULL){
        // DEBUG_PRINT(("-------entry_name = %s----------\n", entry->d_name));
        sprintf(new_dir_name, "%s/%s", dir_name, entry->d_name);
        
        if (stat(new_dir_name, &path_stat) != 0) {
            fprintf(stderr, "ERROR in stat()");
            failed_flag = 1;
        };

        if (S_ISDIR(path_stat.st_mode) != 0){
            if (access(new_dir_name, R_OK | X_OK) != 0){
                printf("Directory %s: Permission denied.\n", new_dir_name);
            }
            else{
                if ((strcmp(entry->d_name, ".") != 0) & (strcmp(entry->d_name, "..") !=0)){
                    mtx_lock(&qlock);
                    if (threads_queue->length == 0){
                        struct node* new = (struct node*) malloc(sizeof(struct node));
                        if (new == NULL) {
                            fprintf(stderr, "ERROR: allocation failed.\n");
                            failed_flag = 1;
                            }
                        create_new_dir_node(new, new_dir_name);
                        enqueue(dir_queue, new); /*if there is no thread waiting, put dir in queue*/
                    }
                    else{
                        DEBUG_PRINT(("pass task to waiting thread %ld, dir name = %s\n", threads_queue->first->id, new_dir_name));
                        tmp = threads_queue->first;
                        strcpy(tmp->dir_name, new_dir_name); /*if there is a waiting thread - give the task to him*/
                        thread_dequeue(threads_queue);
                        cnd_signal(&(tmp->wait));
                    }
                    mtx_unlock(&qlock);
                    

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
    char dir_name[PATH_MAX];

    struct node *thread_node = (struct node*) malloc(sizeof(struct node));
    if (thread_node == NULL){
        fprintf(stderr, "ERROR: allocation failed.\n");
        failed_flag = 1;
    }

    create_new_thread_node(thread_node, thrd_current());
    
    mtx_lock(&qlock);
    running_threads++;
    cnd_wait(&start_searching, &qlock);
    mtx_unlock(&qlock);
    

    DEBUG_PRINT(("before while %ld\n", thrd_current()));    
    running_threads++;
    while (1)
    {
        print_threads_queue(threads_queue);
        print_dir_queue(dir_queue);

        mtx_lock(&qlock);
        if (!isEmpty(dir_queue)){
        // if ((NUM_THREADS - threads_queue->length) <= dir_queue->length){
            dir_dequeue(dir_queue, dir_name);
        }
        
        else{
            if ((threads_queue->length == (NUM_THREADS - 1)) & isEmpty(dir_queue)){
                finished = 1;
                wake_up_all_threads();              
                DEBUG_PRINT(("---------finished search ----------\n"));
                cnd_destroy(&thread_node->wait);
                mtx_unlock(&qlock);
                thrd_exit(0);
            }

            enqueue(threads_queue, thread_node);
            DEBUG_PRINT(("add to queue thread %ld\n", thrd_current()));

            cnd_wait(&(thread_node->wait), &qlock);

            if (finished){
                cnd_destroy(&thread_node->wait);
                mtx_unlock(&qlock);
                thrd_exit(0);
            }

            strcpy(dir_name, thread_node->dir_name);
            
        }
      
        mtx_unlock(&qlock);

        
        search_in_dir(dir_name);     
        
    }

}



int main(int argc, char *argv[]){
    int rc, t;

    if (argc != 4){
        fprintf(stderr, "wrong amount of arguments\n");
        exit(1);
    }

    if (access(argv[1], R_OK | X_OK) != 0){
        printf("Directory %s: Permission denied.\n", argv[1]);
        exit(1);
    }


    NUM_THREADS = atoi(argv[3]);
    file_name = argv[2];
    thrd_t thread_id[NUM_THREADS];


    dir_queue = (struct queue*) malloc(sizeof(struct queue));
    threads_queue = (struct queue*) malloc(sizeof(struct queue));
    if ((dir_queue == NULL) | (threads_queue == NULL)){
        fprintf(stderr, "Error mem Allocation");
        exit(1);
    }


    create_new_queue(dir_queue);
    create_new_queue(threads_queue);

    mtx_init(&qlock, mtx_plain);
    cnd_init(&start_searching);
    cnd_init(&first_thread);
    
    struct node* new = (struct node*) malloc(sizeof(struct node));
    create_new_dir_node(new, argv[1]);
    if (enqueue(dir_queue, new) != 0){
        fprintf(stderr, "ERROR enqueue\n");
    }


    for (t = 0; t < NUM_THREADS; ++t) {
        rc = thrd_create(&thread_id[t], thread_search, NULL);
        if (rc != thrd_success) {
            fprintf(stderr, "ERROR in thrd_create()\n");
            exit(1);
        }
    }

    
    while(running_threads != NUM_THREADS) {thrd_yield();}

    mtx_lock(&qlock);
    running_threads = 0;
    cnd_broadcast(&start_searching);
    mtx_unlock(&qlock);

    for (t = 0; t < NUM_THREADS; ++t) {
        rc = thrd_join(thread_id[t], NULL);
        if (rc != thrd_success) {
            fprintf(stderr,"ERROR in thrd_join()\n");
            exit(1);
        }
        
    }

    printf("Done searching, found %d files\n", files_counter);


    mtx_destroy(&qlock);
    cnd_destroy(&start_searching);
    cnd_destroy(&first_thread);
    free(dir_queue);
    free(threads_queue);
    exit(failed_flag);


}
