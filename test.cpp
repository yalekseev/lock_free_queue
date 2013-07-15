#include <iostream>

#include "queue.h"

int main() {
    lock_free::queue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);
    queue.push(4);
    queue.push(5);
    queue.push(6);
    queue.push(7);

    int val;
    while (queue.try_pop(val)) {
        std::cout << val << std::endl;
    }

    return 0;
}
