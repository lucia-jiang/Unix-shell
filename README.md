# minishell
Small shell coded in C using UNIX

The "minishell" program acts as a command interpreter, designed and implemented in C within the Unix operating system environment.

## Key Functionalities

- Utilizes standard input (`file descriptor 0`) for reading and interpreting command lines.
- Utilizes standard output (`file descriptor 1`) to display results of internal commands.
- Uses standard error (`file descriptor 2`) to present special variables such as "prompt" and "bgpid," as well as for error notifications.
- Manages errors through the `perror` library function in the event of system call failures.

## Components and Concepts

- **Command:** A sequence of texts separated by whitespace, where the first text specifies the command to execute, and subsequent texts represent its arguments.
- **Sequence:** A series of two or more commands separated by the `|` character. The standard output of each command is connected via a pipe to the standard input of the next command in the sequence.
- **Redirection:** The input and/or output of a command or sequence can be redirected using `<` and `>` notations.

## Additional Features

- **Background Execution:** Commands or sequences terminated with `&` are executed asynchronously in the background, updating the `bgpid` variable.
- **Signal Handling:** Ensures that neither the "minishell" nor background-launched commands terminate due to keyboard-generated signals.
- **Internal Commands:** Includes internal commands like `cd`, `umask`, `limit`, and `set`, executed either directly or as complements provided by the "minishell."
- **Filename Expansion:** Text containing specific characters (e.g., `~` or `$`) undergoes special character substitution before execution, expanding to filenames that match the specified patterns.

## Name Expansion and Constraints

Furthermore, the program features name expansion for filenames, expanding to lists of matching filenames based on specified patterns. This applies to characters such as `?` and adheres to certain restrictions, particularly when combined with the `/` character.

---

**Note:** The following files are not included, as they were created by the professor of the "Operating Systems" course:

- **Makefile:** Facilitates automatic recompilation of modified source files.
- **scanner.l:** Automatically generates C code for a lexical analyzer (scanner) capable of recognizing the TXT token, considering possible separators (\t␣|<>&\n).
- **parser.y:** Automatically generates C code for a grammar analyzer (parser) capable of recognizing valid statements based on the input grammar of the minishell.
