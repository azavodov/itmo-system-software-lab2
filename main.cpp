#include <pthread.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

int amount_active_worker_threads;
int amount_expected_worker_threads;
bool alive, empty;
int max_delay = 0;
int consumer_number = 0;
int debug_mode = false;

pthread_cond_t condstart;
pthread_cond_t condcons, condprod, cond_interruptor;
pthread_mutex_t mutex;

std::vector <pthread_t> consumers;

class mutex_guard {
public:
    mutex_guard(pthread_mutex_t &mutex) : mutex_(mutex) {
        pthread_mutex_lock(&mutex_);
    }

    ~mutex_guard() {
        pthread_mutex_unlock(&mutex_);
    }

private:
    pthread_mutex_t &mutex_;
};

class Value {
public:
    Value() : _value(0) {}

    void update(int value) {
        _value = value;
    }

    int get() const {
        return _value;
    }

private:
    int _value;
};

void wait_other() {
    mutex_guard guard(mutex);

    ++amount_active_worker_threads;
    bool need_notify = true;
    while (amount_active_worker_threads < amount_expected_worker_threads && alive) {
        need_notify = false;
        pthread_cond_wait(&condstart, &mutex);
    }
    if (need_notify) {
        pthread_cond_broadcast(&condstart);
    }
}

void sleep() {
    if (max_delay != 0) {
        int random_delay = rand() % (max_delay + 1);
        usleep(random_delay * 1000);
    }
}

int get_tid() {
    // Source:
    // http://dulanja.blogspot.com/2011/09/how-to-use-thread-local-storage-tls-in.html
    static pthread_key_t tid_key;
    static bool key_flag = true;
    static int tid_value = 1;
    static pthread_mutex_t tid_key_lock = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t tid_value_lock = PTHREAD_MUTEX_INITIALIZER;
    if (key_flag) {
        pthread_mutex_lock(&tid_key_lock);
        if (key_flag) {
            pthread_key_create(&tid_key, [](void *ptr) {
                free(ptr);
            });
            key_flag = false;
        }
        pthread_mutex_unlock(&tid_key_lock);
    }
    if (pthread_getspecific(tid_key) == NULL) {
        int *ptr = (int *) (malloc(sizeof(int)));
        pthread_mutex_lock(&tid_value_lock);
        *ptr = tid_value;
        tid_value++;
        pthread_setspecific(tid_key, ptr);
        pthread_mutex_unlock(&tid_value_lock);
        return *ptr;
    } else {
        return *(int *) (pthread_getspecific(tid_key));
    }
}

void print_debug_message(int value) {
    if (debug_mode) {
        std::cout << '(' << get_tid() << ", " << value << ')' << std::endl;
    }
}

void *producer_routine(void *arg) {
    wait_other();

    Value &value = *(Value *) arg;
    int number;
    while (alive) {
        mutex_guard guard(mutex);
        if (std::cin >> number) {
            value.update(number);
            empty = false;
            pthread_cond_signal(&condcons);
        } else {
            alive = false;
            pthread_cond_broadcast(&condcons);
        }
        while (!empty && alive) {
            pthread_cond_wait(&condprod, &mutex);
        }
    }
    return nullptr;
}

void *consumer_routine(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    wait_other();

    Value &value = *(Value *) arg;
    long sum = 0;
    while (true) {
        {
            mutex_guard guard(mutex);
            while (empty && alive) {
                pthread_cond_wait(&condcons, &mutex);
            }
            if (!alive) {
                break;
            }
            int l = value.get();
            empty = true;
            sum += l;
            print_debug_message(sum);
            pthread_cond_signal(&condprod);
        }
        sleep();
    }

    return new long(sum);
}

void *consumer_interruptor_routine(void *arg) {
    wait_other();

    std::vector <pthread_t> const &consumers = *(std::vector <pthread_t> *) arg;
    while (true) {
        for (size_t i = 0; i < consumers.size(); ++i) {
            pthread_cancel(consumers[i]);
        }
        pthread_mutex_lock(&mutex);
        bool local_alive = alive;
        pthread_mutex_unlock(&mutex);
        if (!local_alive) {
            break;
        }
        sleep();
    }
    return nullptr;
}

int run_threads() {
    Value value;
    pthread_cond_init(&condstart, nullptr);
    pthread_cond_init(&condcons, nullptr);
    pthread_cond_init(&condprod, nullptr);
    pthread_mutex_init(&mutex, nullptr);
    alive = true;
    empty = true;
    amount_active_worker_threads = 0;
    amount_expected_worker_threads = 2 + consumer_number;

    consumers.resize(consumer_number);
    for (size_t i = 0; i < consumers.size(); ++i) {
        pthread_create(&consumers[i], NULL, consumer_routine, (void *) &value);
    }

    pthread_t producer;
    pthread_t interruptor;
    pthread_create(&producer, NULL, producer_routine, (void *) &value);
    pthread_create(&interruptor, NULL, consumer_interruptor_routine, (void *) &consumers);

    pthread_join(producer, NULL);
    pthread_join(interruptor, NULL);

    long acc_result = 0;
    for (size_t i = 0; i < consumers.size(); ++i) {
        long *local_result;
        pthread_join(consumers[i], (void **) &local_result);
        acc_result += *local_result;
        delete local_result;
    }
    int ret_value = acc_result;

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condcons);
    pthread_cond_destroy(&condprod);
    pthread_cond_destroy(&condstart);

    return ret_value;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        return 1;
    }
    consumer_number = std::atoi(argv[1]);
    max_delay = std::atoi(argv[2]);
    if (argc > 3 && std::strcmp(argv[3], "-debug") == 0) {
        debug_mode = true;
    }
    std::cout << run_threads() << std::endl;
    return 0;
}
