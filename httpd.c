/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN 0
#define STDOUT 1
#define STDERR 2

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* 接收请求
 * Parameters: 连接到客户端的 socket */
/**********************************************************************/
void accept_request(void *arg)
{
    int client = (intptr_t)arg; // intptr_t是为了跨平台，其长度总是所在平台的位数，所以用来存放地址
    char buf[1024];
    size_t numchars; // size_t 其大小足以保证存储内存中对象的大小
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st; // 文件基本信息
    int cgi = 0;    // 如果为 cgi 程序，则值为1
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    i = 0;
    j = 0;

    /*******************************
    GET / HTTP/1.1
    Host: 192.168.114.143:4000
    ********************************/
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j = i;
    method[i] = '\0';

    // strcasecmp: 忽略大小写比较
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // 若为 POST， 则为 cgi 程序
    if (!strcasecmp(method, "POST"))
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    // 若为 GET， url 若为 ? 打头，则也为 cgi 程序
    if (!strcasecmp(method, "GET"))
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0'; // 在 ? 前截断，保证可以拼接得到文件路径
            query_string++;       // 此时 query_string 保存了附带参数信息
        }
    }

    sprintf(path, "htdocs%s", url);
    // 若以 / 结束，则默认访问 index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1)
    {
        // 不存在文件信息，读完所有请求并返回 not_found
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        // 若请求的文件是个 dir，则再加上index.html
        // 可能存在漏洞：两个名为 index.html 的 dir，建议直接拒绝
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* 告诉客户端它的请求有问题。
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* 读取完整的文件内容返回
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 将错误原因输出到 stderr，并且退出。 */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }
    else /*HEAD or other*/
    {
    }

    if (pipe(cgi_output) < 0)
    {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0) /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        { /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL);
        exit(0);
    }
    else
    { /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* 从一个 socket 中取得一行, 无论这行是以换行符、回车符，还是 CRLF 组合结束.
 * 通过空字符终止字符串读取。在 buffer 的末尾如果没有换行的指示符，将会被截断。
 * Parameters: socket 描述符
 *             保存数据的 buffer
 *             buffer 的大小
 * Returns: 转存数据的大小（可能为0） */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    /*
    int send(int s, const void *msg, size_t len, int flags); 
    flags取值有：
        0： 与write()无异
        MSG_DONTROUTE:告诉内核，目标主机在本地网络，不用查路由表
        MSG_DONTWAIT:将单个I／O操作设置为非阻塞模式
        MSG_OOB:指明发送的是带外信息

    int recv(int s, void *buf, size_t len, int flags);
    flags取值有：
        0：常规操作，与read()相同
        MSG_DONTWAIT:将单个I／O操作设置为非阻塞模式
        MSG_OOB:指明发送的是带外信息
        MSG_PEEK:可以查看可读的信息，在接收数据后不会将这些数据丢失
        MSG_WAITALL:通知内核直到读到请求的数据字节数时，才返回。
    */

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return (i);
}

/**********************************************************************/
/* 返回 header */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename; /* 可以通过 filename 判断文件类型 */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 返回 404 信息. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 向客户端返回一个文件。不存在返回 404。
 * Parameters: client filename   */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) /* 取掉 header 头 */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* 这个函数在一个特定的接口监听网络连接。如果端口号为 0，则会随机分配一个端口
 * 并修改参数 port 的值。
 * Parameters: 指向设置端口号的指针
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    /*
    AF = Address Family
    PF = Protocol Family
    意思就是 AF_INET 主要是用于互联网地址，而 PF_INET 是协议相关，通常是sockets和端口

    使用上是没有区别的。
    这其实是设计上误差。最开始设计者设想一个AF可以支持多个PF，可是后来，就没有后来了。
    */

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));

    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    // closesocket（一般不会立即关闭而经历TIME_WAIT的过程）后想继续重用该socket：
    /*
        默认情况下，两个独立的套接字不可与同一本地接口（在TCP/IP情况下，则是端口）绑定在一起。
        但是少数情况下，还是需要使用这种方式，来实现对一个地址的重复使用。
        设置了这个套接字，服务器便可在重新启动之后，在相同的本地接口以端口上进行监听。
        一般来说，一个端口释放后会等待两分钟之后才能再被使用，SO_REUSEADDR 是让端口释放后立即就可以被再次使用。 
        SO_REUSEADDR 用于对 TCP 套接字处于 TIME_WAIT 状态下的 socket，允许重复绑定使用。
        server 程序总是应该在调用 bind() 之前设置 SO_REUSEADDR 套接字选项。
    */
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        error_die("setsockopt failed");
    }

    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0) /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* 告知客户端此方法未实现。
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");

        // accept_request(&client_sock);
        if (!pthread_create(&newthread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock))
            perror("pthread_create");
    }

    close(server_sock);

    return (0);
}
