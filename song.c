//基于上一个版本通过预线程化来实现并发
//通过简单的200多行代码来实现一个虽小但是功能齐全的Web服务器。
//先实现迭代服务器，客户端的请求用doit函数封装。
//另外还需要错误处理函数，提供静态内容函数，提供动态内容函数，获取文件类型函数，忽略请求报头函数，解析URI函数

#include "csapp.h"
#include "sbuf.h"

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void serve_static(int fd, char *filename, int filesize);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void get_filetype(char *filename, char *filetype);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);

void *thread(void *vargp);

#define SBUFSIZE 4
#define NTHREADS 4

sbuf_t sbuf;

int main(int argc, char **argv)
{
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_in clientaddr;

	int pthreadsize = 4;
	int i;
	pthread_t *tid = (pthread_t *)Calloc(pthreadsize, sizeof(pthread_t));

	/*if(argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}*/

	listenfd = Open_listenfd(7777);

	sbuf_init(&sbuf, SBUFSIZE);
	for(i = 0; i < NTHREADS; ++i)
	{
		Pthread_create(&tid[i], NULL, thread, NULL);
	}

	while(1)
	{
		if((sbuf.rear - sbuf.front) == 0)
		{
			//限制最小工作线程数量，不能一直减没有底线！
			//if(pthreadsize >= 8)
			//{
				int j;
				pthread_t *newtid = (pthread_t *)Calloc(pthreadsize/2, sizeof(pthread_t));
				for(j = 0; j < pthreadsize/2; j++)
				{
					newtid[j] = tid[j];
				}
				for(; j < pthreadsize; j++)
				{
					pthread_cancel(tid[j]);
				}
				pthread_t *tmp = tid;
				tid = newtid;
				Free(tmp);
				pthreadsize = pthreadsize / 2;
				printf("half now:%d\n", pthreadsize);
			//}
		}

		if((sbuf.rear - sbuf.front) == sbuf.n)
		{
			int j;
			pthread_t *newtid = (pthread_t *)Calloc(pthreadsize*2, sizeof(pthread_t));
			for(j = 0; j < pthreadsize; j++)
			{
				newtid[j] = tid[j];
			}
			for(; j < pthreadsize*2; j++)
			{
				//这里之前写成tid[j],我还以为是我操作动态内存不当！
				Pthread_create(&newtid[j], NULL, thread, NULL);
			}
			pthread_t *tmp = tid;
			tid = newtid;
			Free(tmp);

			pthreadsize = pthreadsize * 2;
			printf("double now:%d\n", pthreadsize);
		}

		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		//Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		//这里getnameinfo不知道怎么搞得用不了，所以我还是用的inep_ntoa
		printf("Accept from %s %d\n", inet_ntoa(clientaddr.sin_addr), clientaddr.sin_port);

		sbuf_insert(&sbuf, connfd);
		//doit(connfd);//HTTP事务处理
		//Close(connfd);//处理事务完毕直接关闭连接
		//这里睡两秒的原因是，之前测试一直很难有减少线程的情况，所以我猜想应该是工作线程还没来得及抢的原因！
		sleep(2);
	}
}

void *thread(void *vargp)
{
	Pthread_detach(pthread_self());
	while(1)
	{
		//pthread_testcancel();//测试取消
		int connfd = sbuf_remove(&sbuf);
		doit(connfd);
		Close(connfd);

		//测试取消放后面是因为等工作完了再取消。这个与pthread_testcancel()函数有关！
		//但是实践证明，在处理事务中就被关闭啦，可能是因为doit里面有涉及系统调用！
		pthread_testcancel();
	}
}


void doit(int fd)
{
	int is_static;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	rio_t rio;

	//从客户端fd读取请求行和请求头到buf
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);

	printf("request header:%s\n", buf);

	sscanf(buf, "%s %s %s", method, uri, version);

	//只处理GET方法，其他一律返回错误信息
	if(strcasecmp(method, "GET"))
	{
		clienterror(fd, method, "501", "Not implemented", "Song does not implement this method");
		return ;
	}

	//忽略请求报头
	read_requesthdrs(&rio);

	//解析uri
	is_static = parse_uri(uri, filename, cgiargs);

	//
	if(stat(filename, &sbuf) < 0)
	{
		clienterror(fd, filename, "404", "Not found", "Song can't find the file");
		return;
	}

	if(is_static)
	{
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
		{
			clienterror(fd, filename, "403", "Forbidden", "Song can't read the file");
			return;
		}
		serve_static(fd, filename, sbuf.st_size);
	}
	else
	{
		if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
		{
			clienterror(fd, filename, "403", "Forbidden", "Song can't run the file");
			return;
		}
		serve_dynamic(fd, filename, cgiargs);
	}

}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXLINE];

	//
	sprintf(body, "<html><title>Song Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Song Web server</em>\r\n", body);

	//
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));

	sprintf(buf, "Conent-Type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));

	sprintf(buf, "Conent-Length: %d\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));

	Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	while(strcmp(buf, "\r\n"))
	{
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s\n", buf);
	}
	return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr;
	if(!strstr(uri, "cgi-bin"))
	{
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		if(uri[strlen(uri) - 1] == '/')
		{
			strcat(filename, "home.html");
		}
		return 1;
	}
	else
	{
		ptr = index(uri, '?');
		if(ptr)
		{
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");

		strcpy(filename, ".");
		strcat(filename, uri);

		return 0;
		
	}
}

void serve_static(int fd, char *filename, int filesize)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXLINE];

	//
	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Song Web Server\r\n", buf);
	sprintf(buf, "%sConnection: colse\r\n", buf);
	sprintf(buf, "%sContent-Type: %s\r\n", buf, filetype);
	sprintf(buf, "%sContent-Length: %d\r\n\r\n", buf, filesize);

	Rio_writen(fd, buf, strlen(buf));
	printf("Response header: %s\n", buf);

	srcfd = Open(filename, O_RDONLY, 0);
	srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
	Close(srcfd);

	Rio_writen(fd, srcp, filesize);
	Munmap(srcp, filesize);
}

void get_filetype(char *filename, char*filetype)
{
	if(strstr(filename, ".html"))
	{
		strcpy(filetype, "text/html");
	}
	else if(strstr(filename, ".gif"))
	{
		strcpy(filetype, "image/gif");
	}
	else if(strstr(filename, ".png"))
	{
		strcpy(filetype, "image/png");
	}
	else if(strstr(filename, ".jpg"))
	{
		strcpy(filetype, "image/jpeg");
	}
	else	
		strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
	char buf[MAXLINE], *emptylist[] = { NULL };

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	Rio_writen(fd, buf, strlen(buf));

	sprintf(buf, "Server: Song Web Server\r\n");
	Rio_writen(fd, buf, strlen(buf));

	if(Fork() == 0)
	{
		setenv("QUERY_STRING", cgiargs, 1);
		Dup2(fd, STDOUT_FILENO);
		Execve(filename, emptylist, environ);
	}
	Wait(NULL);
}