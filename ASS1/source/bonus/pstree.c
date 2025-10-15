#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <pwd.h>

#define MAX_PROCESSES 65536
#define MAX_CMDLINE 1024
#define MAX_COMM 256

// Process structure
typedef struct process {
    int pid;
    int ppid;
    int uid;
    int pgid;           // Process group ID
    char comm[MAX_COMM];
    char cmdline[MAX_CMDLINE];
    struct process **children;
    int child_count;
    int child_capacity;
    int thread_count;   // Number of threads for this process
    int is_thread;      // Whether this is a thread (name in {})
} process_t;

// Global options
struct {
    int show_pids;      // -p
    int show_args;      // -a
    int numeric_sort;   // -n
    int uid_changes;    // -u
    int ascii_mode;     // -A
    int show_pgids;     // -g
    int long_format;    // -l
    int compact_not;    // -c
    int highlight_pid;  // -H PID
    int show_threads;   // -t
} options = {0};

// Global process table
process_t *processes[MAX_PROCESSES];
int process_count = 0;

// Function prototypes
int is_number(const char *str);
int read_process_info(int pid, process_t *proc);
void scan_processes(void);
void build_process_tree(void);
void print_tree(process_t *proc, const char *prefix, int is_last);
void print_compact_tree(process_t *proc, const char *prefix, int is_last);
void free_processes(void);
int process_compare(const void *a, const void *b);
int is_ancestor_of(int ancestor_pid, int descendant_pid);
int should_highlight(process_t *proc);
void merge_threads(void);
int is_thread_name(const char *name);

// Check if string is a number (for PID directories)
int is_number(const char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return 0;
        }
    }
    return 1;
}

// Read process information from /proc/[pid]
int read_process_info(int pid, process_t *proc) {
    char path[256];
    FILE *file;
    
    proc->pid = pid;
    proc->children = NULL;
    proc->child_count = 0;
    proc->child_capacity = 0;
    proc->pgid = -1;
    proc->thread_count = 0;
    proc->is_thread = 0;
    
    // Read from /proc/[pid]/stat
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    
    char state;
    int tty_nr, session;
    if (fscanf(file, "%d %s %c %d %d %d %d", 
               &proc->pid, proc->comm, &state, &proc->ppid, 
               &proc->pgid, &session, &tty_nr) < 4) {
        fclose(file);
        return -1;
    }
    fclose(file);
    
    // Remove parentheses from comm
    if (proc->comm[0] == '(' && proc->comm[strlen(proc->comm) - 1] == ')') {
        proc->comm[strlen(proc->comm) - 1] = '\0';
        memmove(proc->comm, proc->comm + 1, strlen(proc->comm));
    }
    
    // Check if this is a thread (name starts with { and ends with })
    if (proc->comm[0] == '{' && proc->comm[strlen(proc->comm) - 1] == '}') {
        proc->is_thread = 1;
        // Remove the braces for comparison
        proc->comm[strlen(proc->comm) - 1] = '\0';
        memmove(proc->comm, proc->comm + 1, strlen(proc->comm));
    }
    
    // Read UID from /proc/[pid]/status
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    file = fopen(path, "r");
    proc->uid = -1;
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "Uid:", 4) == 0) {
                sscanf(line, "Uid:\t%d", &proc->uid);
                break;
            }
        }
        fclose(file);
    }
    
    // Read cmdline if needed
    proc->cmdline[0] = '\0';
    if (options.show_args) {
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        file = fopen(path, "r");
        if (file) {
            int i = 0;
            int c;
            while ((c = fgetc(file)) != EOF && i < MAX_CMDLINE - 1) {
                if (c == '\0') {
                    proc->cmdline[i++] = ' ';
                } else {
                    proc->cmdline[i++] = c;
                }
            }
            proc->cmdline[i] = '\0';
            
            // Remove trailing space
            if (i > 0 && proc->cmdline[i-1] == ' ') {
                proc->cmdline[i-1] = '\0';
            }
            fclose(file);
        }
    }
    
    return 0;
}

