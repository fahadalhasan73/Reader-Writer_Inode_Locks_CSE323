We will use Windows as the host operating system and Ubuntu through WSL2 as the development environment.  
<img width="779" height="392" alt="image" src="https://github.com/user-attachments/assets/189b585b-778e-44a7-970a-19939ff5c4fb" />  

This does not replace Windows, create a dual-boot setup, or require repartitioning of drive.

**Step 1 — Check whether WSL is already installed**  
Open Windows PowerShell as Administrator.
```powershell
wsl --status
```

**Step 2 — Install WSL2 and Ubuntu**  
If WSL is not installed, execute:

```powershell
wsl --install -d Ubuntu
```
This installs the necessary Windows Subsystem for Linux components and Ubuntu.  
Restart the computer if Windows asks to.  
After restarting I have face the following situation
<img width="781" height="354" alt="image" src="https://github.com/user-attachments/assets/cfb1ba8b-5a70-4b4d-9dbb-678ab1c206e9" />
This is completely normal, and installation hasn't necessarily failed. PowerShell output tells us exactly what happened.  
Why is Ubuntu not showing?  
When previously executed:  
```powershell
wsl --install -d Ubuntu
```
Windows successfully installed WSL and enabled `VirtualMachinePlatform.`  
However, the installation required a restart before those components could become active. As a result, Ubuntu itself was not installed before the restart.  
Current output:
```powershell
Windows Subsystem for Linux has no installed distributions.
```
means WSL is available, but no Linux distribution has been registered yet.  
Then reinstall the ubuntu now in powershell, run  
```powershell
wsl --install -d Ubuntu
```
After giving the command, I have faced the following error
<img width="906" height="178" alt="image" src="https://github.com/user-attachments/assets/8be4695a-103e-4685-af1d-0054107d07e6" />  

**Going for alternative solution**
**Solution 1 -**  
Step 1 — Open PowerShell as Administrator and execute 
```powershell
wsl --install --web-download -d Ubuntu
```
The `--web-download` option uses an alternative download route instead of the default Store-based installation path.  
Wait for it to finish.  
If successful, Ubuntu should begin its first-time setup and ask you to create a username and password.  
For my case, My installation done here  
<img width="904" height="387" alt="image" src="https://github.com/user-attachments/assets/00fd6f83-b552-4b25-a6f7-eda3fb158966" />  

**Solution 2 —**
If Solution 1 produces the same error, use the Microsoft Store.  
Open the - https://apps.microsoft.com/detail/9pdxgncfsczv?utm_source=chatgpt.com  
Click Get or Install. After installation, open Ubuntu from the Start menu and complete the first-time setup.  
If the Store is blocked or the installation fails, we can troubleshoot the network or use an official offline distribution package.






When Ubuntu opens for the first time, it will ask to create a Linux username and password.  

```powershell

Enter new UNIX username: fahad
New password:
Retype new password:
```
The password will not appear while typing. This is normal.  
After completing setup, you should see a terminal prompt similar to:
```powershell
fahad@DESKTOP:~$
```
That means you are inside Ubuntu. Ubuntu is ready to accept Linux commands.  

**Then Update Ubuntu**   
```powershell
sudo apt update
```
Next run
```powershell
sudo apt upgrade -y
```
These commands update Ubuntu's package information and installed packages.  

Then verify the ubuntu version
```powershell
lsb_release -a
```
This should display the Ubuntu version.  

Then run:
```powershell
uname -m
```
This shows Linux environment's CPU architecture.

For my case the output is 
```powershell
fahadalhasan@Laptop-FahadAlHasan:/mnt/c/WINDOWS/system32$ lsb_release -a                  
No LSB modules are available.                                                             
Distributor ID: Ubuntu                                                                    
Description:    Ubuntu 26.04.1 LTS                                                        
Release:        26.04                                                                     
Codename:       resolute                                                                  

fahadalhasan@Laptop-FahadAlHasan:/mnt/c/WINDOWS/system32$ uname -m

x86_64
``` 
## Next - Directory Ideas  
If the directory is  
```powershell
fahadalhasan@Laptop-FahadAlHasan:/mnt/c/WINDOWS/system32$
```
something like this, it means we are currently inside the Windows `System32` directory. We should not work there.  
Run 
```powershell
cd ~
```
Then
```powershell
pwd
```
Expected output: `/home/fahadalhasan`  
Optional: Create another folder namely CSE323 for the project workings
```powershell
mkdir CSE323
```
```powershell
cd CSE323
```
Now for `pwd` the location should be `/home/fahadalhasan/CSE323`   

