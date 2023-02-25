#include "stdio.h"
#include "wrap.h"
#include "sys/epoll.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "pub.h"
#include "dirent.h"
#include "signal.h"
#define PORT 8889

void send_header(int cfd, int code,char *info,char *filetype,int length)
{	//发送状态行
	char buf[1024]="";
	int len =0;
	len = sprintf(buf,"HTTP/1.1 %d %s\r\n",code,info);
	send(cfd,buf,len,0);
	//发送消息头
	len = sprintf(buf,"Content-Type:%s\r\n",filetype); // 将字串拷贝到buf中，并以\0结尾。
	send(cfd,buf,len,0); // 这里的len不能改为strlen(buf)。因为如果buf中有\0，那么strlen(buf)代表的只是到\0为止的字串长度
	if(length > 0) // Content-Length可不发送，这里判断是否要发送
	{
			//发送消息头
		len = sprintf(buf,"Content-Length:%d\r\n",length);
		send(cfd,buf,len,0);

	}
	//空行
	send(cfd,"\r\n",2,0);
}
void send_file(int cfd,char *path,struct epoll_event *ev,int epfd,int flag)
{
		int fd = open(path,O_RDONLY);
		if(fd <0)
		{
			perror("");
			return ;
		}
		char buf[1024]="";
		int len =0;
		while( 1)
		{

			len = read(fd,buf,sizeof(buf));
			if(len < 0)
			{
				perror("");
				break;

			}
			else if(len == 0) // 读到文件末尾
			{
				break;
			}
			else	   
			{
				int n=0;
				n =  send(cfd,buf,len,0);
				printf("len=%d\n", n);

			}


		}
		close(fd);
		//关闭cfd,下树
		if(flag==1)
		{
			close(cfd);
			epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,ev);
		}



}
void read_client_request(int epfd ,struct epoll_event *ev)
{
	//读取请求(先读取一行,在把其他行读取,扔掉)
	char buf[1024]="";
	char tmp[1024]="";
	 int n = Readline(ev->data.fd, buf, sizeof(buf));
	 if(n <= 0)
	 {
	 	printf("close or err\n");
	 	epoll_ctl(epfd,EPOLL_CTL_DEL,ev->data.fd,ev);
	 	close(ev->data.fd);
	 	return ;
	 }
	 printf("[%s]\n", buf);
	 int ret =0;
	 while(  (ret = Readline(ev->data.fd, tmp, sizeof(tmp))) >0); // 一行一行循环得读出来，那每一次不都把tmp给覆盖了？？
	 //解析请求行  GET /a.txt  HTTP/1.1\R\N
	 //
	 char method[256]="";
	 char content[256]="";
	 char protocol[256]="";
	 sscanf(buf,"%[^ ] %[^ ] %[^ \r\n]",method,content,protocol); // [^ ]：取非空格的字符，即碰到空格就停
	 printf("[%s]  [%s]  [%s]\n",method,content,protocol );
	 //判断是否为get请求  get   GET
	 if( strcasecmp(method,"get") == 0) // strcasecmp忽略大小写
	 {
	 		//[GET]  [/a.txt]  [HTTP/1.1]
	 		char *strfile = content+1; // strfile指向a.txt
	 		strdecode(strfile,strfile); // 将中文乱码转化为中文
	 		 //GET / HTTP/1.1\R\N
	 		if(*strfile == 0)// 如果没有请求文件,那么strfile将指向字符串的结尾\0，即*strfile == 0
	 			strfile= "./"; // 默认请求当前目录

	 		//判断请求的文件在不在
	 		struct stat s;
	 		if(stat(strfile,&s)< 0)//文件不存在。stat将文件strfile的一些信息放入s中
	 		{
	 			printf("file not fount\n");
	 			//先发送 报头(状态行  消息头  空行)
	 			send_header(ev->data.fd, 404,"NOT FOUND",get_mime_type("*.html"),0); // get_mime_type为pub.c中的函数
	 			//发送文件 error.html
	 			send_file(ev->data.fd,"error.html",ev,epfd,1);


	 		}
	 		else
	 		{
	 			
	 			//请求的是一个普通的文件
	 			if(S_ISREG(s.st_mode))
	 			{
	 				printf("file\n");
	 				//先发送 报头(状态行  消息头  空行)
	 				send_header(ev->data.fd, 200,"OK",get_mime_type(strfile),s.st_size);
	 				//发送文件
	 				send_file(ev->data.fd,strfile,ev,epfd,1);

	 			}
	 			else if(S_ISDIR(s.st_mode))//请求的是一个目录
	 			{
						printf("dir\n");
						//发送一个列表  网页
						send_header(ev->data.fd, 200,"OK",get_mime_type("*.html"),0);
						//发送header.html
						send_file(ev->data.fd,"dir_header.html",ev,epfd,0); // 0表示是否要关闭ev->data.fd      

						struct dirent **mylist=NULL;
						char buf[1024]="";
						int len =0;
						int n = scandir(strfile,&mylist,NULL,alphasort);
						for(int i=0;i<n;i++)
						{
							//printf("%s\n", mylist[i]->d_name);
							if(mylist[i]->d_type == DT_DIR) // 如果是目录，超链接后面要加/
							{
								len = sprintf(buf,"<li><a href=%s/ >%s</a></li>",mylist[i]->d_name,mylist[i]->d_name);
							}
							else
							{
								len = sprintf(buf,"<li><a href=%s >%s</a></li>",mylist[i]->d_name,mylist[i]->d_name);
							}

							

							send(ev->data.fd,buf,len ,0);

							free(mylist[i]);
						}
						free(mylist);


						send_file(ev->data.fd,"dir_tail.html",ev,epfd,1);

	 			}


	 		}




	 }


}
int main(int argc, char const *argv[])
{
	signal(SIGPIPE,SIG_IGN); // 套接字通信抽象成管道进行通信。浏览器读缓冲区关闭，即管道的读端关闭了，
							  // 如果此时服务器端（写端）向管道中写数据，服务器就会收到SIGPIPE，这时服务器会被杀死。这里将SIGPIPE忽略。

	//切换工作目录
	char pwd_path[256]="";
	char * path = getenv("PWD");  // 获取当前目录的工作路径。在linux命令行下输入env，可以看到当前环境的信息。其中就包括当前工作目录。
	///home/itheima/share/bjc++34/07day/web-http
	strcpy(pwd_path,path);
	strcat(pwd_path,"/web-http");
	chdir(pwd_path);	// 切换目录


	//创建套接字 绑定
	int lfd = tcp4bind(PORT,NULL);
	//监听
	Listen(lfd,128);
	//创建树
	int epfd = epoll_create(1);
	//将lfd上树
	struct epoll_event ev,evs[1024];
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	//循环监听
	while(1)
	{
		int nready = epoll_wait(epfd,evs,1024,-1);
		if(nready < 0)
		{
			perror("");
			break;
		}
		else
		{
			for(int i=0;i<nready;i++)
			{
				printf("001\n");
				//判断是否是lfd
				if(evs[i].data.fd == lfd && evs[i].events & EPOLLIN)
				{
					struct sockaddr_in cliaddr;
					char ip[16]="";
					socklen_t len = sizeof(cliaddr);
					int cfd = Accept(lfd,(struct sockaddr*)&cliaddr,&len);
					printf("new client ip=%s port=%d\n",
						inet_ntop(AF_INET,&cliaddr.sin_addr.s_addr,ip,16),
						ntohs(cliaddr.sin_port));
					//设置cfd为非阻塞
					int flag = fcntl(cfd,F_GETFL);
					flag |= O_NONBLOCK;
					fcntl(cfd,F_SETFL,flag);
					//上树
					ev.data.fd = cfd;
					ev.events = EPOLLIN;
					epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);


				}
				else if(evs[i].events & EPOLLIN)//cfd变化
				{
					read_client_request(epfd,&evs[i]);

				}



			}


		}


	}
	//=收尾

	return 0;
}
