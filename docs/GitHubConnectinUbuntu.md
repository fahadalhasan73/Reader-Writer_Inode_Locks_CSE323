# GitHub CLI Login on Ubuntu (WSL)

A step-by-step guide to signing in to GitHub from an Ubuntu terminal running under Windows Subsystem for Linux (WSL), including what to do when the browser cannot open automatically.

## Environment

- **Operating system:** Ubuntu on Windows Subsystem for Linux (WSL)
- **GitHub account:** `fahadalhasan73`
- **Project directory:** `~/CSE323/Reader-Writer_Inode_Locks_CSE323`
- **Authentication method:** GitHub CLI (`gh`) browser/device authentication
- **Git protocol:** HTTPS

> **Security:** Never put a one-time device code, GitHub access token, or other secret in a public README, screenshot, or shared documentation.

## 1. Open your Ubuntu terminal

Launch Ubuntu through WSL, or open the Ubuntu terminal within Visual Studio Code.

Optionally navigate to your repository:

```bash
cd ~/CSE323/Reader-Writer_Inode_Locks_CSE323
```

GitHub login is not tied to this directory; the `gh` authentication applies to the current Ubuntu user.

## 2. Check whether GitHub CLI is installed

```bash
gh --version
```

If the command is not found, install GitHub CLI using the official instructions at <https://cli.github.com/>. On Ubuntu systems where the package is available from configured repositories, you can try:

```bash
sudo apt update
sudo apt install gh
```

Then verify it again with `gh --version`.

## 3. Start GitHub authentication

Run:

```bash
gh auth login
```

Follow the interactive prompts. For the setup described here, select:

1. **GitHub.com** as the account host.
2. **HTTPS** as the preferred Git protocol.
3. **Login with a web browser** as the authentication method.

The CLI will display a **one-time device code** and prompt you to press Enter to open a browser. Keep the terminal running and keep the code private.

## 4. Handle the browser-opening error in WSL

You might see:

```text
Press Enter to open github.com in your browser...
! Failed opening a web browser at https://github.com/login/device
  exec: "xdg-open,x-www-browser,www-browser,wslview": executable file not found in $PATH
  Please try entering the URL in your browser manually
```

### Why this occurs

Ubuntu in WSL tried to launch a browser using a Linux or WSL browser-opening utility, but it could not find one in the terminal's `PATH`.

**This does not mean your GitHub authentication has failed.** You can finish authorization manually in a Windows browser.

## 5. Complete device authentication in Windows

1. **Keep the Ubuntu terminal open** so the ongoing `gh auth login` command can complete.
2. Open **Chrome, Edge, or another browser** in Windows.
3. Go to **<https://github.com/login/device>**.
4. Sign in to the intended GitHub account (`fahadalhasan73`) if necessary.
5. Enter the **one-time code shown in your Ubuntu terminal**.
6. Select **Continue**, review the requested access, and select **Authorize GitHub CLI**.

Do not send the one-time code to anyone or paste it into a shared document.

## 6. Return to Ubuntu

Return to the Ubuntu/WSL terminal after authorizing GitHub CLI in the browser.

The waiting authentication command should complete and return you to the normal shell prompt. If it reports an expired code or fails, restart the flow with:

```bash
gh auth login
```

## 7. Verify that GitHub login succeeded

Run:

```bash
gh auth status
```

The successful output from this setup showed the following (sensitive token details intentionally omitted):

```text
github.com
  ✓ Logged in to github.com account fahadalhasan73 (/home/fahadalhasan/.config/gh/hosts.yml)
  - Active account: true
  - Git operations protocol: https
  - Token: [REDACTED]
  - Token scopes: 'gist', 'read:org', 'repo', 'workflow'
```

### What the status means

| Status field | Meaning |
|---|---|
| `Logged in to github.com account fahadalhasan73` | GitHub CLI has authenticated the expected account. |
| `Active account: true` | This is the active account used by GitHub CLI. |
| `Git operations protocol: https` | GitHub CLI is configured to use HTTPS for Git operations. |
| `Token: [REDACTED]` | A credential is stored; never publish the actual value. |
| `Token scopes` | The listed permissions were granted to the CLI credential. |

**Result: Authentication completed successfully.**

## 8. Check repository status before working

From the project directory:

```bash
cd ~/CSE323/Reader-Writer_Inode_Locks_CSE323
git status
git branch --show-current
git remote -v
```

These commands let you verify the working tree, current branch, and configured remote. They do not push any changes.

### Optional: Work on the feature branch

If the branch already exists locally:

```bash
git switch feature/import-xv6
```

If it exists only on the remote, first fetch it and then switch:

```bash
git fetch origin
git switch --track origin/feature/import-xv6
```

Once you have made and committed your own changes, and after checking the branch and remote, you can push this branch with:

```bash
git push -u origin feature/import-xv6
```

> This push command is an **optional next step**, not part of the login itself. If the remote branch has changed since you last fetched it, first inspect and integrate those changes rather than force-pushing.

## 9. Troubleshooting

### GitHub authentication succeeded, but Git push still fails

Check your remote URL and Git authentication configuration:

```bash
git remote -v
gh auth status
gh auth setup-git
```

`gh auth setup-git` configures Git to use GitHub CLI as a credential helper. It may be useful when HTTPS Git operations are not using your CLI authentication.

### The device code expired

Run `gh auth login` again to obtain a new code, then complete authorization at <https://github.com/login/device>.

### You signed in to the wrong account

Use `gh auth status` to check the active account. If needed, use `gh auth switch` when multiple accounts are configured or `gh auth logout` followed by `gh auth login` to authenticate again.

### The WSL browser error occurs again

Manual browser authorization remains a valid workaround. Installing and configuring browser-opening utilities is optional; you do not need to resolve the browser-opening error to use GitHub CLI successfully.

## 10. Quick command reference

```bash
# Check GitHub CLI installation
gh --version

# Start GitHub login
gh auth login

# Verify authentication
gh auth status

# Configure Git to use gh credentials if required
gh auth setup-git

# Inspect the project repository
git status
git branch --show-current
git remote -v
```

**Official references:**

- GitHub CLI: <https://cli.github.com/>
- `gh auth login` manual: <https://cli.github.com/manual/gh_auth_login>
- `gh auth status` manual: <https://cli.github.com/manual/gh_auth_status>
- GitHub device authorization: <https://github.com/login/device>

---

**Final outcome:** The GitHub CLI was successfully authenticated on Ubuntu/WSL as `fahadalhasan73` using HTTPS. The automatic browser-opening failure was resolved by authorizing the device code manually in a Windows browser.
