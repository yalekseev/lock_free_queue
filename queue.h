#ifndef QUEUE_H
#define QUEUE_H

#include <atomic>
#include <memory>

namespace lock_free {

template <typename T>
class queue {
private:
    struct node_type;

    struct counted_node_type {
        int m_external_count;
        node_type * m_node_ptr;
    };

    struct node_counter_type {
        int m_internal_count:30;
        unsigned m_external_counters:2;
    };

    struct node_type {
        node_type() {
            // Set node counter
            node_counter_type new_counter;
            new_counter.m_internal_count = 0;
            new_counter.m_external_counters = 2;
            m_counter.store(new_counter);

            // Set next counted node
            counted_node_type next_counted_node = { 0, 0 };
            m_next.store(next_counted_node);
        }

        void release_ref() {
            node_counter_type old_node_counter = m_counter.load();
            node_counter_type new_node_counter;

            do {
                new_node_counter = old_node_counter;
                --new_node_counter.m_internal_count;
            } while (!m_counter.compare_exchange_strong(old_node_counter, new_node_counter));

            if (new_node_counter.m_internal_count == 0 && new_node_counter.m_external_counters == 0) {
                delete this;
            }
        }

        std::atomic<T *> m_data;
        std::atomic<node_counter_type> m_counter;
        std::atomic<counted_node_type> m_next;
    };

public:
    queue();

    bool try_pop(T & data);

    void push(const T & data);

    // TODO: unsafe_empty();
    // TODO: unsafe_clear();
    // TODO: unsafe_size();

private:
    static void increase_external_count(
            std::atomic<counted_node_type> & counted_node,
            counted_node_type & old_counted_node);

    static void free_external_counter(counted_node_type & old_counted_node);

    void set_new_tail(counted_node_type & old_tail, const counted_node_type & new_tail);

    std::atomic<counted_node_type> m_head;
    std::atomic<counted_node_type> m_tail;
};

template <typename T>
queue<T>::queue() {
    counted_node_type new_counted_node;
    new_counted_node.m_external_count = 1;
    new_counted_node.m_node_ptr = new node_type;

    m_head.store(new_counted_node);
    m_tail.store(new_counted_node);
}

template <typename T>
bool queue<T>::try_pop(T & data) {
    counted_node_type old_head = m_head.load();   

    while (true) {
        // TODO: get_head();
        increase_external_count(m_head, old_head);
        node_type * node_ptr = old_head.m_node_ptr;
        if (node_ptr == m_tail.load().m_node_ptr) {
            return false;
        }

        counted_node_type next_counted_node = node_ptr->m_next.load();
        if (m_head.compare_exchange_strong(old_head, next_counted_node)) {
            data = *(node_ptr->m_data.load());
            free_external_counter(old_head);
            return true;
        }

        node_ptr->release_ref();
    }
}

template <typename T>
void queue<T>::push(const T & data) {
    // std::shared_ptr can't be used here because of it's internal locks
    std::unique_ptr<T> new_data(new T(data));
    counted_node_type new_next;
    new_next.m_node_ptr = new node_type;
    new_next.m_external_count = 1;

    counted_node_type old_tail = m_tail.load();

    while (true) {
        // TODO: get_tail();
        increase_external_count(m_tail, old_tail);

        T * old_data = 0;
        if (old_tail.m_node_ptr->m_data.compare_exchange_strong(old_data, new_data.get())) {
            counted_node_type old_next = { 0 };
            if (!old_tail.m_node_ptr->m_next.compare_exchange_strong(old_next, new_next)) {
                delete new_next.m_node_ptr;
                new_next = old_next;
            }

            set_new_tail(old_tail, new_next);
            new_data.release();
            return;
        } else {
            counted_node_type old_next = { 0 };
            if (old_tail.m_node_ptr->m_next.compare_exchange_strong(old_next, new_next)) {
                old_next = new_next;
                new_next.m_node_ptr = new node_type;
            }

            set_new_tail(old_tail, old_next);
        }
    }

}

template <typename T>
void queue<T>::increase_external_count(
        std::atomic<counted_node_type> & counted_node,
        counted_node_type & old_counted_node) {
    counted_node_type new_counted_node;
    do {
        new_counted_node = old_counted_node;
        ++new_counted_node.m_external_count;
    } while (!counted_node.compare_exchange_strong(old_counted_node, new_counted_node));

    old_counted_node.m_external_count = new_counted_node.m_external_count;
}

template <typename T>
void queue<T>::free_external_counter(counted_node_type & old_counted_node) {
    node_type * node_ptr = old_counted_node.m_node_ptr;
    int count_increase = old_counted_node.m_external_count - 2;

    node_counter_type old_node_counter = node_ptr->m_counter.load();
    node_counter_type new_node_counter;

    do {
        new_node_counter = old_node_counter;
        --new_node_counter.m_external_counters;
        new_node_counter.m_internal_count += count_increase;
    } while (!node_ptr->m_counter.compare_exchange_strong(old_node_counter, new_node_counter));

    if (new_node_counter.m_internal_count == 0 && new_node_counter.m_external_counters == 0) {
        delete node_ptr;
    }
}

template <typename T>
void queue<T>::set_new_tail(counted_node_type & old_tail, const counted_node_type & new_tail) {
    node_type * current_tail_ptr = old_tail.m_node_ptr;

    while (!m_tail.compare_exchange_strong(old_tail, new_tail) && old_tail.m_node_ptr == current_tail_ptr) {
        ;
    }

    if (old_tail.m_node_ptr == current_tail_ptr) {
        free_external_counter(old_tail);
    } else {
        current_tail_ptr->release_ref();
    }
}

} // namespace lock_free

#endif
