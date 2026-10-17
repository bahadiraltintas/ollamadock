#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 512

static void pause_enter(void){ printf("\nPress Enter to continue..."); int c; while((c=getchar())!='\n'&&c!=EOF){} }
static int read_line(const char *prompt,char *out,size_t cap){ printf("%s",prompt); fflush(stdout); if(!fgets(out,(int)cap,stdin))return 0; out[strcspn(out,"\r\n")]=0; return 1; }
static int run_argv(char *const argv[]){ pid_t p=fork(); if(p<0){perror("fork");return -1;} if(p==0){execvp(argv[0],argv);perror(argv[0]);_exit(127);} int st; while(waitpid(p,&st,0)<0){} return WIFEXITED(st)?WEXITSTATUS(st):1; }
static int have_ollama(void){ char *a[]={"ollama","--version",NULL}; return run_argv(a)==0; }
static void list_models(void){ char *a[]={"ollama","list",NULL}; (void)run_argv(a); }
static int compare_running_rows(const void *left, const void *right) {
    const char *a = *(char *const *)left;
    const char *b = *(char *const *)right;
    /* Ollama model names are the first whitespace-delimited column. */
    size_t alen = strcspn(a, " \t\r\n");
    size_t blen = strcspn(b, " \t\r\n");
    size_t n = alen < blen ? alen : blen;
    int cmp = strncasecmp(a, b, n);
    if (cmp != 0) return cmp;
    return alen < blen ? -1 : alen > blen ? 1 : 0;
}

/* Capture the complete CLI output before printing it. This avoids relying on
   a one-shot terminal display and makes every running-model row visible.
   Only `ollama ps` is used; no Ollama HTTP API is called. */
static void running_models(void) {
    FILE *fp = popen("ollama ps", "r");
    if (!fp) {
        perror("ollama ps");
        return;
    }

    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t length;
    char *header = NULL;
    char **rows = NULL;
    size_t count = 0, capacity = 0;

    while ((length = getline(&line, &line_capacity, fp)) != -1) {
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        if (length == 0) continue;
        if (!header) {
            header = strdup(line);
            if (!header) break;
            continue;
        }
        if (count == capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 8;
            char **next = realloc(rows, next_capacity * sizeof *rows);
            if (!next) break;
            rows = next;
            capacity = next_capacity;
        }
        rows[count] = strdup(line);
        if (!rows[count]) break;
        count++;
    }
    free(line);
    int status = pclose(fp);

    puts("\nCurrently running Ollama models (from `ollama ps`):");
    if (header) puts(header);
    if (count > 1) qsort(rows, count, sizeof *rows, compare_running_rows);
    for (size_t i = 0; i < count; i++) puts(rows[i]);
    if (count == 0 && header)
        puts("No models are currently loaded in memory. Start a model with option 3.");
    else if (count > 0)
        printf("\nTotal running models: %zu\n", count);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        puts("Warning: `ollama ps` returned an error; check that the Ollama service is available.");
    if (!header && count == 0 && status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0)
        puts("Ollama returned no output.");

    free(header);
    for (size_t i = 0; i < count; i++) free(rows[i]);
    free(rows);
}
#define MAX_CATALOG_MODELS 1024
#define MAX_CATALOG_URL 2048
#define CATALOG_PAGE_SIZE 20

static int compare_model_names(const void *left, const void *right) {
    const char *a = (const char *)left;
    const char *b = (const char *)right;
    return strcasecmp(a, b);
}

/* Load the complete public model-family list from Ollama's /library page.
   Pagination is local, so it does not depend on a website "Show more" cursor. */