// Scan all processes in /proc
void scan_processes(void) {
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("opendir /proc");
        exit(1);
    }
    
    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        if (!is_number(entry->d_name)) {
            continue;
        }
        
        int pid = atoi(entry->d_name);
        process_t *proc = malloc(sizeof(process_t));
        if (!proc) {
            perror("malloc");
            exit(1);
        }
        
        if (read_process_info(pid, proc) == 0) {
            processes[process_count++] = proc;
            
            // Scan for threads in /proc/[pid]/task/ - only if we want to show thread counts
            char task_dir[256];
            snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", pid);
            DIR *task_dir_ptr = opendir(task_dir);
            if (task_dir_ptr) {
                struct dirent *task_entry;
                int thread_count = 0;
                while ((task_entry = readdir(task_dir_ptr)) != NULL) {
                    if (is_number(task_entry->d_name)) {
                        int tid = atoi(task_entry->d_name);
                        if (tid != pid) {  // Don't count the main thread
                            thread_count++;
                        }
                    }
                }
                proc->thread_count = thread_count;
                closedir(task_dir_ptr);
            } else {
                proc->thread_count = 0;
            }
        } else {
            free(proc);
        }
        
        if (process_count >= MAX_PROCESSES) {
            break;
        }
    }
    
    closedir(proc_dir);
}

// Add child to parent process
void add_child(process_t *parent, process_t *child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        parent->children = realloc(parent->children, 
                                 parent->child_capacity * sizeof(process_t *));
        if (!parent->children) {
            perror("realloc");
            exit(1);
        }
    }
    parent->children[parent->child_count++] = child;
}

// Build the process tree
void build_process_tree(void) {
    // Sort processes by PID if numeric sort is enabled
    if (options.numeric_sort) {
        qsort(processes, process_count, sizeof(process_t *), process_compare);
    }
    
    // Build parent-child relationships
    for (int i = 0; i < process_count; i++) {
        process_t *child = processes[i];
        
        // Find parent
        for (int j = 0; j < process_count; j++) {
            process_t *parent = processes[j];
            if (parent->pid == child->ppid) {
                add_child(parent, child);
                break;
            }
        }
    }
    
    // Sort children of each process
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->child_count > 0) {
            qsort(processes[i]->children, processes[i]->child_count,
                  sizeof(process_t *), process_compare);
        }
    }
}

// Compare processes for sorting
int process_compare(const void *a, const void *b) {
    process_t *proc_a = *(process_t **)a;
    process_t *proc_b = *(process_t **)b;
    
    if (options.numeric_sort) {
        return proc_a->pid - proc_b->pid;
    } else {
        return strcmp(proc_a->comm, proc_b->comm);
    }
}

// Print the process tree
void print_tree(process_t *proc, const char *prefix, int is_last) {
    if (!proc) return;
    
    // Print current process
    printf("%s", prefix);
    
    // Choose the appropriate tree characters
    const char *branch, *continue_prefix;
    if (options.ascii_mode) {
        branch = is_last ? "`-" : "|-";
        continue_prefix = is_last ? "  " : "| ";
    } else {
        branch = is_last ? "└─" : "├─";
        continue_prefix = is_last ? "  " : "│ ";
    }
    
    printf("%s", branch);
    
    // Check if this process should be highlighted
    int highlight = should_highlight(proc);
    if (highlight) {
        printf("\033[1m"); // Bold text
    }
    
    // Print process name
    if (options.show_args && strlen(proc->cmdline) > 0) {
        printf("%s", proc->cmdline);
    } else {
        printf("%s", proc->comm);
    }
    
    // Print PID if requested
    if (options.show_pids) {
        printf("(%d)", proc->pid);
    }
    
    // Print PGID if requested
    if (options.show_pgids && proc->pgid != -1) {
        printf("[%d]", proc->pgid);
    }
    
    // Print UID change if requested
    if (options.uid_changes && proc->ppid != 0) {
        // Find parent to compare UID
        process_t *parent = NULL;
        for (int i = 0; i < process_count; i++) {
            if (processes[i]->pid == proc->ppid) {
                parent = processes[i];
                break;
            }
        }
        
        if (parent && parent->uid != proc->uid && proc->uid != -1) {
            struct passwd *pw = getpwuid(proc->uid);
            if (pw) {
                printf("(user: %s)", pw->pw_name);
            } else {
                printf("(uid: %d)", proc->uid);
            }
        }
    }
    
    if (highlight) {
        printf("\033[0m"); // Reset text formatting
    }
    
    printf("\n");
    
    // Print children
    if (proc->child_count > 0) {
        char new_prefix[1024];
        snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, continue_prefix);
        
        for (int i = 0; i < proc->child_count; i++) {
            int child_is_last = (i == proc->child_count - 1);
            print_tree(proc->children[i], new_prefix, child_is_last);
        }
    }
}

