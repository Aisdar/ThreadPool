#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>

using namespace std;

#define DEFALUT_TIME 1             /* 10s a check */
#define MIN_WAIT_TASK_NUM 10        /* 任务数阈值，超过此值时增开线程 */
#define DEFAULT_THRAD_VARY 10       /* 每次创建和销毁线程的个数 */
#define THREADPOOL_SHUTDOWN 0       /* 线程池关闭 */
#define THREADPOOL_ACTIVE   1       /* 线程池活跃 */
#define THREAD_TASK_ADD_SUCCEED 1   /* 线程池添加任务成功 */
#define THREAD_TASK_ADD_FAIL 0      /* 线程池添加任务失败 */
#define THREAD_DEBUG 1              /* debug宏 */

typedef struct Task
{
    Task()
    {
        fun = nullptr;
        arg = nullptr;
    }
    void *(*fun)(void*);        /* 任务函数指针 */
    void *arg;                  /* 传递给函数的参数 */
}TASK;

class ThreadPool
{
public:
    ThreadPool(int minThreadNum, int maxThreadNum, int maxTaskSize);
    int addTask(void* (*fun)(void* arg), void* arg);
    static int getBusyThreadNum();
    static int getIdleThreadNum();
    static int getAliveThreadNum();
    static int getTaskNum();

private:
    static void* threadFun(void* arg);       /* 工作线程函数 */
    static void* manageFun(void* arg);       /* 管理线程函数 */
    static void* idleToBusy(pthread_t pid);  /* 移动线程所在队列 空闲->忙碌 */
    static void* busyToIdle(pthread_t pid);  /* 移动线程所在队列 空闲->忙碌 */
    int isThreadAlive(pthread_t tid);        /* 判断线程是否已经存在 */

private:
    /* 任务队列、忙碌线程队列、空闲线程队列.... 被设置为静态，是因为静态函数(如threadFun....)只能使用静态的变量 */

    /* 这个地方设计的还是不太好，其实可以不使用静态变量的，就是使用arg参数传入线程池对象本身的指针.
    *  静态函数中不能用this指针，但传入参数可以解决不能访问this的问题(传参时把this传进去) */

    static vector<TASK> taskQueue;      /* 线程领取任务时的任务队列 */
    static vector<pthread_t> idleQueue; /* 空闲队列，空闲的线程都在这里 */
    static vector<pthread_t> busyQueue; /* 忙碌队列，忙碌的线程都在这里 */
    static pthread_mutex_t poolLock;    /* 操作任务队列、计数变量如最大线程数时锁 */
    static pthread_mutex_t counterLock; /* 操作计数器时的锁 */
    static pthread_cond_t  queueNotFull;/* 任务队列满时，新添加的任务会阻塞，需要等待此条件变量通知合适的时机 */
    static pthread_cond_t queueNotEmpty;/* 队列不为空时，通知等待执行任务的线程 */
    static int poolStatus;              /* 线程池状态:THREADPOOL_SHUTDOWN:关闭、THREADPOOL_ACTIVE:活跃 */

    pthread_t managerTid;               /* 管理线程池的线程threadId，对线程的增加与销毁操作可以由此线程做 */

    int minThreadNum;                   /* 线程池初始最小值 */
    int maxThreadNum;                   /* 线程池初始最大值 */
    int liveThreadNum;                  /* 当前存活的线程数量 */
    int busyThreadNum;                  /* 当前正处于工作中的线程数量 */
    int exitingThreadNum;               /* 正等待退出的线程数量 */
    int maxTaskSize;                    /* 任务队列的最大值 */
};

#endif // THREADPOOL_H
