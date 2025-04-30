# 🐚 Custom UNIX Shell in C

This project is a fully functional UNIX-like shell implemented in **C**. It replicates core shell features such as command execution, redirection, piping, and more. The shell provides a command-line interface to interact with the operating system, similar to `bash` or `sh`.

---

## 🚀 Features

1. **Command Prompt**
   - Displays the current working directory appended with a custom prompt (`sh> `) and reads user input interactively.
   
        E.g., `/mnt/d/C's/UNIX_Shell sh`

2. **Command Execution**
   - Executes standard system commands using `fork()` and `exec()` system calls.
   - To be specific all the executable programs listed in the root's bin directory can be executed in this custom shell.
   - Example: `pwd` will display the current working directory, `ls` will list files and folders, `mkdir` for creating directory etc.

3. **Input and Output Redirection**
   - Supports `<` for input redirection.
   - Supports `>` for overwrite output redirection.
   - Supports `>>` for append output redirection.
   - Example:  
     ```sh
     cat < input.txt > output.txt
     ```

4. **Command Piping**
   - Supports piping (`|`) between **any number of commands**.
   - Example:  
     ```sh
     cat file.txt | grep "data" | sort | uniq
     ```

5. **Multiple Commands**
   - Allows multiple commands on the same line separated by a semicolon (`;`).
   - Example:  
     ```sh
     pwd; ls; whoami
     ```

6. **Conditional Command Execution**
   - Supports `&&` to run the next command only if the previous one succeeds.
   - Example:  
     ```sh
     mkdir test && cd test
     ```

7. **Command History**
   - Maintains a history of executed commands during the shell session. By typing `history` onto the shell, all the commands executed while running the program will be listed.

8. **Signal Handling**
   - Handles `SIGINT` (`Ctrl+C`) to terminate only the foreground process, **not** the shell itself.

---
**Commands like `exit`, `cd` & `history` were handled in a separate function as thses are not executables from the bin directory so the "execvp()" function won't work for these.**

## 🛠️ Build Instructions

### Prerequisites
- GCC compiler
- POSIX-compliant system (Linux, macOS, WSL, etc.)

### Compile
Use the following command to compile the shell:
```bash
gcc -o myshell main.c
