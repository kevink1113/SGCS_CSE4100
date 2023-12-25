/*
 * echoserveri.c - An iterative echo server
 */
/* $begin echoserverimain */
#include "csapp.h"
#include <stdbool.h>


#define MAX_TREE_NODE 10000
#define SELL 0
#define BUY 1
#define NTHREADS 10
#define SBUFSIZE 600


#define FOR(i, n) for(int i=0; i<n; i++)
#define FOR1(i, n) for(int i=1; i<=n; i++)
#define DIFF(A, B) if ((A) != (B))
#define SAME(A, B) if ((A) == (B))

/* Stock Information Binary Tree */
typedef struct {
    int ID;                         // stock ID
    int left_stock;                 // left stock amount
    int price;                      // stock price
    int readcnt;                    // reader count
    sem_t mutex;                    // mutex
} ITEM;

/* Tree node */
typedef ITEM *TREE;
TREE tree[MAX_TREE_NODE];
int tree_cnt = 0;

/* Represents a pool of connected descriptors */
typedef struct pool {
    int maxfd;                      // Largest descriptor in read_set
    fd_set read_set;                // Set of all active descriptors
    fd_set ready_set;               // Subset of descriptors ready for reading
    int nready;                     // Number of ready descriptors from select
    int maxi;                       // High water index into client array
    int clientfd[FD_SETSIZE];       // Set of active descriptors
    rio_t clientrio[FD_SETSIZE];    // Set of active read buffers
} pool;

typedef struct {
    int *buf;                       // Buffer array
    int n;                          // Maximum number of slots
    int front;                      // buf[(front+1)%n] is first item
    int rear;                       // buf[rear%n] is last item
    sem_t mutex;                    // Protects accesses to buf
    sem_t slots;                    // Counts available slots
    sem_t items;                    // Counts available items
} sbuf_t;

sbuf_t sbuf;                        // Shared buffer

// TODO
static sem_t sem, mutex, w;
int num_conn = 0;

void sbuf_init(sbuf_t *sp, int n) { // Create an empty, bounded, shared FIFO buffer with n slots
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;                                                /* Buffer holds max of n items */
    sp->front = sp->rear = 0;                                 /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1);         /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n);         /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0);         /* Initially, buf has 0 items */
}

void sbuf_deinit(sbuf_t *sp) { // Clean up buffer sp
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item) { // Insert item onto the rear of shared buffer sp
    P(&sp->slots);                                /* Wait for available slot */
    P(&sp->mutex);                                /* Lock the buffer */
    sp->buf[(++sp->rear) % (sp->n)] = item;                /* Insert the item */
    V(&sp->mutex);                                /* Unlock the buffer */
    V(&sp->items);                                /* Announce available item */
}

int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);                                /* Wait for available item */
    P(&sp->mutex);                                /* Lock the buffer */
    int ret = sp->buf[(++sp->front) % (sp->n)];         /* Remove the item */
    V(&sp->mutex);                                /* Unlock the buffer */
    V(&sp->slots);                                /* Announce available slot */
    return ret;
}


/**
 * @brief           Update the node to the file
 */
void write_tree_file() {
    SAME(tree_cnt, 0) return;  // exception
    FILE *fp = Fopen("./stock.txt", "w");
    FOR(i, MAX_TREE_NODE) {
        if (tree[i] == NULL) continue;
        fprintf(fp, "%d %d %d\n", (*tree[i]).ID, (*tree[i]).left_stock, (*tree[i]).price);
    }

    Fclose(fp);
}

void insert_in_tree(ITEM **tree, int start, int end, ITEM *new_node) {
    if (start > end) return;

    int mid = start + (end - start) / 2;

    if (tree[mid] == NULL) {
        tree[mid] = new_node;
        // printf("inserted %d in %d\n", new_node->ID, mid);
        return;
    }

    if (new_node->ID < tree[mid]->ID)
        insert_in_tree(tree, start, mid - 1, new_node);
    else
        insert_in_tree(tree, mid + 1, end, new_node);
}


/**
 * @brief           Load the data from the file
 * @param file_name The name of the file
 */
void load_data(const char *file_name) {
    ITEM *new_node = NULL;
    int id, left, price;
    tree_cnt = 0;

    FILE *fp = Fopen(file_name, "r");

    while (1) {
        if (fscanf(fp, "%d %d %d", &id, &left, &price) < 0) break;

        new_node = (ITEM *) Malloc(sizeof(ITEM));

        new_node->readcnt = 0;
        new_node->left_stock = left;
        new_node->price = price;
        new_node->ID = id;

        Sem_init(&new_node->mutex, 0, 1); // TODO

        insert_in_tree(tree, 0, MAX_TREE_NODE - 1, new_node);
        tree_cnt++;
    }
    Fclose(fp);
}

