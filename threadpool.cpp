#include "threadpool.h"

/* 初始化静态变量。队列(构造函数)...互斥锁、条件变量(linux宏定义) */
vector<TASK> ThreadPool::taskQueue = vector<Task>();
vector<pthread_t> ThreadPool::idleQueue = vector<pthread_t>();
vector<pthread_t> ThreadPool::busyQueue = vector<pthread_t>();
pthread_mutex_t ThreadPool::poolLock  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ThreadPool::counterLock  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ThreadPool::queueNotFull = PTHREAD_COND_INITIALIZER;
pthread_cond_t ThreadPool::queueNotEmpty = PTHREAD_COND_INITIALIZER;
int ThreadPool::poolStatus = THREADPOOL_ACTIVE;

ThreadPool::ThreadPool(int minThreadNum, int maxThreadNum, int maxTaskSize)
    : minThreadNum(minThreadNum),
      maxThreadNum(maxThreadNum),
      liveThreadNum(minThreadNum),
      busyThreadNum(0),
      exitingThreadNum(0),
      maxTaskSize(maxTaskSize)
{
    /* 创建一批线程，数量为池最小值 */
    pthread_t pid;
    memset(&pid, 0, sizeof(pthread_t));
    for(int i = 0; i < minThreadNum; i++)
    {
        /* 请注意，这里必须把this传入。我们需要对象存储的参数。*/
        pthread_create(&pid, nullptr, threadFun, this);
        idleQueue.push_back(pid);    /* 新创建的工作线程入空闲队列 */
        printf("Start Thread 0x%x...\n", (unsigned int)pid);
    }
    /* 创建一个管理线程 */
    memset(&managerTid, 0, sizeof(pthread_t));
    pthread_create(&managerTid, nullptr, manageFun, this);
    printf("Thread Pool create Succeed!\n");
}

static int i = 0;

int ThreadPool::addTask(void *(*fun)(void *arg), void *arg)
{
    pthread_mutex_lock(&poolLock);
    /* 任务队列达到预设上限，等待工作线程取走任务发出的signal */
    while(taskQueue.size() == maxTaskSize && poolStatus == THREADPOOL_ACTIVE)
    {
        printf("addTask() blocked \n");
        pthread_cond_wait(&queueNotFull, &poolLock);
        printf("addTask() Wake up ,taskNum:%d\n", taskQueue.size());
    }

    /* 线程池关闭。拦截住任务添加 */
    if(poolStatus == THREADPOOL_SHUTDOWN)
    {
        pthread_mutex_lock(&poolLock);
    }

    /* 创建任务 */
    TASK task;
    task.fun = fun;
    task.arg = arg;
    /* 任务加入任务队列 */
    taskQueue.push_back(task);
    printf("添加任务成功%d\n", ++i);

    /* 任务队列不为空的条件变量发signal通知线程函数解开阻塞等待 */
    pthread_cond_signal(&queueNotEmpty);

    pthread_mutex_unlock(&poolLock);

}

int ThreadPool::getBusyThreadNum()
{
    int size = 0;
    pthread_mutex_lock(&counterLock);
    size = busyQueue.size();
    pthread_mutex_unlock(&counterLock);
    return size;
}

/* 提供给外部使用的，类内部不要用 */
int ThreadPool::getIdleThreadNum()
{
    int size = 0;
    pthread_mutex_lock(&counterLock);
    size = idleQueue.size();
    pthread_mutex_unlock(&counterLock);
    return size;
}

/* 提供给外部使用的，类内部不要用 */
int ThreadPool::getAliveThreadNum()
{
    int size = 0;
    pthread_mutex_lock(&counterLock);
    size = idleQueue.size() + busyQueue.size();
    pthread_mutex_unlock(&counterLock);
    return size;
}

/* 提供给外部使用的，类内部不要用 */
int ThreadPool::getTaskNum()
{
    int size = 0;
    pthread_mutex_lock(&poolLock);
    size = taskQueue.size();
    pthread_mutex_unlock(&poolLock);
    return size;
}

void *ThreadPool::threadFun(void *arg)
{
    TASK task;
    ThreadPool* pool = (ThreadPool*)arg;
    while(true)
    {
        /**********************************/
        pthread_mutex_lock(&poolLock);

        /* 无任务->阻塞等待任务添加时的signal。*/
        /* 有任务->不阻塞，直接执行 */
        while(pool->taskQueue.size() == 0 && poolStatus == THREADPOOL_ACTIVE)
        {
            printf("no task 0x%x is waiting\n", (unsigned int)pthread_self());
            pthread_cond_wait(&queueNotEmpty, &poolLock);   // 等待条件变量发signal
            /* 任务来了1 */
            if(pool->exitingThreadNum > 0)
            {/* 有任务来了，不过此任务是销毁自己 */
                pool->exitingThreadNum--;
                if(pool->liveThreadNum > pool->minThreadNum)
                {
                    printf("thread 0x%x is exiting\n", (unsigned int)pthread_self());
                    pool->liveThreadNum--;
                    pthread_mutex_unlock(&poolLock);    // [重要]线程退出，归还锁
                    pthread_exit(nullptr);
                }
            }
        }

        /* 任务来了2 */
        /* 取一个任务 */
        task = taskQueue[0];
        taskQueue.erase(taskQueue.begin());

        /*--------------------------------*/

        /* 通知addTask处因任务队列达到预设上限时而等待的任务 */
        pthread_cond_broadcast(&queueNotFull);

        pthread_mutex_unlock(&poolLock);
        /**********************************/

        /* 准备执行任务，将此线程加入忙碌队列 */
        pthread_mutex_lock(&counterLock);
        idleToBusy(pthread_self());
        pthread_mutex_unlock(&counterLock);

        /* 执行任务函数 */
        (*(task.fun))(task.arg);    // 看似小小的函数，实则执行了具体的业务

        /* 完成任务，将此线程加入空闲队列 */
        pthread_mutex_lock(&counterLock);
        busyToIdle(pthread_self());
        pthread_mutex_unlock(&counterLock);
        /*--------------------------------*/
    }
}

