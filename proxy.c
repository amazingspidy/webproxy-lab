#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void doit(int cfd);
void parse_uri(char *uri, char *host, char *port, char *path);
void read_requesthdrs(rio_t *rio, void *buf, int sfd, char *host, char *port);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];  //여기에 적혀질것들은 클라이언트 정보임.
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  
  listenfd = Open_listenfd(argv[1]); 
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 여기서 clientaddr가 채워짐
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0); //여기서 클라이언트의 host, port가 채워짐
    printf("Accepted connection from (%s, %s)\n", hostname, port); // 채워진 정보 바탕으로 클라이언트정보 띄워주기.
    doit(connfd);   
    Close(connfd); 
  }
  
  
  return 0;
}

void doit(int cfd) {
  int sfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char buf2[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio, rio2;
  /*요청 라인과 헤더를 읽는다*/
  //클라이언트 -> 프록시 -> 서버
  Rio_readinitb(&rio, cfd);
  Rio_readlineb(&rio, buf, MAXLINE); // ex) GET / HTTP/1.1
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s", method, uri); //쪼개주기.
  printf("method= %s uri= %s\n", method, uri);
  parse_uri(uri, host, port, path);
  
  
  sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
  
  
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD")==0)) {  //GET과 HEAD 이외의 요청은 처리하지않는다.
    clienterror(cfd, method, "501", "Not implemented",
    "Tiny does not implement this method");
    return;
  }
  sfd = Open_clientfd(host, port);
  if (sfd < 0) {
    clienterror(sfd, method, "502", "Bad Gateway", " Failed");
    return;
  }
  Rio_writen(sfd, buf, strlen(buf));
  read_requesthdrs(&rio, buf, sfd, host, port);


  //////////서버 -> 프록시 -> 클라이언트
  int content_length;
  Rio_readinitb(&rio, sfd);
  while (strcmp(buf2, "\r\n")) {
    Rio_readlineb(&rio, buf, MAXLINE);
    if (strstr(buf2, "Content-length")) {
      content_length = atoi(strchr(buf, ':') + 1);
      Rio_writen(cfd, buf2, strlen(buf2));
    }
  }

  char *response_ptr = malloc(content_length);
  Rio_readnb(&rio2, response_ptr, content_length);
  Rio_writen(cfd, response_ptr, content_length);
  free(response_ptr);
  Close(sfd);

}
//uri의 형태: http://host:port(optional)/path
void parse_uri(char *uri, char *host, char *port, char *path) {
  char *http_prefix = "http://";
  char *host_start, *port_start, *path_start;

  if (strncmp(uri, http_prefix, strlen(http_prefix)) == 0) {
    // Skip the "http://" prefix
    host_start = uri + strlen(http_prefix);
  }
  else {
    host_start = uri;
  }

  // Find the position of ":" and "/"
  port_start = strchr(host_start, ':');
  path_start = strchr(host_start, '/');

  if (port_start != NULL) { //포트가 들어오면
    *port_start = '\0'; // Null-terminate the host string at the ':' to isolate it
    port_start += 1;    // Move past ":"
    strncpy(host, host_start, port_start-host_start-1);
    strncpy(port, port_start, path_start-port_start-1);
  } else {
    strncpy(host, host_start, path_start-host_start);
    strcpy(port, "80"); //client는 웹서버 80번포트를 기본으로 요청했을거임.
  }

    // Null-terminate the path string at the end or at '?' if it exists
  

  if (path_start != NULL) {  //설마 path가 안들어와주겠어?!
      strcpy(path, path_start+1);
  } else {
    strcpy(path, "home.html");   // If no "/", set path to an empty string
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /*HTTP 응답 body 빌드*/
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /*HTTP  응답 출력*/
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rio, void *buf, int sfd, char *host, char *port) {
  int is_host_exist, is_connection_exist, is_proxy_connection_exist, is_user_agent_exist;
  Rio_readlineb(rio, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    if (strstr(buf, "Proxy-Connection") != NULL) {
      sprintf(buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(buf, "Connection") != NULL) {
      sprintf(buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(buf, "User-Agent") != NULL) {
      sprintf(buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(buf, "Host") != NULL) {
      is_host_exist = 1;
    }

    Rio_writen(sfd, buf, strlen(buf));
    Rio_readlineb(rio, buf, MAXLINE);
  }
 
  if (!is_proxy_connection_exist)
      {
        sprintf(buf, "Proxy-Connection: close\r\n");
        Rio_writen(sfd, buf, strlen(buf));
      }
      if (!is_connection_exist)
      {
        sprintf(buf, "Connection: close\r\n");
        Rio_writen(sfd, buf, strlen(buf));
      }
      if (!is_host_exist)
      {
        sprintf(buf, "Host: %s:%s\r\n", host, port);
        Rio_writen(sfd, buf, strlen(buf));
      }
      if (!is_user_agent_exist)
      {
        sprintf(buf, user_agent_hdr);
        Rio_writen(sfd, buf, strlen(buf));
      }
    
      sprintf(buf, "\r\n"); // 종료문
      Rio_writen(sfd, buf, strlen(buf));
      return;
}