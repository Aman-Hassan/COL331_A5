#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"


// SUREN AND RAGHAV KA TEST CASE

char buf[8192];
char name[3];
char *echoargv[] = { "echo", "ALL", "TESTS", "PASSED", 0 };
int stdout = 1;
#define TOTAL_MEMORY (1 << 20) + (1 << 18)


void print_free_pages(){
    printf(1, "Free pages : %d\n", getNumFreePages());
    // printf(1, "Free swap slots : %d\n", get_num_free_swap_slots());
}

void
mem(void)
{
    // printf(1,"Test Started");    
    int pid;
    uint prev_free_pages = getNumFreePages();
    long long size = ((prev_free_pages - 20) * 4096);

    printf(1,"size : %d\n", size/4096);

    print_free_pages();
    printf(1, "Allocating %d bytes for each process\n", size);

    print_free_pages();

    char* outer_malloc = (char*) malloc(100 * 4096);

    for(int i = 0; i < 100 * 4096; i++){
        outer_malloc[i] = (char) (65 + i % 26);
    }   

    print_free_pages();

    pid = fork();
    print_free_pages();
    // printf(1,"fork done\n");
    int x = 0;
    hello:

    if(pid > 0) {
        char* parent_malloc;
        if (x == 0) {
            parent_malloc = (char*) malloc(50 * 4096);
            print_free_pages();
            for(int i = 0; i < 50 * 4096; i++){
                parent_malloc[i] = (char) (65 + i % 26);
            }
            printf(1,"Parent alloc-ed\n");
        }

        for(int i = 0; i < 50 * 4096; i++){
            if (parent_malloc[i] != (char) (65 + i % 26)){
                printf(1,"parent malloc failed\n");
                goto failed;
            }
        }

        wait();

        for(int i = 0; i < 50 * 4096; i++){
            if (parent_malloc[i] != (char) (65 + i % 26)){
                goto failed;
            }
        }


        for(int i = 0; i < 100 * 4096; i++){
            if (outer_malloc[i] != (char) (65 + i % 26)){
                goto failed;
            }
        }   

        
        print_free_pages();

        printf(1,"x : %d\n", x);

        pid = fork();

        if (x < 5){
            x++;
            goto hello;
        }

    }


    else if(pid < 0){ 
        printf(1, "Fork Failed\n");
    }

    else {
        sleep(100);

        print_free_pages();

        char* malloc_child = (char*) malloc(size);

        print_free_pages();

        for(int i = 0; i < size; i++){
            malloc_child[i] = (char) (65 + i % 26);
        }

        printf(1,"Child alloc-ed\n");

        for(int i = 0; i < 100 * 4096; i++){
            if (outer_malloc[i] != (char) (65 + i % 26)){
                goto failed;
            }
        }   
    }

    if(pid > 0)
        printf(1, "Casual test case Passed !\n");
    exit();
    
failed:
    printf(1, "Casual test case Failed!\n");
    exit();
}

int
main(int argc, char *argv[])
{
    // printf(1, "Memtest starting\n");
    mem();
    // getrss();
    exit();
    return 0;
}