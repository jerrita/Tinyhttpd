# 我的 TinyHttpd 源码阅读笔记

最近开始学一些 Linux 的网络编程，准备从读一些项目入手

简单介绍：

> tinyhttpd 是一个超轻量型 Http Server，使用 C 语言开发，全部代码只有 502 行(包括注释)，附带一个简单的 Client，用来学习非常不错，可以通过阅读这段代码理解一个 Http Server 的本质。

我的理解会以注释的形式写在源码中，故可能会超过 502 行

## Process

- [x] void accept_request(void \*);
- [ ] void bad_request(int);
- [ ] void cat(int, FILE \*);
- [ ] void cannot_execute(int);
- [ ] void error_die(const char \*);
- [ ] void execute\*cgi(int, const char \*, const char \*, const char \*);
- [x] int get_line(int, char \*, int);
- [ ] void headers(int, const char \*);
- [ ] void not_found(int);
- [ ] void serve_file(int, const char \*);
- [ ] int startup(u_short \*);
- [ ] void unimplemented(int);
- [ ] int main(void)
