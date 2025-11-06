#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

// Global variables initalzed
double cpu_usage;
double usedMem, totalMem, memoryPercent;
int numProcess;
pthread_mutex_t lock;

// By default interval is set to 1 second
// Logging is initalized as false
int interval = 1, logging = 0;

volatile sig_atomic_t running = 1;

FILE* loggingFile = NULL;

// Handle exit gracefully by turning running to 0 in case of SIGNINT
void handle_sigint(){
    running = 0;

}

// Reads CPU time stats from /proc/stat 
// and compute the percentage of CPU utilization from last call
// Returns: Percentage CPU usage as a double

double getCpuUsage() {

    static double cpuUtilization = 0.0;

    // read from /proc/stat
    FILE *file = fopen("/proc/stat", "r");

    if (!file) return -1;

    char buffer[512];
    // reads from file buffer of 512
    fgets(buffer, sizeof(buffer), file);
    fclose(file);

    // names from https://www.linuxhowtos.org/System/procstat.htm
    unsigned long long user, nice, system, idle, ioWait, irq, softirq;

    // read from string in memory
    sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu",
    &user, &nice, &system, &idle, &ioWait, &irq, &softirq);

    static unsigned long long past_total = 0;
    static unsigned long long past_idle = 0;


    // cpu util = (total-idle)/total
    unsigned long long current_idle_time = idle + ioWait;
    unsigned long long current_total_time = idle + ioWait + user + nice + system + irq + softirq;

    unsigned long long diff_total = current_total_time - past_total;
    unsigned long long diff_idle = current_idle_time - past_idle;

    // saves current values to next iteration
    past_idle = current_idle_time;
    past_total = current_total_time;

    // avoids dividing by 0 error by returning last valid cpuUtilization value
    if (diff_total == 0) return cpuUtilization;

    cpuUtilization = 100*((double)(diff_total - diff_idle) / (double)diff_total);

    return cpuUtilization;
}


// 
// Sample Memory Read
//MemTotal:       196137884 kB
//MemAvailable:        169652440 kB
// Argumets:
// used: pointer thats stores used memory
// total: pointer to store the total memory
// percent: pointer that stores percentage for display

void getMemoryUsage(double *used, double *total, double *percent){
    // read from /proc/meminfo
    FILE *file = fopen("/proc/meminfo", "r");

    if (!file) return;

    char line[128];

    unsigned long mem_avail = 0, mem_total = 0;

    while (fgets(line, sizeof(line), file)){

        sscanf(line, "MemTotal: %lu kB", &mem_total);

        sscanf(line, "MemAvailable: %lu kB", &mem_avail);

        if (mem_avail && mem_total){
            break;
        }
    }

    if (mem_total == 0){
        printf("MemTotal not found in /proc/meminfo\n");
    }
    if (mem_avail == 0){
         printf("MemAvailable not found in /proc/meminfo\n");
    }

    fclose(file);

    // mem_total and mem_avail are divided by 1024^2 to convert kB to gB
    double total_mem_in_gb = mem_total / (1024.0 * 1024.0);
    double used_gb = (mem_total - mem_avail) / (1024.0 * 1024.0);
    double mem_used_percentage = 100.0 * ((double)(mem_total - mem_avail) / (double)mem_total);

    *used = used_gb;
    *total = total_mem_in_gb;
    *percent = mem_used_percentage;

}
// Iterates through /proc directory and counts entries that start with digit
// ensure that its a process
//
int getCountProcesses(){
    // Implementation follows
    // reading from a directory https://c-for-dummies.com/blog/?p=3246

    DIR *folder = opendir("/proc");

    if (!folder) {
        printf("Unable to acess folder \n");
        return -1;
    }

    struct dirent *entry;
    int count = 0;

    while ( (entry=readdir(folder))){
        if (entry != NULL){
            if (isdigit(entry->d_name[0])){
                count++;
            }
        }
    }

    closedir(folder);

    return count;
}

