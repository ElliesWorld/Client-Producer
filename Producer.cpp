#include <string>
#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <semaphore.h>
#include <fcntl.h>
#include <cerrno>

class Product
{
public:
    int id;
    std::string name;
    int price;

    Product(int i, std::string n, int p) : id(i), name(std::move(n)), price(p) {}

    void display() const
    {
        std::cout << "ID: " << id << "\nName: " << name << "\nPrice: " << price << "\n\n";
    }

    void serialize(char *buffer) const
    {
        std::memcpy(buffer, &id, sizeof(id));
        std::strncpy(buffer + sizeof(id), name.c_str(), 64);
        std::memcpy(buffer + sizeof(id) + 64, &price, sizeof(price));
    }

    static Product deserialize(const char *buffer)
    {
        int id;
        char name[64];
        int price;
        std::memcpy(&id, buffer, sizeof(id));
        std::strncpy(name, buffer + sizeof(id), 64);
        std::memcpy(&price, buffer + sizeof(id) + 64, sizeof(price));
        return Product(id, std::string(name), price);
    }
};

void producer(mqd_t mq, const std::vector<Product> &products, sem_t *full, sem_t *empty, sem_t *output)
{
    char buffer[sizeof(int) + 64 + sizeof(int)];
    size_t product_index = 0;

    while (true)
    {
        Product product = products[product_index % products.size()];
        product.id = rand() % 9000 + 1000; 
        product.serialize(buffer);

        sem_wait(empty);
        if (mq_send(mq, buffer, sizeof(buffer), 0) == -1)
        {
            sem_post(empty);
            sem_wait(output);
            std::cerr << "Error: Producer failed to send message: " << strerror(errno) << std::endl;
            sem_post(output);
            break;
        }
        sem_post(full);
        product_index++;
        sleep(rand() % 3 + 1);
    }
}

void consumer(mqd_t mq, int id, sem_t *full, sem_t *empty, sem_t *output)
{
    char buffer[sizeof(int) + 64 + sizeof(int)];

    while (true)
    {
        sem_wait(full);
        if (mq_receive(mq, buffer, sizeof(buffer), NULL) == -1)
        {
            sem_post(full);
            sem_wait(output);
            std::cerr << "Error: Consumer " << id << " failed to receive message: " << strerror(errno) << std::endl;
            sem_post(output);
            break;
        }
        sem_post(empty);

        Product product = Product::deserialize(buffer);
        sem_wait(output);
        std::cout << "=== Consumer: " << id << " ===\n";
        product.display();
        sem_post(output);
        sleep(rand() % 3 + 1);
    }
}

int main()
{
    std::vector<Product> products = {
        Product(0, "iPhone 14 Pro Max", 14000),
        Product(0, "Samsung Galaxy S23 5G", 12000),
        Product(0, "Apple Watch S9 45mm GPS+CEL", 7000),
        Product(0, "Samsung Galaxy Watch5 Pro 45mm LTE", 6000)};

    // Define message queue attributes
    struct mq_attr attr = {};
    attr.mq_flags = 0; // Blocking mode
    attr.mq_maxmsg = 8;
    attr.mq_msgsize = sizeof(int) + 64 + sizeof(int); // ID + Name (64 chars) + Price

    // Unlink any stale queue
    mq_unlink("/store");
    mqd_t mq = mq_open("/store", O_RDWR | O_CREAT, 0644, &attr);
    if (mq == (mqd_t)-1)
    {
        std::cerr << "Error: Unable to open message queue: " << strerror(errno) << std::endl;
        return 1;
    }

    // Create semaphores
    sem_t *full = sem_open("/full", O_CREAT, 0644, 0);     // Initially 0 (no items)
    sem_t *empty = sem_open("/empty", O_CREAT, 0644, 8);   // Initially max capacity
    sem_t *output = sem_open("/output", O_CREAT, 0644, 1); // Binary semaphore for std::cout

    if (full == SEM_FAILED || empty == SEM_FAILED || output == SEM_FAILED)
    {
        std::cerr << "Error: Unable to create semaphores: " << strerror(errno) << std::endl;
        return 1;
    }

    // Create producer process
    if (fork() == 0)
    {
        producer(mq, products, full, empty, output);
        exit(0);
    }

    // Create consumer processes
    for (int i = 0; i < 4; ++i)
    {
        if (fork() == 0)
        {
            consumer(mq, i + 1, full, empty, output);
            exit(0);
        }
    }

    // Wait for all child processes
    for (int i = 0; i < 5; ++i)
    { // 1 producer + 4 consumers
        wait(NULL);
    }

    // Clean up
    mq_close(mq);
    mq_unlink("/store");
    sem_close(full);
    sem_close(empty);
    sem_close(output);
    sem_unlink("/full");
    sem_unlink("/empty");
    sem_unlink("/output");

    return 0;
}