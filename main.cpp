#include <pthread.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <cstring>

pthread_mutex_t mutex;
pthread_cond_t condprod;
pthread_cond_t condcons;
pthread_cond_t condstart;


int max_delay = 0;
int consumer_number = 0;
int debug_mode = false;

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

enum class Status {
    STARTED,
    FINISHED
};

enum class ValueStatus {
    NEEDSPRODUCER,
    NEEDSCONSUMER
};

namespace shared {
    Status status;
    ValueStatus value_status;
    int result = 0;
    Value value;
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
            pthread_key_create(&tid_key, [](void* ptr) {
                free(ptr);
            });
            key_flag = false;
        }
        pthread_mutex_unlock(&tid_key_lock);
    }
    if (pthread_getspecific(tid_key) == NULL) {
        int* ptr = (int*)(malloc(sizeof(int)));
        pthread_mutex_lock(&tid_value_lock);
        *ptr = tid_value;
        tid_value++;
        pthread_setspecific(tid_key, ptr);
        pthread_mutex_unlock(&tid_value_lock);
        return *ptr;
    } else {
        return *(int*)(pthread_getspecific(tid_key));
    }
}

void print_debug_message(Value *value) {
    if (debug_mode) {
        std::cout << '(' << get_tid() << ", " << value->get() << ')' << std::endl;
    }
}

void *producer_routine(void *arg) {
    if (shared::status != Status::STARTED) {
        pthread_mutex_lock(&mutex);
    }
    while (shared::status != Status::STARTED) {
        pthread_cond_wait(&condstart, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    Value *value = static_cast<Value *>(arg);
    std::vector<int> values;
    int number;

    while (std::cin >> number) {
        values.push_back(number);
    }

    for (std::vector<int>::iterator it = values.begin(); it != values.end(); ++it) {
        pthread_mutex_lock(&mutex);
        value->update(*it);
        shared::value_status = ValueStatus::NEEDSCONSUMER;
        pthread_cond_signal(&condprod);

        while (shared::value_status != ValueStatus::NEEDSPRODUCER) {
            pthread_cond_wait(&condcons, &mutex);
        }
        pthread_mutex_unlock(&mutex);
    }
    pthread_mutex_lock(&mutex);
    shared::status = Status::FINISHED;
    pthread_cond_broadcast(&condprod);
    pthread_mutex_unlock(&mutex);

    return nullptr;
}

void *consumer_routine(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);

    pthread_mutex_lock(&mutex);
    shared::status = Status::STARTED;
    pthread_cond_broadcast(&condstart);
    pthread_mutex_unlock(&mutex);

    Value *value = static_cast<Value *>(arg);

    while (true) {
        pthread_mutex_lock(&mutex);
        while (shared::value_status != ValueStatus::NEEDSCONSUMER &&
               shared::status != Status::FINISHED) {
            pthread_cond_wait(&condprod, &mutex);
        }

        if (shared::status == Status::FINISHED) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        shared::result += value->get();
        print_debug_message(value);
        shared::value_status = ValueStatus::NEEDSPRODUCER;
        pthread_cond_signal(&condcons);
        pthread_mutex_unlock(&mutex);

        int random_delay = rand() % max_delay;
        usleep(random_delay);
    }
    return &shared::result;

}

void *consumer_interruptor_routine(void *arg) {
    if (shared::status != Status::STARTED) {
        pthread_mutex_lock(&mutex);
    }
    std::vector <pthread_t> *threads = static_cast<std::vector <pthread_t> *>(arg);
    while (shared::status != Status::STARTED) {
        pthread_cond_wait(&condstart, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    while (shared::status != Status::FINISHED) {
        int thread_to_cancel = rand() % threads->size();
        pthread_cancel(threads->at(thread_to_cancel));
    }
    return nullptr;
}

int run_threads() {
    pthread_cond_init(&condstart, nullptr);
    pthread_cond_init(&condprod, nullptr);
    pthread_cond_init(&condcons, nullptr);
    pthread_mutex_init(&mutex, nullptr);

    pthread_t producer;
    pthread_t interruptor;
    std::vector <pthread_t> consumers(consumer_number);

    pthread_create(&producer, nullptr, producer_routine, &shared::value);
    for (auto &cons : consumers) {
        pthread_create(&cons, nullptr, consumer_routine, &shared::value);
    }
    pthread_create(&interruptor, nullptr, consumer_interruptor_routine, &consumers);

    pthread_join(producer, nullptr);
    pthread_join(interruptor, nullptr);
    for (auto &cons : consumers) {
        pthread_join(cons, nullptr);
    }

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condcons);
    pthread_cond_destroy(&condprod);
    pthread_cond_destroy(&condstart);

    return shared::result;
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
