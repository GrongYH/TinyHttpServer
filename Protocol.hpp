#pragma once 
#include <iostream>
#include "Log.hpp"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include "Util.hpp"


#define SEP ": "
#define WEB_ROOT "wwwroot"
#define HOME_PAGE "index.html"
#define HTTP_VERSION "HTTP/1.0"
#define LINE_END "\r\n"
#define PAGE_404 "404.html"

#define OK 200
#define BAD_REQUEST 400
#define NOT_FOUND 404
#define SERVER_ERROR 500

static std::string Code2Desc(int code)
{
  std::string desc;
  switch(code)
  {
    case 200:
      desc = "OK";
      break;
    case 404:
      desc = "Not Found";
      break;
    default:
      break;
  }

  return desc;
}


static std::string Suffix2Desc(const std::string& suffix)
{
  static std::unordered_map<std::string, std::string> suffix2desc = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".jpg", "application/x-jpg"},
    {".xml", "application/xml"},
  };
  auto iter = suffix2desc.find(suffix);
  if(iter != suffix2desc.end())
  {
    return iter->second;
  }
  return "text/html";
}

class HttpRequest{
  public:
    std::string request_line;
    std::vector<std::string> request_header;
    std::string blank;
    std::string request_body;

    //解析完之后的结果
    std::string method;
    std::string uri; //path?args
    std::string version;

    std::unordered_map<std::string, std::string> header_kv;
    int content_length;
    std::string path;
    std::string suffix;
    std::string query_string;

    bool cgi;
    int size;
  public:
    HttpRequest():content_length(0),cgi(false){}
    ~HttpRequest(){}

};

class HttpResponse{
  public:
    std::string status_line;
    std::vector<std::string> response_header;
    std::string blank;
    std::string response_body;

    int status_code;
    int fd;

  public:
    HttpResponse():blank(LINE_END),status_code(OK),fd(-1){}
    ~HttpResponse(){}
};

//读取请求，分析请求，构建响应
//IO通信
class EndPoint{
  private:
    int sock;
    HttpRequest http_request;
    HttpResponse http_response;
    bool stop;
  private:
    bool RecvHttpRequestLine()
    {
      auto& line = http_request.request_line;
      if(Util::ReadLine(sock, line) > 0)
      {
        line.resize(line.size() - 1);
        LOG(INFO, http_request.request_line);
      }
      else 
      {
        stop = true;
      }
      return stop;
    }

    bool RecvHttpRequestHeader()
    {
      std::string line;
      while(true)
      {
        line.clear();
        if(Util::ReadLine(sock, line) <= 0)
        {
          stop = true;
          break;
        }
        if(line == "\n")
        {
          http_request.blank = line;
          break;
        }
        line.resize(line.size() - 1);
        http_request.request_header.push_back(line);
      }
      return stop;
    }

    void ParseHttpRequestLine()
    {
      auto& line = http_request.request_line;
      std::stringstream ss(line);
      ss>>http_request.method >> http_request.uri >> http_request.version;
      auto& method = http_request.method;
      std::transform(method.begin(), method.end(), method.begin(), ::toupper);
    }

    //将请求报头存到kv里面
    void ParseHttpRequestHeader()
    {
      std::string key;
      std::string value;
      for(auto& iter: http_request.request_header)
      {
        if(Util::CutString(iter, key, value, SEP))
          http_request.header_kv.insert({key, value});
      }
    }

    bool IsNeedRecvHttpRequestBody()
    {
      auto& method = http_request.method;
      if(method == "POST")
      {
        auto& header_kv = http_request.header_kv;
        auto iter = header_kv.find("content-length");
        if(iter != header_kv.end())
        {
          LOG(INFO, "Post method, Content-Length:" + iter->second);
          http_request.content_length = atoi(iter->second.c_str());
          return true;
        }
      }
      return false;
    }

    bool RecvHttpRequestBody()
    {
      if(IsNeedRecvHttpRequestBody())
      {
        int content_length = http_request.content_length;
        std::cout<<"这里打印一下长度"<<std::endl;
        auto& body = http_request.request_body;
        char ch ='x';
        while(content_length)
        {
          ssize_t s = recv(sock, &ch, 1, 0);
          if(s > 0)
          {
            body.push_back(ch);
            content_length --;
          }
          else 
          {
            stop = true;
            break;
          }
        }
        std::cout<<"这里是body"<<std::endl;
        LOG(INFO, body);
      }
      return stop;
    }

