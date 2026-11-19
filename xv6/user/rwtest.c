
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NUM_READERS 3
#define NUM_WRITERS 3
#define STARVATION_READERS 5

// Test exclusive writer access.
void
test_writers(void)
{
  printf("\n=== Exclusive Writer Lock Test ===\n");
  printf("Starting %d writer processes...\n", NUM_WRITERS);

  // Create the shared file before starting writer processes.
  int fd = open("rwtestwriters", O_CREATE | O_RDWR);

  if (fd < 0) {
    printf("Failed to create writer test file\n");
    exit(1);
  }

  close(fd);

  // Create multiple writer processes.
  for (int i = 0; i < NUM_WRITERS; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("Writer fork failed\n");
      exit(1);
    }

    if (pid == 0) {
      // Writer child process.
      printf("Writer %d started\n", getpid());

      // Open the shared file for writing.
      int writer_fd = open("rwtestwriters", O_WRONLY);

      if (writer_fd < 0) {
        printf("Writer %d: open failed\n", getpid());
        exit(1);
      }

      // Write a message to the shared file.
      char message[] = "Writer process data\n";

      int written = write(writer_fd, message, sizeof(message) - 1);

      if (written != sizeof(message) - 1) {
        printf("Writer %d: write failed\n", getpid());
        close(writer_fd);
        exit(1);
      }

      printf("Writer %d: write completed\n", getpid());

      close(writer_fd);
      exit(0);
    }
  }

  // Parent waits for all writer processes.
  for (int i = 0; i < NUM_WRITERS; i++) {
    wait(0);
  }

  printf("Writer processes completed!\n");
}

// Test whether writers can make progress under reader contention.
void
test_writer_starvation(void)
{
  printf("\n=== Writer Starvation Prevention Test ===\n");

  // Create a shared file for reader-writer contention.
  int fd = open("rwstarve", O_CREATE | O_RDWR);

  if (fd < 0) {
    printf("Failed to create starvation test file\n");
    exit(1);
  }

  // Write initial data so readers have content to read.
  char initial_data[] = "Reader-writer contention test\n";

  if (write(fd, initial_data, sizeof(initial_data) - 1)
      != sizeof(initial_data) - 1) {
    printf("Failed to initialize starvation test file\n");
    close(fd);
    exit(1);
  }

  close(fd);

  // Create reader processes to generate contention.
  for (int i = 0; i < STARVATION_READERS; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("Starvation test: reader fork failed\n");
      exit(1);
    }

    if (pid == 0) {
      // Reader child process.
      printf("Starvation reader %d started\n", getpid());

      // Repeatedly open, read, and close the shared file.
      // Reopening resets the file offset to zero.
      char buffer[32];

      for (int j = 0; j < 1000; j++) {
        int reader_fd = open("rwstarve", O_RDONLY);

        if (reader_fd < 0) {
          printf("Starvation reader %d: open failed\n", getpid());
          exit(1);
        }

        int n = read(reader_fd, buffer, sizeof(buffer));

        if (n < 0) {
          printf("Starvation reader %d: read failed\n", getpid());
          close(reader_fd);
          exit(1);
        }

        close(reader_fd);
      }

      printf("Starvation reader %d completed\n", getpid());
      exit(0);
    }
  }

  // Create a writer while reader processes may still be active.
  int writer_pid = fork();

  if (writer_pid < 0) {
    printf("Starvation test: writer fork failed\n");
    exit(1);
  }

  if (writer_pid == 0) {
    printf("Starvation writer %d started\n", getpid());

    int writer_fd = open("rwstarve", O_WRONLY);

    if (writer_fd < 0) {
      printf("Starvation writer %d: open failed\n", getpid());
      exit(1);
    }

    char message[] = "Writer acquired access\n";

    int written = write(writer_fd, message, sizeof(message) - 1);

    if (written != sizeof(message) - 1) {
      printf("Starvation writer %d: write failed\n", getpid());
      close(writer_fd);
      exit(1);
    }

    printf("Starvation writer %d completed\n", getpid());

    close(writer_fd);
    exit(0);
  }

  // Wait for all five readers and the writer.
  int writer_completed = 0;
  int failed_children = 0;

  for (int i = 0; i < STARVATION_READERS + 1; i++) {
    int status = -1;
    int finished_pid = wait(&status);

    if (finished_pid < 0) {
      printf("Starvation test: wait failed\n");
      exit(1);
    }

    if (status != 0) {
      failed_children++;
    }

    if (finished_pid == writer_pid && status == 0) {
      writer_completed = 1;
    }
  }

  if (writer_completed && failed_children == 0) {
    printf("Starvation test: all processes completed successfully!\n");
  } else {
    printf("Starvation test: FAILED (%d child errors)\n",
           failed_children);
  }

  unlink("rwstarve");
}

int
main(int argc, char *argv[])
{
  int fd;
  int pid;
  char buffer[64];

  printf("=== Reader-Writer Inode Lock Test ===\n");

  // Create a file containing sample data.
  fd = open("rwtestdata", O_CREATE | O_RDWR);

  if (fd < 0) {
    printf("Failed to create test file\n");
    exit(1);
  }

  if (write(fd, "Concurrent reader test data", 27) != 27) {
    printf("Failed to initialize reader test file\n");
    close(fd);
    exit(1);
  }

  close(fd);

  printf("Starting %d reader processes...\n", NUM_READERS);

  // Create multiple reader processes.
  for (int i = 0; i < NUM_READERS; i++) {
    pid = fork();

    if (pid < 0) {
      printf("Fork failed\n");
      exit(1);
    }

    if (pid == 0) {
      // Each child opens the same inode independently.
      int reader_fd = open("rwtestdata", O_RDONLY);

      if (reader_fd < 0) {
        printf("Reader %d: open failed\n", getpid());
        exit(1);
      }

      int n = read(reader_fd, buffer, sizeof(buffer) - 1);

      if (n < 0) {
        printf("Reader %d: read failed\n", getpid());
        close(reader_fd);
        exit(1);
      }

      buffer[n] = 0;

      printf("Reader %d: %s\n", getpid(), buffer);

      close(reader_fd);
      exit(0);
    }
  }

  // Parent waits for all readers.
  for (int i = 0; i < NUM_READERS; i++) {
    wait(0);
  }

  unlink("rwtestdata");

  printf("Concurrent reader test completed!\n");

  test_writers();
  test_writer_starvation();

  exit(0);
}
