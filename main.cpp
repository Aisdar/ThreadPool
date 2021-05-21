#include <iostream>
#include <threadpool.h>
#include <math.h>

using namespace std;
static int i = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* testFun(void* arg){
    pthread_mutex_lock(&mutex);
    printf("Thread 0x%x 执行 工作 %d\n", (unsigned int)pthread_self(), ++i);
    pthread_mutex_unlock(&mutex);
    sleep(1);
}

int main()
{
    /* 初始线程数10，最大任务数100，最大线程数100 */
    ThreadPool* pool = new ThreadPool(10, 100, 100);
    /* for 循环结束，总共做的任务理应是12*5=60个 */
    for (int i = 0; i < 5; i++) {
        /* 添加12个任务函数进去， 12 > 10,看看线程池受得了不 */
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        pool->addTask(testFun, nullptr);
        sleep(1);
    }
    printf("main:busy:%d, task:%d, alive:%d\n", pool->getBusyThreadNum(), pool->getTaskNum(), pool->getAliveThreadNum());
    /* 主线程休眠10s，这时会看到manager每个1s检测一次线程情况 */
    sleep(10);
    return 0;
}