static int fetch_website_catalog(char models[][MAX_LINE], int capacity) {
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); return -1; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);
        char *args[] = {"curl", "-fsSL", "--max-time", "45", "https://ollama.com/library", NULL};
        execvp(args[0], args);
        perror("curl (install it with: sudo apt install curl)");
        _exit(127);
    }
    close(fds[1]);
    size_t used = 0, alloc = 65536;
    char *html = malloc(alloc);
    if (!html) { close(fds[0]); waitpid(pid, NULL, 0); return -1; }
    for (;;) {
        if (used + 8192 + 1 > alloc) {
            if (alloc >= 16 * 1024 * 1024) break;
            size_t next = alloc * 2;
            char *tmp = realloc(html, next);
            if (!tmp) break;
            html = tmp; alloc = next;
        }
        ssize_t n = read(fds[0], html + used, alloc - used - 1);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fds[0]); html[used] = '\0';
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || used == 0) {
        free(html);
        puts("Could not retrieve https://ollama.com/library. Check your internet connection and curl.");
        return -1;
    }

    int count = 0;
    const char *p = html;
    while ((p = strstr(p, "href=\"/library/")) != NULL && count < capacity) {
        p += strlen("href=\"/library/");
        const char *end = strchr(p, '"');
        if (!end) break;
        size_t len = (size_t)(end - p);
        /* Library family links are one slug: reject nested paths and query strings. */
        if (len > 0 && len < MAX_LINE - 1 && !memchr(p, '/', len) && !memchr(p, '?', len) &&
            !memchr(p, '#', len)) {
            char name[MAX_LINE];
            memcpy(name, p, len); name[len] = '\0';
            int duplicate = 0;
            for (int i = 0; i < count; i++) if (!strcmp(models[i], name)) { duplicate = 1; break; }
            if (!duplicate) snprintf(models[count++], MAX_LINE, "%s", name);
        }
        p = end + 1;
    }
    free(html);
    if (count == 0) {
        puts("No model names were found on the library page. Ollama may have changed its HTML structure.");
        return -1;
    }
    qsort(models, (size_t)count, sizeof models[0], compare_model_names);
    return count;
}

static void pull_model(void) {
    char all_models[MAX_CATALOG_MODELS][MAX_LINE];
    puts("Loading model names from https://ollama.com/library ...");
    int total = fetch_website_catalog(all_models, MAX_CATALOG_MODELS);
    if (total < 0) return;
    printf("Loaded %d model names from the official library.\n", total);

    char query[MAX_LINE] = "";
    char input[64];
    int show_all = 0;
    for (;;) {
        int filtered[MAX_CATALOG_MODELS];
        int visible = 0;
        for (int i = 0; i < total; i++) {
            if (!*query || strcasestr(all_models[i], query)) filtered[visible++] = i;
        }
        if (visible == 0) {
            printf("\nNo model names match '%s'.\n", query);
            puts("S = new search | 0 = cancel");
            if (!read_line("Action: ", input, sizeof input)) return;
            if (!strcmp(input, "0")) return;
            if (tolower((unsigned char)input[0]) == 's' && input[1] == '\0') {
                if (!read_line("Filter model names (blank = all models): ", query, sizeof query)) return;
                continue;
            }
            continue;
        }

        int pages = show_all ? 1 : (visible + CATALOG_PAGE_SIZE - 1) / CATALOG_PAGE_SIZE;
        int page = 0;
        for (;;) {
            if (page < 0) page = 0;
            if (page >= pages) page = pages - 1;
            int first = show_all ? 0 : page * CATALOG_PAGE_SIZE;
            int last = show_all ? visible : first + CATALOG_PAGE_SIZE;
            if (last > visible) last = visible;
            if (show_all)
                printf("\nOfficial Ollama Library — all %d matching models (alphabetical; %d total loaded)\n", visible, total);
            else
                printf("\nOfficial Ollama Library — page %d/%d (%d matching models; %d total loaded)\n",
                       page + 1, pages, visible, total);
            for (int i = first; i < last; i++)
                printf("%3d. %s\n", show_all ? i + 1 : i - first + 1, all_models[filtered[i]]);
            puts("\nN = next page | P = previous page | S = search/filter | A = show all | 0 = cancel");
            if (!read_line("Choose a model number, or an action: ", input, sizeof input)) return;
            if (!strcmp(input, "0")) { puts("Cancelled."); return; }
            if (tolower((unsigned char)input[0]) == 'n' && input[1] == '\0') {
                if (page + 1 < pages) page++; else puts("Already on the last page.");
                continue;
            }
            if (tolower((unsigned char)input[0]) == 'p' && input[1] == '\0') {
                if (page > 0) page--; else puts("Already on the first page.");
                continue;
            }
            if (tolower((unsigned char)input[0]) == 's' && input[1] == '\0') {
                if (!read_line("Filter model names (blank = all models): ", query, sizeof query)) return;
                show_all = 0;
                break;
            }
            if (tolower((unsigned char)input[0]) == 'a' && input[1] == '\0') {
                show_all = 1;
                page = 0;
                continue;
            }
            char *end = NULL;
            long choice = strtol(input, &end, 10);
            int selectable = show_all ? visible : last - first;
            if (end == input || *end != '\0' || choice < 1 || choice > selectable) {
                puts("Invalid selection. Choose a listed model number, N, P, S, A, or 0.");
                continue;
            }
            int selected_index = show_all ? filtered[(int)choice - 1] : filtered[first + (int)choice - 1];
            const char *model = all_models[selected_index];
            printf("\nDownloading '%s' with the Ollama CLI. Progress appears below.\n\n", model);
            char *args[] = {"ollama", "pull", (char *)model, NULL};
            (void)run_argv(args);
            return;
        }
    }
}
static int get_installed_models(char models[][MAX_LINE], int capacity){
    FILE *fp = popen("ollama list", "r");
    if (!fp) { perror("ollama list"); return -1; }
    char line[2048];
    int count = 0;
    int first_line = 1;
    while (fgets(line, sizeof line, fp)) {
        if (first_line) { first_line = 0; continue; } /* Skip NAME header. */
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p) continue;
        char *end = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        size_t len = (size_t)(end - p);
        if (!len || count >= capacity) continue;
        if (len >= MAX_LINE) len = MAX_LINE - 1;
        memcpy(models[count], p, len);
        models[count][len] = '\0';
        count++;
    }
    int status = pclose(fp);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        puts("Could not read the installed model list. Check that Ollama is running.");
        return -1;
    }
    return count;
}
/* Find an executable in PATH without invoking a shell. */
static int find_executable(const char *name, char *out, size_t out_size) {
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) { snprintf(out, out_size, "%s", name); return 1; }
        return 0;
    }
    const char *env_path = getenv("PATH");
    if (!env_path) return 0;
    char *paths = strdup(env_path);
    if (!paths) return 0;
    char *save = NULL;
    for (char *dir = strtok_r(paths, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        if (!*dir) dir = ".";
        if (snprintf(out, out_size, "%s/%s", dir, name) >= (int)out_size) continue;
        if (access(out, X_OK) == 0) { free(paths); return 1; }
    }
    free(paths);
    return 0;
}

