## Understanding xv6 inode locking [Fahad Al Hasan] ##
Before implementing the reader-writer lock, we need to understand how the original inode lock works. Let's start with one file.  
`file.h`  
xv6 → kernel → file.h  
Currently trying to understand the struct inode section in file.h
