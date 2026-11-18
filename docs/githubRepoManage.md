Everyone should pull from main, create their own feature branch, push that feature branch, and then merge it into main through a pull request.
<img width="752" height="579" alt="image" src="https://github.com/user-attachments/assets/8d44a30d-7fd5-4f1c-98b5-b3ae3547fd4e" />

## Work Flow for everyone ##
First clone the repository
```powershell
git clone https://github.com/fahadalhasan73/Reader-Writer_Inode_Locks_CSE323.git
cd Reader-Writer_Inode_Locks_CSE323
```
If the repository is already cloned 
```powershell
git switch main
git pull --ff-only origin main
```
Then create a new feature branch. Suppose you're responsible for the reader-writer lock implementation
```powershell
git switch -c feature/rw-inode-lock
```
Now you're free to modify your kernel files without directly changing `main`.  

**Then make changes and commit**  
For example, after modifying the inode locking implementation:  
```powershell
git status
git add xv6/kernel/fs.c xv6/kernel/file.h
git commit -m "Implement reader-writer inode locking"
```
Only stage the files relevant to your change. Include other modified source files when necessary.  
Then push your feature branch 
```powershell
git push -u origin feature/rw-inode-lock
```
This created your branch on GitHub  

**Next — Create a pull request**    
On GitHub, open a pull request with:  
- Base: `main`
- Compare: `feature/rw-inode-lock`  
Your groupmates can review your implementation, request changes, and discuss potential issues.  
After the code passes the required tests and review, merge it into main.

**Then — Synchronize again**  
Once the Pull Request is merged, everyone updates their local `main`:  
```powershell
git switch main
git pull --ff-only origin main
```



