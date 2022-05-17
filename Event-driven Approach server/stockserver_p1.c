/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

typedef struct _item{
	int id;
	int left_stock;
	int price;
	int readcnt;
	struct _item * left;
	struct _item * right;
	sem_t mutex;
	sem_t w;
}item;	// stock tree node 구조체

item* stocklist=NULL;	// stock tree

// tree에 new node 삽입
void insert_tree(item *tree, item*new){
	if (tree->id == new->id)
		return;
	else if(tree->id > new->id){
		if (tree->left == NULL){
			tree->left = new;
			return;
		}
		insert_tree(tree->left, new);
	}
	else{
		if (tree->right == NULL){
			tree->right = new;
			return;
		}
		insert_tree(tree->right, new);
	}
	return;
}

// stock.txt를 읽고 stock tree 생성
void init_stock(){
	FILE*fp;
	int id, n, price;
	item* tmp;
	fp = fopen("stock.txt", "r");
	while(fscanf(fp, "%d %d %d", &id, &n, &price)!=EOF){
		tmp = (item*)malloc(sizeof(item));
		tmp->id = id;
		tmp->left_stock = n;
		tmp->price = price;
		tmp->left = NULL;
		tmp->right = NULL;
		tmp->readcnt = 0;
		Sem_init(&(tmp->mutex), 0, 1);
		Sem_init(&(tmp->w), 0, 1);
		if (stocklist==NULL){
			stocklist = tmp;
			continue;
		}
		insert_tree(stocklist, tmp);
	}
	fclose(fp);
}

char string[MAXLINE];	// client에게 보낼 string

// show 명령어 동작
// tree를 dfs로 탐색하며 tree 출력
void show_stock(item* tree){
	char buf[MAXLINE];
	if(tree == NULL)
		return;

	P(&(tree->mutex));
	tree->readcnt++;
	if(tree->readcnt == 1)
		P(&(tree->w));
	V(&(tree->mutex));

	sprintf(buf, "%d %d %d\n", tree->id, tree->left_stock, tree->price);
	
	P(&(tree->mutex));
	tree->readcnt--;
	if(tree->readcnt == 0)
		V(&(tree->w));
	V(&(tree->mutex));
	strcat(string, buf);
	show_stock(tree->right);
	show_stock(tree->left);
	return;
}

// stock tree에서 목표 id의 left_stock을 num개만큼 빼주는 함수
void sub_stock(item* tree, int id, int num){
	if (tree == NULL)
		return;
	if(tree->id == id){
		P(&(tree->w));
		if(tree->left_stock < num){
			strcpy(string, "Not enough left stock\n\0");
			V(&(tree->w));
			return;
		}
		tree->left_stock -= num;
		V(&(tree->w));
		strcpy(string, "[buy] success\n\0");
	}
	else if(tree->id > id)
		sub_stock(tree->left, id, num);
	else
		sub_stock(tree->right, id, num);
	return;
}

// buy 명령어 동작
void buy_stock(char*line){
	char tmp[7];
	int id, num;
	sscanf(line, "%s %d %d", tmp, &id, &num);

	sub_stock(stocklist, id, num);
	if(string[0] == '\0')
		strcpy(string, "no such stock\n\0");
}

// stock tree에서 목표 id의 left_stock을 num개만큼 더해주는 함수
void add_stock(item* tree, int id, int num){
	if(tree ==NULL)
		return;
	if(tree->id == id){
		P(&(tree->w));
		tree->left_stock += num;
		V(&(tree->w));
		strcpy(string, "[sell] success\n\0");
	}
	else if(tree->id > id)
		add_stock(tree->left, id, num);
	else
		add_stock(tree->right, id, num);
	return;
}

// sell 명령어 동작
void sell_stock(char*line){
	char tmp[7];
	int id, num;
	sscanf(line, "%s %d %d", tmp, &id, &num);
	add_stock(stocklist, id, num);
	if(string[0] == '\0')
		strcpy(string, "no such stock\n\0");
}

// client의 명령어를 수행하는 함수
int run(int connfd){
	int n;
	char buf[MAXLINE];
	rio_t rio;

	string[0] = '\0';

	Rio_readinitb(&rio, connfd);
	n = Rio_readlineb(&rio, buf, MAXLINE);
	Rio_readinitb(&rio, 0);
	if(n==0)
		return 0;
	printf("server received %d byte.\n", n);

	if (strncmp(buf, "exit", 4) == 0)
		return 0;
	else if(strncmp(buf, "show", 4) == 0)
		show_stock(stocklist);
	else if(strncmp(buf, "buy ", 4) == 0)
		buy_stock(buf);
	else if(strncmp(buf, "sell ", 5) == 0)
		sell_stock(buf);
	
	Rio_writen(connfd, string, MAXLINE);

	return 1;
}

// stock tree의 내용을 stock.txt에 저장
void save_stock(FILE* fp, item*node){
	if(node == NULL)
		return;
	fprintf(fp, "%d %d %d\n", node->id, node->left_stock, node->price);
	save_stock(fp, node->left);
	save_stock(fp, node->right);
	return;
}

void free_all(item*node){
	if(node==NULL)
		return;
	free_all(node->left);
	free_all(node->right);
	free(node);
	return;
}

// sigint handler. save_stock을 호출하고 함수 종료
void sigint_handler(int sig){
	FILE*fp = fopen("stock.txt", "w");
	save_stock(fp, stocklist);
	fclose(fp);
	free_all(stocklist);	
	exit(0);
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */ 
    char client_hostname[MAXLINE], client_port[MAXLINE];
	fd_set read_fds, tmp_fds;
	int max_fd = 0;
	int fd_num = 0;
	
	Signal(SIGINT, sigint_handler);		// sigint handler install
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
    }
	
	init_stock();						// stock tree 생성

    listenfd = Open_listenfd(argv[1]);	// listenfd open
	FD_ZERO(&read_fds);					// file descriptor list 초기화
	FD_SET(listenfd, &read_fds);
	max_fd = listenfd;

    while (1) {
		tmp_fds = read_fds;
		Select(max_fd+1, &tmp_fds, (fd_set*)0, (fd_set*)0, NULL);		// select로 event 탐지
		if(FD_ISSET(listenfd, &tmp_fds)){										// listenfd에 event 발생시 accept 후 file descriptor list에 connfd 추가
			clientlen = sizeof(struct sockaddr_storage); 
			connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
			FD_SET(connfd, &read_fds);
			if(connfd > max_fd)
				max_fd = connfd;
	    	Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
	    	printf("Connected to (%s, %s)\n", client_hostname, client_port);
			fd_num++;
			continue;
		}

		for(int i = 0; i <= max_fd; i++){										// connfd에 event 발생시 동작함수 run 호출
			if(FD_ISSET(i, &tmp_fds)){
				if(run(i) == 0){
					Close(i);
					FD_CLR(i, &read_fds);
					fd_num--;
					if(fd_num == 0){
						FILE*fp = fopen("stock.txt","w");
						save_stock(fp, stocklist);
						fclose(fp);
					}
				}
			}
    	}
	}
    exit(0);
}

/* $end echoserverimain */