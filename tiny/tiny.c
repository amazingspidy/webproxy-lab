/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];  //여기에 적혀질것들은 클라이언트 정보임.
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);  //  리스닝소켓: 우리가 열어줄 서버의 포트번호 입력받음.
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 여기서 clientaddr가 채워짐
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0); //여기서 클라이언트의 host, port가 채워짐
    printf("Accepted connection from (%s, %s)\n", hostname, port); // 채워진 정보 바탕으로 클라이언트정보 띄워주기.
    doit(connfd);   
    Close(connfd); 
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  /*요청 라인과 헤더를 읽는다*/
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE); // ex) GET / HTTP/1.1
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); //파싱작업: 윗줄과 같은 입력이 들어오면 method = GET, uri = /, version = HTTP/1.1
  
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD")==0)) {  //GET과 HEAD 이외의 요청은 예외처리. strcasecmp함수는 같으면 0을 리턴.
    clienterror(fd, method, "501", "Not implemented",
    "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);  
  
  is_static = parse_uri(uri, filename, cgiargs);
  
  if (stat(filename, &sbuf) < 0) {  //stat함수, filename에 해당하는 파일이 존재해야 0을반환. sbuf에는 filename파일에 관한 정보가 채워짐.
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {  //파일이 정규/읽기전용이어야 제대로된 파일접근.
      clienterror(fd, filename, "403", "Forbidden",
      "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);

  }

  else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
      "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
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

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);  // 요청헤더를 한줄씩 출력한다.
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {  //strstr-> substring search
    strcpy(cgiargs, "");
    strcpy(filename, ".");  //상대경로 처리를 위한과정.
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')  //uri가 /이면 home.html로 uri가 대체됨.
      strcat(filename, "home.html");
    return 1;
  }

  else {
    ptr = index(uri, '?'); // ptr을 ?가 존재하는곳으로 위치시키고
    if (ptr) {
      strcpy(cgiargs, ptr+1); // ptr한칸뒤부터 cgiargs에 복사.
      *ptr = '\0'; // ptr의 위치를 NULL로 바꿔줌. 아래 strcpy(filename, uri) 에서 uri만 넣어주기위함.
    }
    else 
      strcpy(cgiargs,"");
    strcpy(filename, ".");  
    strcat(filename, uri); 
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /*응답헤더를 클라이언트에게 보낸다*/
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  //필수 헤더. 사실 아래는 필요X
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);   //MIME 타입
  Rio_writen(fd, buf, strlen(buf));  //생성된 응답 헤더를 클라이언트에게 전송.
  printf("Response headers:\n");
  printf("%s", buf);                //생성된 응답 헤더를 표준 출력에 출력.

   if (strcasecmp(method, "HEAD")==0) {
    return;  //HEAD요청이 들어오면 요청헤더만 보내므로, 여기서 Cut.
   }

  /*응답바디를 클라이언트에게 보낸다*/
  srcfd = Open(filename, O_RDONLY, 0);  //요청된 파일을 열고 파일 디스크립터(srcfd)를 얻음.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);  //Mmap 함수를 사용하여 파일을 메모리 매핑하고, 매핑된 메모리의 포인터(srcp)를 얻음.
  // Close(srcfd);    //파일 디스크립터를 닫음
  // Rio_writen(fd, srcp, filesize);  //메모리 매핑된 파일을 클라이언트에게 전송.
  // Munmap(srcp, filesize);

  srcp = (char *)malloc(filesize);  //malloc과 Mmap의 가장큰차이: Mmap은 매핑과 동시에 데이터를 넣어줌. malloc은 공간할당만해줌.

  if (srcp!=NULL){
      //rio_readn 함수는 식별자 fd의 현재 파일 위치에서 메모리 위치 usrbuf로 최대 n바이트를 전송, 오류시 -1반환
    if (Rio_readn(srcfd, srcp, filesize) < 0) {  //srcp에, srcfd식별자파일의 filesize만큼을 읽어와서 저장
      fprintf(stderr, "Error reading file\n");   //이과정이 Mmap을쓰면 필요가 없음
      free(srcp);
      Close(srcfd);
      return;
    }
  }
  else {
    fprintf(stderr, "Error allocating memory\n");
    Close(srcfd);
    return;
  }

  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  free(srcp);
  
}



void get_filetype(char *filename, char *filetype) {
  //받고자 하는 파일타입을 언제든지 추가 및 확장 가능
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype,"video/mp4");
  else
    strcpy(filetype, "text/plain");

}


void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {  //자식프로세스 생성 -> ex) adder의 자식프로세스
    setenv("QUERY_STRING", cgiargs, 1);  //환경변수 설정. 두번째 인자를 첫번째 인자로 전달됨.
    Dup2(fd, STDOUT_FILENO);  // 자식프로세스의 표준출력이 클라이언트소켓으로 전송됨.
    Execve(filename, emptylist, environ); // 새로운 프로세스를 실행하는 시스템콜

  }
  Wait(NULL);
}



