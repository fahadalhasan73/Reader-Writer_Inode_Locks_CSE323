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
That means you are inside Ubuntu.