**How can we find the folder inside our PC**
1. Using the path address `\\wsl.localhost\Ubuntu\home\fahadalhasan`
2. Inside the Ubuntu terminal, run:
```powershell
explorer.exe .
```
## xv6 Installation and setup ##  
xv6 is a small, Unix-like operating system developed by MIT for teaching operating system concepts.  
The most important point is that xv6 is a real operating system kernel that we can read, modify, compile, and run.  

**Difference between xv6 and QEMU?**  
xv6 is the operating system. QEMU is the emulator that runs it.

**Installing of the tools required for xv6**  
update Ubuntu's package information: 
```powershell
sudo apt update
```
Next, install the development tools:  
```powershell
sudo apt install -y git build-essential gdb-multiarch qemu-system-misc gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```
**This installs:**  
- Git: Downloads and manages xv6 source code.
- GCC: Compiles the RISC-V kernel.
- Make: Automates compilation.
- QEMU: Runs xv6 in a virtual RISC-V machine.
- GDB: Helps debug the kernel.

After Installing verify the Installs  
```powershell
git --version
```
```powershell
make --version
```
```powershell
riscv64-linux-gnu-gcc --version
```
```powershell
qemu-system-riscv64 --version
```
For my case, the output for the `qemu-system-riscv64 --version`
```powershell
fahadalhasan@Laptop-FahadAlHasan:~/CSE323$ qemu-system-riscv64 --version
Command 'qemu-system-riscv64' not found, but can be installed with:
sudo apt install qemu-system-riscv      # version 1:10.2.1+ds-1ubuntu3.1, or
sudo apt install qemu-system-riscv-hwe  # version 1:10.2.1+ds-1ubuntu4.2
```
Previously we have installed `sudo apt install qemu-system-misc`. However, our Ubuntu 26.04.1 LTS installation provides the RISC-V emulator through a separate package called `qemu-system-riscv.`  
terminal has already identified the correct package.  

**Next - Installing QEMU for RISC-V**  
```powershell
sudo apt install -y qemu-system-riscv
```

Then verify:  
```powershell
qemu-system-riscv64 --version
```
For my case, output is -
```powershell
QEMU emulator version 10.2.1 (Debian 1:10.2.1+ds-1ubuntu3.3)
Copyright (c) 2003-2025 Fabrice Bellard and the QEMU Project developers
```
Next - Download xv6 from MIT  
Run
```powershell
git clone https://github.com/mit-pdos/xv6-riscv.git
```
What this command does:  
- `git clone` downloads a Git repository.
- The URL identifies MIT's official xv6-riscv repository.
- Git creates a new folder named xv6-riscv inside CSE323 directory.

Folder Structure becomes  
```powershell
/home/fahadalhasan/
└── CSE323/
    └── xv6-riscv/
        ├── kernel/
        ├── user/
        ├── mkfs/
        ├── Makefile
        └── ...
```
Now enter the xv6 directory:
```powershell
cd xv6-riscv
```
Check the location using
```powershell
pwd
```
Expected output: `/home/fahadalhasan/CSE323/xv6-riscv`  

**Compile and run xv6 for the first time**  
Inside the xv6-riscv directory, execute:
```powershell
make qemu
```
This command performs two important tasks:
1. Compilation: GNU Make uses the RISC-V compiler to build the xv6 kernel and user programs.
2. Execution: QEMU starts an emulated RISC-V computer and boots your newly compiled xv6 operating system.

If everything works, We should eventually see:
```powershell
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$
```
The `$` indicates that we are inside the xv6 shell.  
Try 
```powershell
ls
```
Then
```powershell
echo Hello xv6
```
To exit QEMU, press `Ctrl+A`, release the keys, and press `X`.