// Check if a name represents a thread
int is_thread_name(const char *name) {
    return name && name[0] == '{' && name[strlen(name) - 1] == '}';
}

// Merge threads into their parent processes
void merge_threads(void) {
    // Thread counting is now done during scanning
    // This function is kept for compatibility
    return;
}

// Print compact tree (like system pstree)
void print_compact_tree(process_t *proc, const char *prefix, int is_last) {
    if (!proc || proc->is_thread) return;
    
    // Print current process
    printf("%s", prefix);
    
    // Choose the appropriate tree characters
    const char *branch, *continue_prefix;
    if (options.ascii_mode) {
        branch = is_last ? "`-" : "|-";
        continue_prefix = is_last ? "  " : "| ";
    } else {
        branch = is_last ? "└─" : "├─";
        continue_prefix = is_last ? "  " : "│ ";
    }
    
    printf("%s", branch);
    
    // Check if this process should be highlighted
    int highlight = should_highlight(proc);
    if (highlight) {
        printf("\033[1m"); // Bold text
    }
    
    // Print process name
    if (options.show_args && strlen(proc->cmdline) > 0) {
        printf("%s", proc->cmdline);
    } else {
        printf("%s", proc->comm);
    }
    
    // Print PID if requested
    if (options.show_pids) {
        printf("(%d)", proc->pid);
    }
    
    // Print PGID if requested
    if (options.show_pgids && proc->pgid != -1) {
        printf("[%d]", proc->pgid);
    }
    
    // Print thread count if there are threads
    if (proc->thread_count > 0) {
        printf("───%d*[{%s}]", proc->thread_count, proc->comm);
    }
    
    // Print UID change if requested
    if (options.uid_changes && proc->ppid != 0) {
        // Find parent to compare UID
        process_t *parent = NULL;
        for (int i = 0; i < process_count; i++) {
            if (processes[i]->pid == proc->ppid) {
                parent = processes[i];
                break;
            }
        }
        
        if (parent && parent->uid != proc->uid && proc->uid != -1) {
            struct passwd *pw = getpwuid(proc->uid);
            if (pw) {
                printf("(user: %s)", pw->pw_name);
            } else {
                printf("(uid: %d)", proc->uid);
            }
        }
    }
    
    if (highlight) {
        printf("\033[0m"); // Reset text formatting
    }
    
    // Check for single-child chain compression
    process_t *current = proc;
    int chain_length = 0;
    
    // Count non-thread children
    int non_thread_children = 0;
    for (int i = 0; i < current->child_count; i++) {
        if (!current->children[i]->is_thread) {
            non_thread_children++;
        }
    }
    
    // If this process has exactly one non-thread child, and that child also has one child,
    // we can compress the display
    if (non_thread_children == 1 && !options.compact_not) {
        process_t *single_child = NULL;
        for (int i = 0; i < current->child_count; i++) {
            if (!current->children[i]->is_thread) {
                single_child = current->children[i];
                break;
            }
        }
        
        if (single_child) {
            int single_child_non_thread_children = 0;
            for (int i = 0; i < single_child->child_count; i++) {
                if (!single_child->children[i]->is_thread) {
                    single_child_non_thread_children++;
                }
            }
            
            // Always compress single-child chains, even if the child has multiple children
            // This matches system pstree behavior
            // Print the chain on the same line
            printf("───%s", single_child->comm);
            if (options.show_pids) {
                printf("(%d)", single_child->pid);
            }
            if (single_child->thread_count > 0) {
                printf("───%d*[{%s}]", single_child->thread_count, single_child->comm);
            }
            
            // If this child has multiple children, add a branch indicator
            int child_non_thread_count = 0;
            for (int i = 0; i < single_child->child_count; i++) {
                if (!single_child->children[i]->is_thread) {
                    child_non_thread_count++;
                }
            }
            
            // Continue the chain if the child also has only one child
            current = single_child;
            if (child_non_thread_count == 1) {
                while (current && current->child_count > 0) {
                    process_t *next = NULL;
                    int next_non_thread_children = 0;
                    
                    // Find the single non-thread child
                    for (int i = 0; i < current->child_count; i++) {
                        if (!current->children[i]->is_thread) {
                            if (next == NULL) {
                                next = current->children[i];
                            }
                            next_non_thread_children++;
                        }
                    }
                    
                    if (next_non_thread_children == 1 && next) {
                        printf("───%s", next->comm);
                        if (options.show_pids) {
                            printf("(%d)", next->pid);
                        }
                        if (next->thread_count > 0) {
                            printf("───%d*[{%s}]", next->thread_count, next->comm);
                        }
                        current = next;
                        
                        // Update child count for branch indicator logic
                        child_non_thread_count = 0;
                        for (int i = 0; i < current->child_count; i++) {
                            if (!current->children[i]->is_thread) {
                                child_non_thread_count++;
                            }
                        }
                        
                        if (child_non_thread_count != 1) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }
    
    printf("\n");
    
    // Print children
    if (current->child_count > 0) {
        char new_prefix[1024];
        
        // If we compressed a chain, we need to adjust the prefix depth
        // Calculate how many levels we compressed
        int compression_depth = 0;
        process_t *temp = proc;
        while (temp != current) {
            compression_depth++;
            // Find the single non-thread child that led to current
            process_t *next = NULL;
            for (int i = 0; i < temp->child_count; i++) {
                if (!temp->children[i]->is_thread) {
                    next = temp->children[i];
                    break;
                }
            }
            if (next == NULL || next == current) {
                break;
            }
            temp = next;
        }
        
        // Build the correct prefix for the compressed chain
        snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, continue_prefix);
        
        // Calculate the exact indentation needed to align with the end of the compressed chain
        if (compression_depth > 0) {
            // Count the length of the compressed chain text
            int chain_length = 0;
            process_t *temp = proc;
            
            // Add length of the tree characters and first process name
            if (options.ascii_mode) {
                chain_length += 2; // "`-" or "|-"
            } else {
                chain_length += 2; // "└─" or "├─" (each is 3 bytes but displays as 2 chars)
            }
            chain_length += strlen(temp->comm);
            
            // Add length for each compressed process in the chain
            while (temp != current) {
                process_t *next = NULL;
                for (int i = 0; i < temp->child_count; i++) {
                    if (!temp->children[i]->is_thread) {
                        next = temp->children[i];
                        break;
                    }
                }
                if (next == NULL || next == current) {
                    break;
                }
                chain_length += 3; // "───"
                chain_length += strlen(next->comm);
                temp = next;
            }
            
            // Add spaces to align with the end of the chain
            for (int i = 0; i < chain_length; i++) {
                strncat(new_prefix, " ", sizeof(new_prefix) - strlen(new_prefix) - 1);
            }
        }
        
        int non_thread_children = 0;
        for (int i = 0; i < current->child_count; i++) {
            if (!current->children[i]->is_thread) {
                non_thread_children++;
            }
        }
        
        int printed_children = 0;
        for (int i = 0; i < current->child_count; i++) {
            if (current->children[i]->is_thread) continue;
            
            int child_is_last = (printed_children == non_thread_children - 1);
            print_compact_tree(current->children[i], new_prefix, child_is_last);
            printed_children++;
        }
    }
}

// Check if a process is an ancestor of another
int is_ancestor_of(int ancestor_pid, int descendant_pid) {
    if (ancestor_pid == descendant_pid) {
        return 1;
    }
    
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->pid == descendant_pid) {
            if (processes[i]->ppid == 0) {
                return 0; // Reached root
            }
            return is_ancestor_of(ancestor_pid, processes[i]->ppid);
        }
    }
    return 0;
}

// Check if a process should be highlighted
int should_highlight(process_t *proc) {
    if (options.highlight_pid <= 0) {
        return 0;
    }
    
    return is_ancestor_of(proc->pid, options.highlight_pid) || 
           proc->pid == options.highlight_pid;
}

// Free all allocated memory
void free_processes(void) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->children) {
            free(processes[i]->children);
        }
        free(processes[i]);
    }
}

