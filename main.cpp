#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr size_t kShmSize = sizeof(int);

void ExampleFork() {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return;
  }

  if (pid == 0) {
    std::cout << "Child PID: " << getpid() << ", Parent PID: " << getppid() << '\n';
    _exit(0);
  }

  std::cout << "Parent PID: " << getpid() << ", Child PID: " << pid << '\n';
  waitpid(pid, nullptr, 0);
}

void ExamplePipe() {
  int fd[2];
  if (pipe(fd) == -1) {
    perror("pipe");
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(fd[0]);
    close(fd[1]);
    return;
  }

  if (pid == 0) {
    close(fd[1]);
    int x = 0;
    ssize_t bytes = read(fd[0], &x, sizeof(x));
    if (bytes == sizeof(x)) {
      std::cout << "Child read: " << x << ", square: " << x * x << '\n';
    } else if (bytes == 0) {
      std::cout << "Child read EOF without data.\n";
    } else {
      perror("read");
    }
    close(fd[0]);
    _exit(0);
  }

  close(fd[0]);
  std::cout << "Enter an integer: ";
  int x = 0;
  std::cin >> x;
  if (!std::cin.fail()) {
    if (write(fd[1], &x, sizeof(x)) != sizeof(x)) {
      perror("write");
    }
  } else {
    std::cout << "Invalid input.\n";
  }
  close(fd[1]);
  waitpid(pid, nullptr, 0);
}

void ExampleSharedMemory() {
  std::string shm_name = "/exam_shm_" + std::to_string(getpid());
  std::string sem_name = "/exam_sem_" + std::to_string(getpid());

  int shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    return;
  }

  if (ftruncate(shm_fd, kShmSize) == -1) {
    perror("ftruncate");
    close(shm_fd);
    shm_unlink(shm_name.c_str());
    return;
  }

  void* addr = mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (addr == MAP_FAILED) {
    perror("mmap");
    close(shm_fd);
    shm_unlink(shm_name.c_str());
    return;
  }

  sem_t* sem = sem_open(sem_name.c_str(), O_CREAT | O_EXCL, 0666, 0);
  if (sem == SEM_FAILED) {
    perror("sem_open");
    munmap(addr, kShmSize);
    close(shm_fd);
    shm_unlink(shm_name.c_str());
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    sem_close(sem);
    sem_unlink(sem_name.c_str());
    munmap(addr, kShmSize);
    close(shm_fd);
    shm_unlink(shm_name.c_str());
    return;
  }

  if (pid == 0) {
    sem_wait(sem);
    int value = *reinterpret_cast<int*>(addr);
    std::cout << "Shared memory read: " << value << ", square: " << value * value << '\n';
    sem_close(sem);
    munmap(addr, kShmSize);
    close(shm_fd);
    _exit(0);
  }

  int input = 0;
  std::cout << "Enter an integer for shared memory: ";
  std::cin >> input;
  if (!std::cin.fail()) {
    *reinterpret_cast<int*>(addr) = input;
    sem_post(sem);
  } else {
    std::cout << "Invalid input.\n";
  }

  waitpid(pid, nullptr, 0);
  sem_close(sem);
  sem_unlink(sem_name.c_str());
  munmap(addr, kShmSize);
  close(shm_fd);
  shm_unlink(shm_name.c_str());
}

void ExampleThreadsA() {
  std::atomic<int> x{0};
  std::atomic<bool> running{true};

  std::thread incrementer([&]() {
    for (int i = 0; i < 5; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      ++x;
    }
    running = false;
  });

  std::thread printer([&]() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "x = " << x.load() << '\n';
    }
  });

  incrementer.join();
  printer.join();
}

void ExampleThreadsB() {
  std::atomic<int> x{0};
  std::atomic<bool> done{false};

  std::thread worker([&]() {
    while (x <= 10) {
      ++x;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    done = true;
  });

  std::thread watcher([&]() {
    while (!done) {
      std::cout << "x = " << x.load() << '\n';
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  worker.join();
  watcher.join();
}

void ExampleArrayA() {
  std::vector<int> values{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::atomic<int> i{0};
  std::atomic<int> j{static_cast<int>(values.size()) - 1};

  auto left = [&]() {
    while (true) {
      int k = i.fetch_add(1);
      if (k > j.load()) {
        break;
      }
      std::cout << "Left thread: " << values[k] << '\n';
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  };

  auto right = [&]() {
    while (true) {
      int k = j.fetch_sub(1);
      if (k < i.load()) {
        break;
      }
      std::cout << "Right thread: " << values[k] << '\n';
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  };

  std::thread t1(left);
  std::thread t2(right);
  t1.join();
  t2.join();
}

void ExampleArrayB() {
  const int n = 100;
  const int m = 4;
  std::vector<int> values(n);
  for (int i = 0; i < n; ++i) {
    values[i] = i + 1;
  }

  std::atomic<long long> global_sum{0};
  std::vector<std::thread> threads;
  threads.reserve(m);

  int base = n / m;
  int remainder = n % m;
  int start = 0;

  for (int t = 0; t < m; ++t) {
    int count = base + (t < remainder ? 1 : 0);
    int local_start = start;
    int local_end = start + count;
    start = local_end;

    threads.emplace_back([&, local_start, local_end]() {
      long long local_sum = 0;
      for (int i = local_start; i < local_end; ++i) {
        local_sum += values[i];
      }
      global_sum.fetch_add(local_sum);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  std::cout << "Sum = " << global_sum.load() << '\n';
}

void PrintUsage(const char* program) {
  std::cout << "Usage: " << program
            << " [fork|pipe|shm|threadA|threadB|arrayA|arrayB]\n";
}
}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string mode = argv[1];
  if (mode == "fork") {
    ExampleFork();
  } else if (mode == "pipe") {
    ExamplePipe();
  } else if (mode == "shm") {
    ExampleSharedMemory();
  } else if (mode == "threadA") {
    ExampleThreadsA();
  } else if (mode == "threadB") {
    ExampleThreadsB();
  } else if (mode == "arrayA") {
    ExampleArrayA();
  } else if (mode == "arrayB") {
    ExampleArrayB();
  } else {
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}
