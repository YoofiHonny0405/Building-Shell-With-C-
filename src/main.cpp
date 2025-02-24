#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

char *builtin_commands[] = {"echo", "exit", NULL};

char *command_generator(const char *text, int state) {
    static int list_index, len;
    const char *name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while ((name = builtin_commands[list_index++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

char **command_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
}

int main() {
    rl_attempted_completion_function = command_completion;

    while (1) {
        char *input = readline("$ ");
        if (input == NULL) break;

        add_history(input);

        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        } else if (strncmp(input, "echo ", 5) == 0) {
            printf("%s\n", input + 5);
        } else {
            printf("Command not found: %s\n", input);
        }

        free(input);
    }

    return 0;
}