void *cpu_thread(void *arg){
    while (running)
    {
        pthread_mutex_lock(&lock);
        cpu_usage = getCpuUsage();
        pthread_mutex_unlock(&lock);

        // sleeping is added so instantaneous values in sucession won't result the 
        // numerator of the function to be 0 (therefore result 0)
        sleep(interval);
        
    }
    return NULL;
    
}

void *process_thread(void *arg){
    while (running)
    {
        pthread_mutex_lock(&lock);
        numProcess = getCountProcesses();
        pthread_mutex_unlock(&lock);
        sleep(interval);

    }
        
     return NULL;
    
}

void *memory_thread(void *arg){
    while (running)
    {
        pthread_mutex_lock(&lock);
        getMemoryUsage(&usedMem, &totalMem, &memoryPercent);
        pthread_mutex_unlock(&lock);
        sleep(interval);
        
    }
    return NULL;
    
}

int main(int argc, char *argv[]) {

    signal(SIGINT, handle_sigint);

    time_t now;
    char time_string[32];
    struct tm *local_time;

    // iterates through cli arguments to parse arguemnts of 
    // --logging and --interval X where X is number of seconds >= 1
    if (argc > 1){
        for (int i = 1; i < argc; i++){
        if (strcmp(argv[i], "--interval") == 0){
            if (i + 1 < argc){
                interval = atoi(argv[++i]);

                // case that interval is not valid
                if (interval < 1){
                    printf("Interval must be greater than 1\n");
                    return 0;
                }
            }else{
                printf("Not a valid interval\n");
                return 0;
            }
        } 
        else if (strcmp(argv[i], "--logging") == 0){
            // Logging Mode Active!
            logging = 1;
            printf("Logging mode Enabled! Statistics are appended to system_stats.log\n");
            // writes file for log
            loggingFile = fopen("system_stats.log", "a");
        }
        else{
            printf("Not a valid flag\n");
            return 0;
        }
    }
    }

    // Initalization of thread, and lock
    pthread_t cpuThread, processThread, memThread;
    pthread_mutex_init(&lock, NULL);
    // Creation of threads
    pthread_create(&cpuThread, NULL, cpu_thread, NULL);
    pthread_create(&processThread, NULL, process_thread, NULL);
    pthread_create(&memThread, NULL, memory_thread, NULL);


    printf("System Resource Monitor (Interval of %d)\n", interval);

    // while true, the print statement loops
    while(running){
        sleep(interval);

        // thread is locked so that log write and console print are synced
        pthread_mutex_lock(&lock);
        if (logging == 1 && loggingFile){
            // logging is active!
            // get current time
            now = time(NULL);
            local_time = localtime(&now);

            strftime(time_string, sizeof(time_string), "[%Y-%m-%d %H:%M:%S]", local_time);

            fprintf(loggingFile, "%s CPU: %.2f%%, Memory: %.2f GB/%.2f GB, Processes: %d\n", 
                time_string ,cpu_usage, usedMem, totalMem, numProcess);
            fflush(loggingFile);
        }

        printf("-------------------------------------------------\n");
        printf("CPU Usage: %.2f%% \n", cpu_usage);
        printf("Memory Usage: %.2f GB/%.2f GB (%.2f%%)\n", usedMem, totalMem, memoryPercent);
        printf("Running Processes: %d\n", numProcess);
        printf("-------------------------------------------------\n");
        printf("(Updating every %d second(s)...Press Ctrl+C to exit)\n", interval);
        pthread_mutex_unlock(&lock);
        

    }

    // Exiting gracefully when CTRL+C is pressed

     // wait for threads to join
    pthread_join(cpuThread, NULL);
    pthread_join(processThread, NULL);
    pthread_join(memThread, NULL);

    pthread_mutex_destroy(&lock);

    if (loggingFile != NULL) {
        fclose(loggingFile);
    };


    printf("Exiting Gracefully, Goodbye!\n");

    return 0;
}