// Print usage information
void print_usage(void) {
    printf("Usage: pstree [options] [PID|USER]\n");
    printf("Display a tree of processes.\n\n");
    printf("Options:\n");
    printf("  -a, --arguments     show command line arguments\n");
    printf("  -A, --ascii         use ASCII line drawing characters\n");
    printf("  -c, --compact-not   don't compact identical subtrees\n");
    printf("  -g, --show-pgids    show process group ids; implies -c\n");
    printf("  -H PID              highlight this process and its ancestors\n");
    printf("  -l, --long          don't truncate long lines\n");
    printf("  -n, --numeric-sort  sort output by PID\n");
    printf("  -p, --show-pids     show PIDs; implies -c\n");
    printf("  -t, --thread-names  show thread names\n");
    printf("  -u, --uid-changes   show uid transitions\n");
    printf("  -h, --help          display this help and exit\n");
}

int main(int argc, char *argv[]) {
    int option;
    int target_pid = 1; // Default to init process
    
    static struct option long_options[] = {
        {"arguments", no_argument, 0, 'a'},
        {"ascii", no_argument, 0, 'A'},
        {"compact-not", no_argument, 0, 'c'},
        {"show-pgids", no_argument, 0, 'g'},
        {"long", no_argument, 0, 'l'},
        {"numeric-sort", no_argument, 0, 'n'},
        {"show-pids", no_argument, 0, 'p'},
        {"thread-names", no_argument, 0, 't'},
        {"uid-changes", no_argument, 0, 'u'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Parse command line options
    while ((option = getopt_long(argc, argv, "aAcglnptuhH:", long_options, NULL)) != -1) {
        switch (option) {
            case 'a':
                options.show_args = 1;
                break;
            case 'A':
                options.ascii_mode = 1;
                break;
            case 'c':
                options.compact_not = 1;
                break;
            case 'g':
                options.show_pgids = 1;
                options.compact_not = 1; // -g implies -c
                break;
            case 'H':
                options.highlight_pid = atoi(optarg);
                break;
            case 'l':
                options.long_format = 1;
                break;
            case 'n':
                options.numeric_sort = 1;
                break;
            case 'p':
                options.show_pids = 1;
                options.compact_not = 1; // -p implies -c
                break;
            case 't':
                options.show_threads = 1;
                break;
            case 'u':
                options.uid_changes = 1;
                break;
            case 'h':
                print_usage();
                return 0;
            case '?':
                print_usage();
                return 1;
            default:
                print_usage();
                return 1;
        }
    }
    
    // Handle optional PID argument
    if (optind < argc) {
        if (is_number(argv[optind])) {
            target_pid = atoi(argv[optind]);
        } else {
            // Handle USER argument (simplified - just show error)
            fprintf(stderr, "User filtering not implemented\n");
            return 1;
        }
    }
    
    // Scan all processes
    scan_processes();
    
    // Build process tree
    build_process_tree();
    
    // Merge threads if not showing them explicitly
    merge_threads();
    
    // Find the root process
    process_t *root = NULL;
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->pid == target_pid) {
            root = processes[i];
            break;
        }
    }
    
    if (!root) {
        fprintf(stderr, "Process %d not found\n", target_pid);
        free_processes();
        return 1;
    }
    
    // Print the tree using compact format by default
    if (options.compact_not) {
        print_tree(root, "", 1);
    } else {
        print_compact_tree(root, "", 1);
    }
    
    // Cleanup
    free_processes();
    
    return 0;
}
