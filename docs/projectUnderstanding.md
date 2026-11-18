**Project - AI-driven operating systems experiment**  
Where we will develop locking policies, measures their performance, improves them, and explains the results.

## Tentative Project Roadmap ##
**Phase 1 — Environment setup**  
Install WSL2, Ubuntu, QEMU, RISC-V tools, Git, and xv6. Verify that xv6 boots successfully.  
Start here  
**Phase 2 — Understand xv6**  
Learn about inodes, file-system operations, sleeplocks, spinlocks, and synchronization.  
**Phase 3 — Build the baseline**  
Examine the original inode lock, establish a working baseline, and create performance measurements.  
**Phase 4 — Implement reader-writer locking**  
Introduce shared read access and exclusive write access while preserving file-system correctness.  
**Phase 5 — Develop starvation-free policies**  
Experiment with writer-preference, fair queuing, and potentially adaptive locking policies.  
**Phase 6 — Benchmark and optimize**  
Compare reader throughput, writer waiting time, fairness, and correctness under different workloads.  
**Phase 7 — Final evaluation and documentation**  
Test unseen workloads, perform ablation experiments, maintain AI interaction logs, and prepare the final report.  

## Some prerequisite Concepts ##
**What is an inode?**  
An inode is a data structure that stores information about a file, including its size, type, and where its data blocks are located.  
In xv6, multiple processes may try to access the same inode concurrently.  

**What happens with the original locking mechanism?**  
The original xv6 inode lock is an exclusive sleeplock. Only one process can hold that inode lock at a time.  
For example, suppose three processes want to read the same file.
<img width="738" height="324" alt="image" src="https://github.com/user-attachments/assets/833247f7-1692-4d73-b0c9-0398c6eb748f" />  
<img width="740" height="387" alt="image" src="https://github.com/user-attachments/assets/40ef2c8b-ba16-44eb-97c4-bd215788f534" />  

However, the challenge is not just enabling concurrent readers.  
Suppose readers keep arriving while a writer is waiting. If the policy continuously allows new readers to enter, the writer might wait indefinitely.  
This is called **writer starvation**.  

Our project must solve both problems:  
- Allow safe concurrent reading.  
- Ensure waiting writers eventually acquire exclusive access.

Our goal: 
A good starting policy is writer preference: once a writer is waiting, prevent new readers from entering until the writer gets an opportunity to run.  
We will eventually test whether that policy provides the best balance between performance and fairness.