typedef enum { TERM_GNOME, TERM_KONSOLE, TERM_XFCE, TERM_MATE, TERM_XTERM, TERM_GENERIC } TerminalKind;

/* Best-effort report of a launch error to the parent. Keep the write result
   so compilers with -Wunused-result do not warn; the child exits either way. */
static void report_launch_errno(int fd, int error_number) {
    ssize_t written;
    do {
        written = write(fd, &error_number, sizeof error_number);
    } while (written < 0 && errno == EINTR);
    (void)written;
}

/* Launch a new terminal and return to the manager immediately. A double-fork
   avoids leaving a zombie when the user later closes the chat terminal. */
static int launch_model_terminal(const char *model) {
    char terminal[PATH_MAX];
    TerminalKind kind;
    const char *candidates[] = {"gnome-terminal", "konsole", "xfce4-terminal", "mate-terminal", "xterm", "x-terminal-emulator"};
    int found = 0;
    for (int i = 0; i < 6; i++) {
        if (find_executable(candidates[i], terminal, sizeof terminal)) { kind = (TerminalKind)i; found = 1; break; }
    }
    if (!found) {
        puts("No supported terminal emulator found. Install/use gnome-terminal, konsole, xfce4-terminal, mate-terminal, or xterm.");
        puts("Alternative: run 'ollama run MODEL' manually in another terminal.");
        return -1;
    }

    int errpipe[2];
    if (pipe(errpipe) != 0) { perror("pipe"); return -1; }
    if (fcntl(errpipe[1], F_SETFD, FD_CLOEXEC) < 0) {
        perror("fcntl"); close(errpipe[0]); close(errpipe[1]); return -1;
    }
    pid_t child = fork();
    if (child < 0) { perror("fork"); close(errpipe[0]); close(errpipe[1]); return -1; }
    if (child == 0) {
        close(errpipe[0]);
        if (setsid() < 0) { int e = errno; report_launch_errno(errpipe[1], e); _exit(127); }
        pid_t grandchild = fork();
        if (grandchild < 0) { int e = errno; report_launch_errno(errpipe[1], e); _exit(127); }
        if (grandchild > 0) _exit(0);

        char *args_gnome[] = {terminal, "--", "ollama", "run", (char *)model, NULL};
        char *args_konsole[] = {terminal, "-e", "ollama", "run", (char *)model, NULL};
        char *args_xfce[] = {terminal, "--execute", "ollama", "run", (char *)model, NULL};
        char *args_mate[] = {terminal, "--", "ollama", "run", (char *)model, NULL};
        char *args_xterm[] = {terminal, "-e", "ollama", "run", (char *)model, NULL};
        char *args_generic[] = {terminal, "-e", "ollama", "run", (char *)model, NULL};
        switch (kind) {
            case TERM_GNOME: execv(terminal, args_gnome); break;
            case TERM_KONSOLE: execv(terminal, args_konsole); break;
            case TERM_XFCE: execv(terminal, args_xfce); break;
            case TERM_MATE: execv(terminal, args_mate); break;
            case TERM_XTERM: execv(terminal, args_xterm); break;
            default: execv(terminal, args_generic); break;
        }
        int e = errno; report_launch_errno(errpipe[1], e); _exit(127);
    }
    close(errpipe[1]);
    int child_status;
    while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {}
    int exec_errno = 0;
    ssize_t got;
    do { got = read(errpipe[0], &exec_errno, sizeof exec_errno); } while (got < 0 && errno == EINTR);
    close(errpipe[0]);
    if (got == (ssize_t)sizeof exec_errno) {
        errno = exec_errno; perror("Could not launch model terminal"); return -1;
    }
    printf("Started chat for '%s' in a separate terminal. You can continue using this menu.\n", model);
    return 0;
}

