#pragma once 
#include <iostream>
#include <queue>
#include <pthread.h>
#include "Log.hpp"
#include "Task.hpp"

#define NUM 6

template <class T>
class ThreadPool
{
  private:
    int num;
    bool stop;
    std::queue<T> task_queue;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    static ThreadPool<T>* single_instance;

    ThreadPool(int _num = NUM):num(_num), stop(false)
    {
      pthread_mutex_init(&lock, nullptr);
      pthread_cond_init(&cond, nullptr);
    }

    ThreadPool(const ThreadPool&) = delete;

  public:
    static ThreadPool* getinstance(int _num = NUM)
    {
      static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;
      if(single_instance == nullptr)
      {
        pthread_mutex_lock(&_mutex);
        if(single_instance == nullptr)
        {
          single_instance = new ThreadPool<T>(_num);
          single_instance->InitThreadPool();
        }
        pthread_mutex_unlock(&_mutex);
      }
      return single_instance;
    }

    bool InitThreadPool()
    {
      for(int i = 0; i < num; i ++)
      {
        pthread_t tid;
        if(pthread_create(&tid, nullptr, ThreadRoutine, this)!= 0)
        {
          LOG(FATAL, "create thread pool error");
          return false;
        }
      }
      LOG(INFO, "create thread pool ... success");
      return true;
    }
    
    static void* ThreadRoutine(void* args)
    {
      ThreadPool* tp = (ThreadPool*)args;
      while(true)
      {
        T t;
        tp->Lock();
        while(tp->TaskQueueIsEmpty())
        {
          tp->ThreadWait();
        }
        tp->PopTask(t);
        tp->Unlock();
        t.ProcessOn();
      }
    }

    bool IsStop()
    {
      return stop;
    }

    bool TaskQueueIsEmpty()
    {
      return task_queue.size() == 0 ? true : false;
    }

    void Lock()
    {
      pthread_mutex_lock(&lock);
    }

    void Unlock()
    {
      pthread_mutex_unlock(&lock);
    }

    void ThreadWait()
    {
      pthread_cond_wait(&cond, &lock);
    }

    void ThreadWakeup()
    {
      pthread_cond_signal(&cond);
    }

    void PushTask(const T& t)
    {
      Lock();
      task_queue.push(t);
      Unlock();
      ThreadWakeup();
    }

    void PopTask(T& t)
    {
      t = task_queue.front();
      task_queue.pop();
    }

    ~ThreadPool()
    {
      pthread_mutex_destroy(&lock);
      pthread_cond_destroy(&cond);
    }
};


template <class T>
ThreadPool<T>* ThreadPool<T>::single_instance = nullptr;
