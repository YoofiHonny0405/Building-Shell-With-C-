void builtin_ls(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        try {
            for (const auto &entry : fs::directory_iterator(fs::current_path()))
                std::cout << entry.path().filename().string() << std::endl;
        } catch (const fs::filesystem_error &e) {
            std::cerr << "ls: " << e.what() << std::endl;
        }
        return;
    }
    for (size_t i = 1; i < args.size(); ++i) {
        std::string path = args[i];
        if (!fs::exists(path))
            std::cerr << "ls: " << path << ": No such file or directory" << std::endl;
        else if (fs::is_directory(path)) {
            try {
                for (const auto &entry : fs::directory_iterator(path))
                    std::cout << entry.path().filename().string() << std::endl;
            } catch (const fs::filesystem_error &e) {
                std::cerr << "ls: " << e.what() << std::endl;
            }
        } else {
            std::cout << fs::path(path).filename().string() << std::endl;
        }
    }
}

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::unordered_set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd", "ls"};
    // Only print the prompt if STDOUT is a terminal.
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    while (true) {
        if (isatty(STDOUT_FILENO)) {
            std::cout << "$ ";
            std::cout.flush();
        }
        std::string input;
        char c;
        while (true) {
            c = getchar();
            if (c == '\n') {
                input.push_back(c);
                break;
            } else if (c == '\t') {
                input = autocomplete(input, builtins);
                if (isatty(STDOUT_FILENO)) {
                    std::cout << "\r$ " << input;
                    std::cout.flush();
                }
            } else if (c == 127) { // Handle backspace.
                if (!input.empty()) {
                    input.pop_back();
                    if (isatty(STDOUT_FILENO)) {
                        std::cout << "\r$ " << input;
                        std::cout.flush();
                    }
                }
            } else {
                input.push_back(c);
                if (isatty(STDOUT_FILENO)) {
                    std::cout << c;
                    std::cout.flush();
                }
            }
        }
        // If no more input is available, break.
        if (feof(stdin))
            break;
        if (input == "exit 0\n")
            break;
        Command cmd = parseCommand(input);
        if (cmd.args.empty())
            continue;
        std::string command = unescapePath(cmd.args[0]);
        if (command == "cd") {
            handleCdCommand(cmd.args);
            continue;
        }
        if (command == "pwd") {
            handlePwdCommand();
            continue;
        }
        if (command == "type") {
            if (cmd.args.size() > 1)
                handleTypeCommand(cmd.args[1], builtins);
            else
                std::cerr << "type: missing operand" << std::endl;
            continue;
        }
        if (command == "ls") {
            // Execute builtin ls in a child process.
            pid_t pid = fork();
            if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | O_CLOEXEC | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                } else {
                    int devNull = open("/dev/null", O_WRONLY);
                    if (devNull != -1) {
                        dup2(devNull, STDERR_FILENO);
                        close(devNull);
                    }
                }
                builtin_ls(cmd.args);
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
                continue;
            }
        }
        if (command == "echo") {
            pid_t pid = fork();
            if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | O_CLOEXEC | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                }
                std::string echoArg;
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    if (cmd.args[i] == ">" || cmd.args[i] == "1>" ||
                        cmd.args[i] == "2>" || cmd.args[i] == "1>>" || cmd.args[i] == "2>>")
                        break;
                    echoArg += cmd.args[i] + " ";
                }
                echoArg = trim(echoArg);
                std::cout << processEchoLine(echoArg) << std::endl;
                exit(0);
            } else {
                int status;
                waitpid(pid, &status, 0);
                std::fflush(stderr);
                if (isatty(STDOUT_FILENO))
                    std::cout << std::endl;
            }
        }
        else {
            // External commands.
            pid_t pid = fork();
            if (pid == -1) {
                std::cerr << "Failed to fork process" << std::endl;
            } else if (pid == 0) {
                if (!cmd.outputFile.empty()) {
                    fs::path outputPath(cmd.outputFile);
                    try {
                        if (!fs::exists(outputPath.parent_path()))
                            fs::create_directories(outputPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for output file: "
                                  << outputPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int out_fd = open(cmd.outputFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendOutput ? O_APPEND : O_TRUNC),
                                      0644);
                    if (out_fd == -1) {
                        std::cerr << "Failed to open output file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);
                }
                if (!cmd.errorFile.empty()) {
                    fs::path errorPath(cmd.errorFile);
                    try {
                        if (!fs::exists(errorPath.parent_path()))
                            fs::create_directories(errorPath.parent_path());
                    } catch (const fs::filesystem_error &e) {
                        std::cerr << "Failed to create directory for error file: "
                                  << errorPath.parent_path() << " - " << e.what() << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    int err_fd = open(cmd.errorFile.c_str(),
                                      O_WRONLY | O_CREAT | (cmd.appendError ? O_APPEND : O_TRUNC),
                                      0644);
                    if (err_fd == -1) {
                        std::cerr << "Failed to open error file: " << strerror(errno) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    dup2(err_fd, STDERR_FILENO);
                    close(err_fd);
                } else {
                    int devNull = open("/dev/null", O_WRONLY);
                    if (devNull != -1) {
                        dup2(devNull, STDERR_FILENO);
                        close(devNull);
                    }
                }
                std::vector<char*> execArgs;
                for (const auto& arg : cmd.args) {
                    std::string unescaped = unescapePath(arg);
                    char* arg_copy = strdup(unescaped.c_str());
                    execArgs.push_back(arg_copy);
                }
                execArgs.push_back(nullptr);
                if (execvp(execArgs[0], execArgs.data()) == -1) {
                    std::cerr << command << ": command not found" << std::endl;
                    for (char* arg : execArgs)
                        if (arg)
                            free(arg);
                    exit(EXIT_FAILURE);
                }
            } else {
                int status;
                waitpid(pid, &status, 0);
                std::fflush(stderr);
                if (isatty(STDOUT_FILENO))
                    std::cout << std::endl;
            }
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}