void *ThreadPool::manageFun(void *arg)
{
    ThreadPool* pool = (ThreadPool *)arg;
    while(poolStatus == THREADPOOL_ACTIVE){
        /* 每隔DEFAULT_TIME开启一次检测, 这个值为10比较好，这里为了测试写为了1 */
        sleep(DEFALUT_TIME);
        /* 获取线程增添时必须得到的参数 得到参数的这些操作需要上锁 */
        pthread_mutex_lock(&counterLock);
        unsigned long busyThreadNum = busyQueue.size();
        unsigned long aliveThreadNum = busyThreadNum + idleQueue.size();
        pthread_mutex_unlock(&counterLock);

        pthread_mutex_lock(&poolLock);
        unsigned long taskNum = taskQueue.size();
        pthread_mutex_unlock(&poolLock);

        pthread_mutex_lock(&poolLock);
        /* 增开线程 */
        /* 当前任务数 > 预设阈值 && 线程池允许的线程上限未达到 -> 增开线程 */
        if(taskNum > MIN_WAIT_TASK_NUM && pool->liveThreadNum < pool->maxThreadNum)
        {

            printf("Manager Try To Add Thread-----------------------------\n");
            pthread_t pid;
            /* 循环次数为DEFAULT_THRAD_VARY */
            for(int count = 0;
                count < pool->maxThreadNum                  /* 初始线程数小于DEFAULT_THREAD_VARY时，这个判断有用 */
                && count < DEFAULT_THRAD_VARY               /* 添加DEFAULT_THREAD_VARY个线程 */
                && pool->liveThreadNum < pool->maxThreadNum;     /* 是否达到池预设线程上限 */
                count++)
            {
                if(pthread_create(&pid, nullptr, threadFun, pool) < 0)
                {
                    perror("Add Thread Error:");
                }else
                {
                    /* 新线程加入空闲队列 */
                    pthread_mutex_lock(&counterLock);
                    idleQueue.push_back(pid);
                    pthread_mutex_unlock(&counterLock);

                    printf("Add New Thread 0x%x...\n", (unsigned int)pid);
                    pool->liveThreadNum++;
                }
            }
        }
        else
        {
#ifdef THREAD_DEBUG
            printf("Manager:Pool Threads Is Enough. task:%d alive:%d max:%d\n", taskNum, pool->liveThreadNum, pool->maxThreadNum);
#endif
        }
        pthread_mutex_unlock(&poolLock);

        pthread_mutex_lock(&poolLock);
        /* 销毁线程 */
        /* 忙碌线程数量*2 < 存活线程 && 存活线程 > 池预设最小线程数 */
        if(busyThreadNum * 2 < pool->liveThreadNum && pool->liveThreadNum > pool->minThreadNum)
        {
            printf("Manager Try To signal Threads to destroy----------\n");
            /* 销毁线程的最小单位是DEFAULTE_THREAD_VARY */
            pool->exitingThreadNum = DEFAULT_THRAD_VARY;

            /* 真正的销毁操作不在这里做，我们通知处于空闲的线程自毁 */
            for(int i = 0; i < DEFAULT_THRAD_VARY; i++)
            {
                pthread_cond_signal(&queueNotEmpty);
            }
        }
        else
        {
#ifdef THREAD_DEBUG
            printf("Manager:No Need To Destroy Thread. busy:%d, alive:%d, min:%d\n", busyThreadNum, pool->liveThreadNum, pool->minThreadNum);
#endif
        }
        pthread_mutex_unlock(&poolLock);
    }
}

void *ThreadPool::idleToBusy(pthread_t pid)
{
    vector<pthread_t>::iterator it;
    for(it = idleQueue.begin(); it > idleQueue.end(); it++)
    {
        if(*it == pid)
        {
            break;
        }
    }
    idleQueue.erase(it);
    busyQueue.push_back(*it);
//    printf("0x%x:加入忙碌队列 忙碌队列大小%d\n", (unsigned int)pthread_self(), busyQueue.size());
}

void *ThreadPool::busyToIdle(pthread_t pid)
{
    vector<pthread_t>::iterator it;
    for(it = busyQueue.begin(); it > busyQueue.end(); it++)
    {
        if(*it == pid)
        {
            break;
        }
    }
    busyQueue.erase(it);
    idleQueue.push_back(*it);
    //    printf("0x%x:加入空闲队列 空闲队列大小%d\n", (unsigned int)pthread_self(), idleQueue.size());
}

int ThreadPool::isThreadAlive(pthread_t tid)
{
    /* 通过信号查询此线程是否存在 */
    int killRC = pthread_kill(tid, 0);
    if(killRC == ESRCH)
    {
        return false;
    }
    return true;
}