    int ProcessCgi()
    {
      LOG(INFO, "proccess cgi method");
      int code = OK;
      //父进程的数据
      auto& method = http_request.method;
      auto& query_string = http_request.query_string; //GET
      auto& body_text = http_request.request_body; //POST
      auto& bin = http_request.path;//要让子进程执行的目标程序，而且该目标程序一定存在
      int content_length = http_request.content_length;
      auto& response_body = http_response.response_body;

      std::string query_string_env;
      std::string method_env;
      std::string content_length_env;

      //站在父进程的角度
      int input[2];
      int output[2];
      if(pipe(input) < 0)
      {
        LOG(ERROR, "pipe intput error");
        code = SERVER_ERROR;
        return code;
      }
      if(pipe(output) < 0)
      {
        LOG(ERROR, "pipe output error");
        code = SERVER_ERROR;
        return code;
      }

      pid_t pid = fork();
      //子进程
      if(pid == 0)
      {
        close(input[0]);
        close(output[1]);

        method_env = "METHOD=";
        method_env += method;
        LOG(INFO, method_env);
        putenv((char*)method_env.c_str());
        if(method == "GET")
        {
          query_string_env = "QUERU_STRING=";
          query_string_env += query_string;
          putenv((char*)query_string_env.c_str());
          LOG(INFO, "Get method, add query_string env "+ query_string_env);
        }
        else if(method == "POST")
        {
          content_length_env = "CONTENT_LENGTH=";
          content_length_env += std::to_string(content_length);
          putenv((char*)content_length_env.c_str());
          LOG(INFO,"Post method, add content length env " + content_length_env);
        }
        else 
        {
          //do nothing
          
        }
        std::cout<< "bin:" <<bin <<std::endl;
        dup2(output[0], 0);
        dup2(input[1], 1);
        execl(bin.c_str(), bin.c_str(), nullptr);
        exit(1);
      }
      else if(pid < 0)
      {
        LOG(ERROR, "fork error");
        return SERVER_ERROR;
      }
      else 
      {
        close(input[1]);
        close(output[0]);

        if(method == "POST")
        {
          const char* start = body_text.c_str();
          int total = 0, size = 0;
          while(total < content_length && (size = write(output[1], start + size, body_text.size() - total)))
          {
            total += size;
          }
        }
        char ch = 0;
        while(read(input[0], &ch, 1) > 0)
        {
          response_body.push_back(ch);
        }
        int status = 0;
        pid_t ret = waitpid(pid, &status, 0);
        if(ret == pid)
        {
          //若正常退出
          if(WIFEXITED(status))
          {
            //若退出码为0
            if(WEXITSTATUS(status) == 0)
            {
              std::cout<<"OK"<<std::endl;
              code = OK;
            }
            else 
            {
              std::cout<<"BAD_REQUEST"<<std::endl;
              code = BAD_REQUEST;
            }
          }
          else
          {
            std::cout<<"SERVER_ERROR"<<std::endl;
            code = SERVER_ERROR;
          }
        }
        close(input[0]);
        close(output[1]);
      }
      return code;
    }

    void HandlerError(std::string page)
    {
      std::cout<< "debug" << page <<std::endl;
      ;http_request.cgi = false;
      //给用户返回对应的页面
      http_response.fd = open(page.c_str(), O_RDONLY);
      if(http_response.fd > 0)
      {
        struct stat st;
        stat(page.c_str(), &st);
        http_request.size = st.st_size;

        std::string line = "content-type: text/html";
        line += LINE_END;
        http_response.response_header.push_back(line);

        line += "Content-Length: ";
        line += std::to_string(st.st_size);
        line += LINE_END;
        http_response.response_header.push_back(line);
      }

    }

    int ProcessNonCgi()
    {
      http_response.fd = open(http_request.path.c_str(), O_RDONLY);
      if(http_response.fd >= 0)
      {
        LOG(INFO, http_request.path + "open success");
        return OK;
      }
      return NOT_FOUND;
    }

    void BuildOkResponse()
    {
      std::string line = "Content-type: ";
      line += Suffix2Desc(http_request.suffix);
      line += LINE_END;
      http_response.response_header.push_back(line);

      line = "Content-Length: ";
      if(http_request.cgi)
      {
        line += std::to_string(http_response.response_body.size());
      }
      else 
      {
        line += std::to_string(http_request.size);//Get
      }
      line += LINE_END;
      http_response.response_header.push_back(line);
    }

    void BuildHttpResponseHelper()
    {
      auto& code = http_response.status_code;
      //构建状态行
      auto& status_line = http_response.status_line;
      status_line += HTTP_VERSION;
      status_line += " ";
      status_line += std::to_string(code);
      status_line += " ";
      status_line += Code2Desc(code);
      status_line += LINE_END;

      //构建响应正文，可能包括响应报头
      std::string path = WEB_ROOT;
      path += "/";
      switch(code)
      {
        case OK:
          BuildOkResponse();
          break;
        case NOT_FOUND:
          path += PAGE_404;
          HandlerError(path);
          break;
        case BAD_REQUEST:
          path += PAGE_404;
          HandlerError(path);
          break;
        case SERVER_ERROR:
          path += PAGE_404;
          HandlerError(path);
          break;
        default:
          break;
      }
    }