static void run_model(void){
    char models[128][MAX_LINE];
    int count = get_installed_models(models, 128);
    if (count < 0) return;
    if (count == 0) {
        puts("No installed models found. Use option 2 to download a model first.");
        return;
    }
    puts("\nInstalled models:");
    for (int i = 0; i < count; i++) printf("%2d. %s\n", i + 1, models[i]);
    char input[32];
    if (!read_line("Choose an installed model number (0 to cancel): ", input, sizeof input)) return;
    char *end = NULL;
    long choice = strtol(input, &end, 10);
    if (end == input || *end != '\0' || choice < 0 || choice > count) {
        puts("Invalid selection."); return;
    }
    if (choice == 0) { puts("Cancelled."); return; }
    (void)launch_model_terminal(models[choice - 1]);
}
static void delete_model(void){ char name[MAX_LINE],confirm[MAX_LINE]; if(!read_line("Exact local model name to remove: ",name,sizeof name)||!name[0])return; printf("WARNING: This removes local model '%s'.\n",name); if(!read_line("Type DELETE to confirm: ",confirm,sizeof confirm)||strcmp(confirm,"DELETE")){puts("Cancelled.");return;} char *a[]={"ollama","rm",name,NULL}; (void)run_argv(a); }
static void system_info(void){ puts("System memory:"); char *m[]={"sh","-c","free -h",NULL}; (void)run_argv(m); puts("\nNVIDIA GPU (if nvidia-smi is installed):"); char *g[]={"nvidia-smi",NULL}; (void)run_argv(g); }
static void recommendations(void){ puts("General model guidance (verify each model and tag at https://ollama.com/library):\n\n- Small / lower memory: gemma3:1b, qwen3:0.6b\n- General-purpose: qwen3:8b, gemma3:4b\n- Coding: qwen2.5-coder:7b\n- Vision: gemma3:4b\n\nThese are examples, not hardware guarantees. Quantization, context length and concurrent workloads affect memory use."); }
static void catalog(void){ puts("Official Ollama model library: https://ollama.com/library"); puts("Open it in your default browser? [y/N]: "); char s[32]; if(!fgets(s,sizeof s,stdin))return; if(tolower((unsigned char)s[0])=='y'){char *a[]={"xdg-open","https://ollama.com/library",NULL};(void)run_argv(a);} puts("\nTo install a model, copy its exact model name/tag from the official site and choose Download."); }
int main(void){ if(!have_ollama()){fprintf(stderr,"Ollama CLI was not found or could not run. Install Ollama and ensure 'ollama' is on PATH.\n");return 1;} for(;;){ puts("\033[2J\033[H\033[1;36mOllamaDOCK by Bahadir Altintas \033[0m  \n======================================================"); puts("1. List installed models\n2. Browse official website models (paginated) and download\n3. Chat with installed model in a new terminal\n4. Show running models (ollama ps)\n5. Remove model (confirmation required)\n6. System RAM / NVIDIA GPU info\n7. Model recommendations\n8. Browse official Ollama model library\n0. Exit\n"); char s[32]; if(!read_line("Select: ",s,sizeof s))break; int n=atoi(s); switch(n){case 1:list_models();pause_enter();break;case 2:pull_model();pause_enter();break;case 3:run_model();break;case 4:running_models();pause_enter();break;case 5:delete_model();pause_enter();break;case 6:system_info();pause_enter();break;case 7:recommendations();pause_enter();break;case 8:catalog();pause_enter();break;case 0:return 0;default:puts("Invalid choice.");pause_enter();}} return 0;}