/**
 * @brief           Show the stock information
 * @param string    The string to be shown
 */
void show(char *string) {
    char show_str[MAXLINE] = {'\0'};
    FOR(i, MAX_TREE_NODE) {
        char str[MAXLINE];
        if (tree[i] == NULL) continue;

        P(&(*tree[i]).mutex);
        (*tree[i]).readcnt++;
        if ((*tree[i]).readcnt == 1) P(&w);
        V(&(*tree[i]).mutex);

        sprintf(str, "%d %d %d\n", (*tree[i]).ID, (*tree[i]).left_stock, (*tree[i]).price);
        strcat(show_str, str);

        P(&(*tree[i]).mutex);
        (*tree[i]).readcnt--;
        if ((*tree[i]).readcnt == 0) V(&w);
        V(&(*tree[i]).mutex);

    }
    strcpy(string, show_str);
}

/**
 * @brief           Traverse the tree and update the stock
 * @param start     start index
 * @param end       end index
 * @param ID        ID of stock
 * @param num       number of stock
 * @param found     whether found
 * @param b_s       buy or sell
 */
void traverse_update(int start, int end, int ID, int num, bool *found, bool b_s) {
    if (start > end) return;

    int mid = start + (end - start) / 2;
    // printf("mid = %d\n", mid);

    if (tree[mid] == NULL) return;

    if (tree[mid]->ID == ID) {
        P(&(*tree[mid]).mutex);


        if (b_s) {
            if (tree[mid]->left_stock >= num) {
                tree[mid]->left_stock -= num;
                *found = true;
            }
        } else {
            tree[mid]->left_stock += num;
            *found = true;
        }
        (*tree[mid]).readcnt++;


        V(&(*tree[mid]).mutex);
    }

    if (*found) return;

    if (ID < tree[mid]->ID)
        traverse_update(start, mid - 1, ID, num, found, b_s);  // left subtree
    else
        traverse_update(mid + 1, end, ID, num, found, b_s);  // right subtree
}

/**
 * @brief           buy 5 3: “ID가 5인 주식을 3개 사겠다.”
 * @param ID        The ID of the stock
 * @param num       The number of the stock
 * @return          buy command is valid or not
 */
bool buy(int ID, int num) {
    bool found = false;
    traverse_update(0, MAX_TREE_NODE - 1, ID, num, &found, BUY);
    return found;
}

/**
 * @brief           sell 2 2: “ID가 2인 주식을 2개 팔겠다.”
 * @param ID        The ID of the stock
 * @param num       The number of the stock
 */
void sell(int ID, int num) {
    bool found = false;
    traverse_update(0, MAX_TREE_NODE - 1, ID, num, &found, SELL);
}


static void init_echo_cnt(void) {
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
}

void echo_cnt(int connfd) {
    char buf[MAXLINE];
    char string[MAXLINE];
    rio_t rio;
    static pthread_once_t once = PTHREAD_ONCE_INIT;

    Pthread_once(&once, init_echo_cnt);
    Rio_readinitb(&rio, connfd);
    while (Rio_readlineb(&rio, buf, MAXLINE) != 0) {
        printf("server received %d bytes\n", strlen(buf));

        char *command = strtok(buf, " ");
        int ID, num;

        if (!strcmp(command, "show\n")) {
            show(string);
        } else if (!strcmp(command, "sell")) {
            ID = atoi(strtok(NULL, " "));
            num = atoi(strtok(NULL, " "));
            sell(ID, num);
            strcpy(string, "[sell] success\n");
        } else if (!strcmp(command, "buy")) {
            ID = atoi(strtok(NULL, " "));
            num = atoi(strtok(NULL, " "));
            snprintf(string, sizeof(string), buy(ID, num) ? "[buy] success\n" : "Not enough left stocks\n");
        } else if (!strcmp(command, "exit\n")) {
            write_tree_file();
            break;
        }
        Rio_writen(connfd, string, MAXLINE);
        snprintf(string, sizeof(string), "");
    }

    write_tree_file();
}


void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        echo_cnt(connfd);   // 강의자료 p.28 참고
        P(&sem);
        Close(connfd);
        num_conn--;
        V(&sem);
        if (!num_conn) {
            write_tree_file();
            // fprintf(stdout, "All clients are disconnected\n");
            return NULL;
        }
    }
}


int main(int argc, char **argv) {
    TREE *tree_pointer = NULL;
    DIFF(argc, 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    load_data("stock.txt");

    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    FOR(i, NTHREADS) Pthread_create(&tid, NULL, thread, NULL);

    Sem_init(&sem, 0, 1);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);

        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        P(&sem);
        sbuf_insert(&sbuf, connfd);
        num_conn++;
        V(&sem);
    }
    exit(0);
}
/* $end echoserverimain */