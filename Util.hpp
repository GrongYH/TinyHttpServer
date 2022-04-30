#pragma once 
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>

class Util{
  public:
    static int ReadLine(int sock, std::string& out)
    {
      char ch = 'x';
      while(ch != '\n')
      {
        ssize_t s = recv(sock, &ch, 1, 0);
        if(s > 0)
        {
          if(ch == '\r')
          {
            //窥探下一个字符
            recv(sock, &ch, 1, MSG_PEEK);
            if(ch == '\n')
              recv(sock, &ch, 1, 0);
            else 
              ch = '\n';
          }
          out.push_back(ch);
        }
        else if(s == 0)
          return 0;
        else 
          return -1;
      }
      return out.size();
    }


    static bool CutString(const std::string& target, std::string& sub1_out, std::string& sub2_out, std::string sep)
    {
      size_t pos = target.find(sep);
      if(pos != std::string::npos)
      {
        sub1_out = target.substr(0, pos);
        sub2_out = target.substr(pos + sep.size());
        return true;
      }
      return false;
    }
};