  public:
    EndPoint(int _sock):sock(_sock), stop(false){}
    bool IsStop()
    {
      return stop;
    }

    void RecvHttpRequest()
    {
      if(!RecvHttpRequestLine() && !RecvHttpRequestHeader())
      {
        ParseHttpRequestLine();
        ParseHttpRequestHeader();
        RecvHttpRequestBody();
      }
    }

    //这部分主要负责构建响应
    void BuildHttpResponse()
    {
      //请求已经全部读完，可以直接构建响应
      std::string _path;
      struct stat st;
      std::size_t found = 0;
      auto& code = http_response.status_code;
      if(http_request.method != "GET" && http_request.method != "POST")
      {
        std::cout<<"method: "<<http_request.method <<std::endl;
        LOG(WARNING, "method is not right");
        code = BAD_REQUEST;
        goto END;
      }
      if(http_request.method == "GET")
      {
        size_t pos = http_request.uri.find('?');
        if(pos != std::string::npos)
        {
          Util::CutString(http_request.uri, http_request.path, http_request.query_string, "?");
          http_request.cgi = true;
        }
        else 
        {
          http_request.path = http_request.uri;
        }
      }
      else if(http_request.method == "POST")
      {
        http_request.cgi = true;
        http_request.path = http_request.uri;
      }
      else 
      {
        //do nothing 
        //保证代码逻辑完整性
      }
      _path = http_request.path;
      http_request.path = WEB_ROOT;
      http_request.path += _path;
      if(http_request.path[http_request.path.size() - 1] == '/')
      {
        http_request.path += HOME_PAGE;
      }
      if(stat(http_request.path.c_str(), &st) == 0)
      {
        //说明资源存在
        if(S_ISDIR(st.st_mode))
        {
          //说明请求的资源是一个目录，这是不被允许的，需要拼接该目录下的首页文件
          //虽然是一个目录，但是绝对不是以'/'结尾，因为上面已经处理过了
          http_request.path += '/';
          http_request.path += HOME_PAGE;
          //重新获取当前文件信息
          stat(http_request.path.c_str(), &st);

        }
        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
        {
          //特殊处理
          http_request.cgi = true;

        }
        http_request.size = st.st_size;
      }
      else 
      {
        //说明资源不存在
        LOG(WARNING, http_request.path + "Not Found");
        code = NOT_FOUND;
        goto END;
      }
      found = http_request.path.rfind(".");
      if(found == std::string::npos)
      {
        http_request.suffix = ".html";
      }
      else 
      {
        http_request.suffix = http_request.path.substr(found);
      }
      if(http_request.cgi)
      {
        //执行目标程序，拿到结果：http_response.response_body;;
        code = ProcessCgi();
      }
      else 
      {
        //目标网页一定存在
        //返回的不仅仅是网页，还要构建HTTP响应
        code = ProcessNonCgi();
      }
END:
      BuildHttpResponseHelper();
    }

    void SendHttpResponse()
    {
      send(sock, http_response.status_line.c_str(), http_response.status_line.size(), 0);
      for(auto iter: http_response.response_header)
      {
        send(sock, iter.c_str(), iter.size(), 0);
      }
      send(sock, http_response.blank.c_str(), http_response.blank.size(), 0);
      if(http_request.cgi)
      {
        auto& response_body = http_response.response_body;
        size_t size = 0;
        size_t total = 0;
        const char* start = response_body.c_str();
        while(total < response_body.size() && (size= send(sock, start + total, response_body.size() - total, 0)) > 0)
        {
          total += size;
        }
      }
      else 
      {
        std::cout<<"............................."<< http_response.fd << std::endl;
        std::cout<<"............................."<< http_request.size <<std::endl;
        sendfile(sock, http_response.fd, nullptr, http_request.size);
        close(http_response.fd);
      }
    }

    ~EndPoint()
    {
      close(sock);
    }

};


class CallBack{
  public:
    CallBack(){}

    void operator()(int sock)
    {
      HandlerRequest(sock);
    }

    void HandlerRequest(int sock)
    {
      LOG(INFO, "Handler request begin");
#ifdef DEBUG
      char buffer[4096];
      recv(sock, buffer, sizeof(buffer), 0);
      std::cout<<"------------------------------------"<<std::endl;
      std::cout<<buffer<<std::endl;
      std::cout<<"------------------------------------"<<std::endl;
#else
      EndPoint* ep = new EndPoint(sock);
      ep->RecvHttpRequest();
      if(!ep->IsStop())
      {
        LOG(INFO, "Recv no error, begin build and send");
        ep->BuildHttpResponse();
        ep->SendHttpResponse();
      }
      else{
        LOG(WARNING, "Recv Erro, stop build and send");
      }
      delete ep;
#endif
      LOG(INFO, "Handler request end");
    }


    ~CallBack(){}
};
